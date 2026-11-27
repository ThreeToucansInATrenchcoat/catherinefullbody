#pragma once
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
#include "written-page-presence.h"
#include "C:/catherine/native-gpu-experiment/Source/native_xyz_copy.h"
#include "C:/catherine/native-gpu-experiment/Source/native_xyz_match.h"

namespace nx::nvn::replay {
// Only a skin dispatch that consumed a substituted render palette produces
// already-interpolated world positions. Morph outputs and native/local skin
// palettes must remain eligible for their c3 attachment transform.
class InterpolatedSkinOutputs {
    struct Span { uintptr_t begin, end; };
    std::vector<Span> spans;
    uint64_t frame = ~0ull;
    bool substituted = false;

    static bool range(const uint8_t* p, uint64_t size, Span& out) {
        if (!p || !size) return false;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(p);
        if (size > UINTPTR_MAX - begin) return false;
        out = {begin, begin + uintptr_t(size)};
        return true;
    }

public:
    void clear() { spans.clear(); substituted = false; frame = ~0ull; }
    void use_frame(uint64_t current) {
        if (frame != current) { clear(); frame = current; }
    }
    void begin_dispatch(uint64_t current) { use_frame(current); substituted = false; }
    void palette_interpolated() { substituted = true; }
    // A later writer supersedes only the bytes it replaces. Keep unaffected
    // slices so adjacent objects sharing one guest allocation stay independent.
    void overwrite(const uint8_t* p, uint64_t size) {
        Span write;
        if (!range(p, size, write)) return;
        std::vector<Span> kept;
        kept.reserve(spans.size() + 1);
        for (Span old : spans) {
            if (old.end <= write.begin || write.end <= old.begin) kept.push_back(old);
            else {
                if (old.begin < write.begin) kept.push_back({old.begin, write.begin});
                if (write.end < old.end) kept.push_back({write.end, old.end});
            }
        }
        spans.swap(kept);
    }
    void complete_skin(const uint8_t* p, uint64_t size, bool known_skin) {
        Span output;
        if (!substituted || !known_skin || !range(p, size, output)) return;
        overwrite(p, size);
        spans.push_back(output);
        std::sort(spans.begin(), spans.end(), [](Span a, Span b) { return a.begin < b.begin; });
        size_t count = 0;
        for (Span span : spans) {
            if (count && span.begin <= spans[count - 1].end)
                spans[count - 1].end = std::max(spans[count - 1].end, span.end);
            else spans[count++] = span;
        }
        spans.resize(count);
    }
    bool contains(const uint8_t* p, uint64_t size, uint64_t current) {
        use_frame(current);
        Span query;
        if (!range(p, size, query)) return false;
        for (Span span : spans)
            if (span.begin <= query.begin && query.end <= span.end) return true;
        return false;
    }
};

// The upload region lasts one submit; its world-space vertex data can survive
// in guest memory much longer. Keep that provenance only while the actual xyz
// stream still equals the completed producer's bytes. GPU producers become
// persistent only at the existing fenced write-back point.
class VerifiedSkinOutputs {
    WrittenPagePresence produced_pages;
    static int page_mode(){static const int mode=[] {const char* e=std::getenv("CATHERINE_PROVENANCE_PAGES");return e?std::atoi(e):1;}();return mode;}

