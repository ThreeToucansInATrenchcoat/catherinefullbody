#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

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
    struct Record {
        uintptr_t begin = 0;
        uint64_t count = 0, stride = 0, submit = 0, used = 0;
        bool pending = true;
        std::vector<uint8_t> xyz;
        const uint8_t* writeback_source = nullptr;
        bool invalid = false;
    };
    std::vector<Record> records;
    uint64_t submit = 0, serial = 0;

    static bool valid(const uint8_t* p, uint64_t count, uint64_t stride) {
        return p && count && stride >= 12 && count <= UINT64_MAX / stride &&
               count * stride <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(p);
    }
    static Record slice(const Record& record, uint64_t first, uint64_t count) {
        Record result = record;
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
        for (uint64_t i = 0; i < record.count; ++i)
            std::memcpy(record.xyz.data() + i * 12, bytes + i * record.stride, 12);
    }
    static bool matches(const Record& record, const uint8_t* bytes) {
        if (!bytes || record.xyz.size() != record.count * 12) return false;
        for (uint64_t i = 0; i < record.count; ++i)
            if (std::memcmp(record.xyz.data() + i * 12, bytes + i * record.stride, 12)) return false;
        return true;
    }
    template<class View> bool eligible(Record& record, View&& view) {
        if (record.invalid) return false;
        // This submit's GPU write is known to produce world positions, but its
        // bytes cannot be read until the fence. Do not guess older GPU results.
        if (record.pending && record.submit != submit) return false;
        const auto* bytes = view(reinterpret_cast<const uint8_t*>(record.begin), record.count * record.stride);
        if (record.pending && record.xyz.empty()) return true;
        if (!matches(record, bytes)) {
            if (bytes) record.invalid = true;
            return false;
        }
        record.used = ++serial;
        return true;
    }
    void prune() {
        std::erase_if(records, [](const Record& record) { return record.invalid; });
        size_t bytes = 0;
        for (const Record& record : records) bytes += record.xyz.size();
        while (records.size() > 1024 || bytes > (32u << 20)) {
            auto oldest = records.end();
            for (auto it = records.begin(); it != records.end(); ++it)
                if (!it->pending && (oldest == records.end() || it->used < oldest->used)) oldest = it;
            if (oldest == records.end()) break; // In-flight snapshots belong to their fenced write-backs.
            bytes -= oldest->xyz.size();
            records.erase(oldest);
        }
    }

public:
    void begin_submit() { ++submit; }
    void clear() { records.clear(); ++submit; }
    void overwrite(const uint8_t* p, uint64_t size) {
        if (!p || !size || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(p)) return;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(p), end = begin + uintptr_t(size);
        std::vector<Record> kept;
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
    }
    void produced(const uint8_t* p, uint64_t count, uint64_t stride, const uint8_t* cpu_bytes) {
        if (!valid(p, count, stride)) return;
        overwrite(p, count * stride);
        Record record{reinterpret_cast<uintptr_t>(p), count, stride, submit, ++serial, true, {}};
        if (cpu_bytes) capture(record, cpu_bytes);
        records.push_back(std::move(record));
        prune();
    }
    void seal_write_back(const uint8_t* from, const uint8_t* to, uint64_t size) {
        if (!from || !to || !size || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(to)) return;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(to), end = begin + uintptr_t(size);
        for (auto& record : records)
            if (record.pending && record.submit == submit && record.begin >= begin &&
                record.begin + record.count * record.stride <= end)
                record.writeback_source = from + (record.begin - begin);
    }
    // Called after apply_write_back, when from is host-ready/fenced and guest
    // positions have actually been updated. Guest races that prevent an exact
    // match do not certify an unrelated/reused allocation.
    void write_back(const uint8_t* from, const uint8_t* to, uint64_t size) {
        if (!from || !to || !size || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(to)) return;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(to), end = begin + uintptr_t(size);
        for (auto it = records.begin(); it != records.end();) {
            if (!it->pending || it->begin < begin || it->begin + it->count * it->stride > end ||
                it->writeback_source != from + (it->begin - begin)) { ++it; continue; }
            const uint64_t offset = it->begin - begin;
            capture(*it, from + offset);
            if (!matches(*it, to + offset)) { it = records.erase(it); continue; }
            it->pending = false;
            it->used = ++serial;
            ++it;
        }
        prune();
    }
    template<class View> bool contains(const uint8_t* p, uint64_t size, View&& view) {
        if (!p || !size || size > 12) return false;
        const uintptr_t address = reinterpret_cast<uintptr_t>(p);
        for (auto& record : records) {
            if (address < record.begin) continue;
            const uint64_t delta = address - record.begin;
            if (delta / record.stride >= record.count || delta % record.stride + size > 12) continue;
            if (eligible(record, view)) return true;
        }
        prune();
        return false;
    }
    template<class View> bool positions(const uint8_t* p, uint64_t count, uint64_t stride, View&& view) {
        if (!valid(p, count, stride)) return false;
        const uintptr_t address = reinterpret_cast<uintptr_t>(p);
        for (auto& record : records) {
            if (stride != record.stride || address < record.begin) continue;
            const uint64_t delta = address - record.begin;
            if (delta % stride || delta / stride >= record.count || count > record.count - delta / stride) continue;
            if (eligible(record, view)) return true;
        }
        prune();
        return false;
    }
};
} // namespace nx::nvn::replay