    struct Record {
        uintptr_t begin = 0;
        uint64_t count = 0, stride = 0, submit = 0, used = 0;
        bool pending = true;
        std::vector<uint8_t> xyz;
        const uint8_t* writeback_source = nullptr;
        bool invalid = false;
        uint64_t verified105 = 0;            // cmd105: memo_epoch of the last successful match
        const uint8_t* verified_at105 = nullptr;
    };
    uint64_t memo_epoch105 = 1;
public:
    uint64_t memo_hits105 = 0, memo_checked105 = 0, memo_wrong105 = 0;
    uint64_t page_queries=0,page_rejects=0;
private:
    static int mode105() {
        static const int m = [] { const char* e = std::getenv("CATHERINE_PARALLEL105"); return e ? std::atoi(e) : 1; }();
        return m;
    }
    std::vector<Record> records;
    std::vector<Record> overwrite_scratch;
    bool prune_dirty = false;
    struct RangeCandidates {
        uint64_t generation=0;
        uintptr_t address=0;
        uint64_t count=0, stride=0;
        uint32_t size=0, scanned=0;
        std::array<uint32_t,8> indices{};
        bool overflow=false;
    };
    std::array<RangeCandidates,16384> range_cache{};
    uint64_t shape_generation=1;
    std::vector<size_t> address_order;
    uint64_t address_generation=0;
    std::vector<size_t> writeback_candidates;
    const std::vector<size_t>& ordered_records() {
        if (address_generation != shape_generation) {
            address_order.resize(records.size());
            for (size_t i=0; i<records.size(); ++i) address_order[i]=i;
            std::sort(address_order.begin(), address_order.end(), [&](size_t a,size_t b) {
                return records[a].begin < records[b].begin ||
                       (records[a].begin == records[b].begin && a < b);
            });
            address_generation=shape_generation;
        }
        return address_order;
    }
    void shape_changed() {
        if (++shape_generation==0) {
            for(auto& entry:range_cache) entry.generation=0;
            shape_generation=1;
            address_generation=0;
        }
    }
    template<class Predicate,class View> bool query_candidates(uintptr_t address,uint64_t count,uint64_t stride,Predicate predicate,View&& view) {
        uint64_t hash=(uint64_t(address)>>4)^(count*0x9e3779b97f4a7c15ull)^(stride*0xbf58476d1ce4e5b9ull);
        hash^=hash>>30;hash*=0xbf58476d1ce4e5b9ull;hash^=hash>>27;hash*=0x94d049bb133111ebull;hash^=hash>>31;
        auto& entry=range_cache[hash&16383];
        if(entry.generation!=shape_generation || entry.address!=address || entry.count!=count || entry.stride!=stride) {
            entry={};entry.generation=shape_generation;entry.address=address;entry.count=count;entry.stride=stride;
            if(records.size()>UINT32_MAX) entry.overflow=true;
        }
        if(entry.overflow) {
            for(auto& record:records) if(predicate(record) && eligible(record,view)) return true;
            return false;
        }
        for(uint32_t i=0;i<entry.size;++i) if(eligible(records[entry.indices[i]],view)) return true;
        // Learn lazily: never scan beyond the original first successful record.
        // If it later fails its byte check, resume the unchanged record-order search.
        for(size_t i=entry.scanned;i<records.size();++i) {
            entry.scanned=uint32_t(i+1);
            if(!predicate(records[i])) continue;
            if(entry.size<entry.indices.size()) entry.indices[entry.size++]=uint32_t(i);
            else entry.overflow=true;
            if(eligible(records[i],view)) return true;
        }
        return false;
    }

    uint64_t submit = 0, serial = 0;

    static bool valid(const uint8_t* p, uint64_t count, uint64_t stride) {
        return p && count && stride >= 12 && count <= UINT64_MAX / stride &&
               count * stride <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(p);
    }
    static Record slice(const Record& record, uint64_t first, uint64_t count) {
        // cmd110: copy everything but xyz (swapped out for the copy; records are never const objects)
        Record& source = const_cast<Record&>(record);
        std::vector<uint8_t> held;
        held.swap(source.xyz);
        Record result = source;
        held.swap(source.xyz);
        result.begin += first * record.stride;
        if (result.writeback_source) result.writeback_source += first * record.stride;
        result.count = count;
        if (!record.xyz.empty())
            result.xyz.assign(record.xyz.begin() + first * 12,
                              record.xyz.begin() + (first + count) * 12);
        return result;
    }
    static void capture(Record& record, const uint8_t* bytes) {
        record.xyz.resize(record.count * 12);
        uint8_t* out = record.xyz.data();
        static const int mode=[] {const auto*e=std::getenv("CATHERINE_NATIVE_XYZ_COPY");return e?std::atoi(e):1;}();
        if(!mode)experimental_xyz::scalar(out,bytes,record.count,record.stride);
        else {
            experimental_xyz::native(out,bytes,record.count,record.stride);
            if(mode==2){std::vector<uint8_t> oracle(record.count*12);experimental_xyz::scalar(oracle.data(),bytes,record.count,record.stride);if(oracle!=record.xyz)std::abort();}
        }
    }
    static bool matches(const Record& record, const uint8_t* bytes) {
        if (!bytes || record.xyz.size() != record.count * 12) return false;
        return experimental_xyz_match::equal(record.xyz.data(),bytes,record.count,record.stride);
    }
    template<class View> bool eligible(Record& record, View&& view) {
        if (record.invalid) return false;
        // This submit's GPU write is known to produce world positions, but its
        // bytes cannot be read until the fence. Do not guess older GPU results.
        if (record.pending && record.submit != submit) return false;
        const auto* bytes = view(reinterpret_cast<const uint8_t*>(record.begin), record.count * record.stride);
        if (record.pending && record.xyz.empty()) return true;
        const int m105 = mode105();
        const bool memo = m105 && bytes && record.verified105 == memo_epoch105 && record.verified_at105 == bytes;
        if (memo && m105 == 1) {
            ++memo_hits105;
            record.used = ++serial;
            return true;
        }
        const bool now = matches(record, bytes);
        if (memo) { ++memo_checked105; if (!now) ++memo_wrong105; }
        if (now) record.verified105 = memo_epoch105, record.verified_at105 = bytes;
        if (!now) {
            if (bytes) { record.invalid = true; prune_dirty = true; }
            return false;
        }
        record.used = ++serial;
        return true;
    }
    void prune() {
        if (!prune_dirty) return;
        ++memo_epoch105;
        prune_dirty = false;
        const size_t old_count=records.size();
        std::erase_if(records, [](const Record& record) { return record.invalid; });
        if(records.size()!=old_count) shape_changed();
        size_t bytes = 0;
        for (const Record& record : records) bytes += record.xyz.size();
        while (records.size() > 1024 || bytes > (32u << 20)) {
            auto oldest = records.end();
            for (auto it = records.begin(); it != records.end(); ++it)
                if (!it->pending && (oldest == records.end() || it->used < oldest->used)) oldest = it;
            if (oldest == records.end()) break; // In-flight snapshots belong to their fenced write-backs.
            bytes -= oldest->xyz.size();
            records.erase(oldest);
            shape_changed();
        }
    }

public:
    void begin_submit() { ++submit; ++memo_epoch105; }
    void clear() { produced_pages.clear();records.clear(); overwrite_scratch.clear(); prune_dirty = false; shape_changed(); ++submit; ++memo_epoch105; }
    void overwrite(const uint8_t* p, uint64_t size) {
        ++memo_epoch105;
        if (!p || !size || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(p)) return;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(p), end = begin + uintptr_t(size);
        // Padding/normal-only and disjoint writes cannot affect XYZ provenance.
        static const bool in_place141 = [] { const char* e = std::getenv("CATHERINE_PARALLEL141"); return !(e && e[0] == '0'); }();
        bool overlaps_xyz = false, split141 = false;
        for (const auto& record : records) {
            if (end <= record.begin || begin >= record.begin + record.count * record.stride) continue;
            uint64_t first = begin > record.begin ? (begin - record.begin) / record.stride : 0;
            if (first < record.count && record.begin + first * record.stride + 12 <= begin) ++first;
            const uint64_t last = std::min<uint64_t>(record.count, (end - 1 - record.begin) / record.stride + 1);
            if (first < last) {
                overlaps_xyz = true;
                if (first && last < record.count) split141 = true;
                if (!in_place141 || split141) break;
            }
        }
        if (!overlaps_xyz) return;
        if (in_place141 && !split141) {  // cmd141: trim/drop in place, compact in order
            size_t w = 0;
            const size_t n = records.size();
            for (size_t i = 0; i < n; ++i) {
                Record& record = records[i];
                if (!(end <= record.begin || begin >= record.begin + record.count * record.stride)) {
                    uint64_t first = begin > record.begin ? (begin - record.begin) / record.stride : 0;
                    if (first < record.count && record.begin + first * record.stride + 12 <= begin) ++first;
                    const uint64_t last = std::min<uint64_t>(record.count, (end - 1 - record.begin) / record.stride + 1);
                    if (first < last) {
                        if (first) {                                  // keep [0, first)
                            if (!record.xyz.empty()) record.xyz.resize(first * 12);
                            record.count = first;
                        } else if (last < record.count) {             // keep [last, count)
                            if (!record.xyz.empty()) record.xyz.erase(record.xyz.begin(), record.xyz.begin() + last * 12);
                            record.begin += last * record.stride;
                            if (record.writeback_source) record.writeback_source += last * record.stride;
                            record.count -= last;
                        } else {
                            continue;                                 // whole record dropped
                        }
                    }
                }
                if (w != i) records[w] = std::move(record);
                ++w;
            }
            records.erase(records.begin() + w, records.end());
            shape_changed();
            prune_dirty = true;
            return;
        }
        auto& kept = overwrite_scratch;
        kept.clear();
        kept.reserve(records.size() + 1);
        for (auto& record : records) {
            if (end <= record.begin || begin >= record.begin + record.count * record.stride) {
                kept.push_back(std::move(record));
                continue;
            }
            uint64_t first = begin > record.begin ? (begin - record.begin) / record.stride : 0;
            if (first < record.count && record.begin + first * record.stride + 12 <= begin) ++first;
            const uint64_t last = std::min<uint64_t>(record.count, (end - 1 - record.begin) / record.stride + 1);
            if (first >= last) kept.push_back(std::move(record)); // Normal/padding-only update.
            else {
                if (first) kept.push_back(slice(record, 0, first));
                if (last < record.count) kept.push_back(slice(record, last, record.count - last));
            }
        }
        records.swap(kept);
        shape_changed();
        kept.clear(); // Release removed snapshots immediately, retain only vector capacity.
        prune_dirty = true;
    }
    void produced(const uint8_t* p, uint64_t count, uint64_t stride, const uint8_t* cpu_bytes) {
        ++memo_epoch105;
        if (!valid(p, count, stride)) return;
        produced_pages.mark(reinterpret_cast<uintptr_t>(p),count*stride);
        overwrite(p, count * stride);
        Record record{reinterpret_cast<uintptr_t>(p), count, stride, submit, ++serial, true, {}};
        if (cpu_bytes) capture(record, cpu_bytes);
        records.push_back(std::move(record));
        shape_changed();
        prune_dirty = true;
        prune();
    }
    void seal_write_back(const uint8_t* from, const uint8_t* to, uint64_t size) {
        ++memo_epoch105;
        if (!from || !to || !size || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(to)) return;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(to), end = begin + uintptr_t(size);
        const auto& order=ordered_records();
        auto first=std::lower_bound(order.begin(),order.end(),begin,
            [&](size_t index,uintptr_t address){ return records[index].begin < address; });
        for (;first!=order.end() && records[*first].begin<end;++first) {
            auto& record=records[*first];
            if (record.pending && record.submit == submit &&
                record.begin + record.count * record.stride <= end)
                record.writeback_source = from + (record.begin - begin);
        }
    }
    // Called after apply_write_back, when from is host-ready/fenced and guest
    // positions have actually been updated. Guest races that prevent an exact
    // match do not certify an unrelated/reused allocation.
    void write_back(const uint8_t* from, const uint8_t* to, uint64_t size) {
        ++memo_epoch105;
        if (!from || !to || !size || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(to)) return;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(to), end = begin + uintptr_t(size);
        const auto& order=ordered_records();
        auto first=std::lower_bound(order.begin(),order.end(),begin,
            [&](size_t index,uintptr_t address){ return records[index].begin < address; });
        writeback_candidates.clear();
        for (;first!=order.end() && records[*first].begin<end;++first)
            writeback_candidates.push_back(*first);
        std::sort(writeback_candidates.begin(),writeback_candidates.end());
        for (size_t index : writeback_candidates) {
            auto& record=records[index];
            if (!record.pending || record.begin + record.count * record.stride > end ||
                record.writeback_source != from + (record.begin - begin)) continue;
            const uint64_t offset=record.begin-begin;
            prune_dirty=true;
            capture(record,from+offset);
            if (!matches(record,to+offset)) { record.invalid=true; continue; }
            record.pending=false;
            record.used=++serial;
        }
        prune();
    }
    template<class View> bool contains(const uint8_t* p, uint64_t size, View&& view) {
        if (!p || !size || size > 12) return false;
        const uintptr_t address = reinterpret_cast<uintptr_t>(p);
        const auto predicate=[&](const Record& record) {
            if(address<record.begin) return false;
            const uint64_t delta=address-record.begin;
            return delta/record.stride<record.count && delta%record.stride+size<=12;
        };
        const bool possible=!page_mode()||produced_pages.may_overlap(address,size);
        if(page_mode()){++page_queries;if(!possible)++page_rejects;}
        if(!possible&&page_mode()==1){prune();return false;}
        if(query_candidates(address,size,0,predicate,view)) {if(!possible)std::abort();return true;}
        prune();
        return false;
    }
    template<class View> bool positions(const uint8_t* p, uint64_t count, uint64_t stride, View&& view) {
        if (!valid(p, count, stride)) return false;
        const uintptr_t address = reinterpret_cast<uintptr_t>(p);
        const auto predicate=[&](const Record& record) {
            if(stride!=record.stride || address<record.begin) return false;
            const uint64_t delta=address-record.begin;
            return !(delta%stride) && delta/stride<record.count && count<=record.count-delta/stride;
        };
        const bool possible=!page_mode()||produced_pages.may_overlap(address,count*stride);
        if(page_mode()){++page_queries;if(!possible)++page_rejects;}
        if(!possible&&page_mode()==1){prune();return false;}
        if(query_candidates(address,count,stride,predicate,view)) {if(!possible)std::abort();return true;}
        prune();
        return false;
    }
};
} // namespace nx::nvn::replay
