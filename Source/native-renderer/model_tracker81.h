// Restored the affine-aware statue matching repair; retained the flat exact index.
// parallel81: catherine::visual::ModelInstanceTracker with an exact same-frame index. Included only by nvn_sets.cpp,
// inside an anonymous namespace (no ODR overlap with the original class, which nvn_written.cpp also uses).
//
// The affine-aware matching below retains the statue repair. Its first loop (the first same-frame entry of this mesh
// with byte-identical matrix) is answered from `exact` when possible. Why that is exact:
//  * within one frame, at most one entry of a mesh has (frame == current, matrix == m): a second could only be made
//    by the distance or fresh paths, which run only when the first loop found none;
//  * an entry with frame == current never changes matrix or frame again within that frame (the distance path skips
//    entries with frame >= current), and the only removals of current-frame entries are the >4096 clear and clear(),
//    which both empty the index;
//  * so every index entry names exactly the entry the first loop would return. A miss falls back to the original
//    loop, so the index never has to be complete. It is emptied whenever the frame changes.
#pragma once
#include <algorithm>
#include <bit>
#include <immintrin.h>
#include <cstdio>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ModelKey81 {
    uint64_t mesh;
    std::array<uint64_t, 8> m;
    bool operator==(const ModelKey81& o) const { return mesh == o.mesh && m == o.m; }
};
struct ModelKeyHash81 {
    size_t operator()(const ModelKey81& k) const {
        static const bool parallel_hash=[] {const auto* e=std::getenv("CATHERINE_TRACKER_HASH");return !e||std::strcmp(e,"0");}();
        uint64_t h;
        if(parallel_hash) {
            const auto& w=k.m;
            const uint64_t a=(w[0]^std::rotl(w[1],23))*0x9e3779b185ebca87ull;
            const uint64_t b=(w[2]^std::rotl(w[3],29))*0xc2b2ae3d27d4eb4full;
            const uint64_t c=(w[4]^std::rotl(w[5],31))*0x165667b19e3779f9ull;
            const uint64_t d=(w[6]^std::rotl(w[7],37))*0x85ebca77c2b2ae63ull;
            h=k.mesh*0x9e3779b97f4a7c15ull^a^std::rotl(b,17)^std::rotl(c,39)^std::rotl(d,53);
            h^=h>>30;h*=0xbf58476d1ce4e5b9ull;h^=h>>27;h*=0x94d049bb133111ebull;h^=h>>31;
        } else {
            h=k.mesh*0x9e3779b97f4a7c15ull;
            for(uint64_t w:k.m)h=(h^w)*0xbf58476d1ce4e5b9ull,h^=h>>29;
        }
        return size_t(h);
    }
};


// cmd131: flat same-frame index (see build131.py).
class FlatExact131 {
    struct Slot {uint32_t gen=0,index=0;uint64_t hash=0;};
    struct Entry {ModelKey81 key;uint64_t id;};
    static constexpr size_t kSlots=8192,kProbe=32;
    std::vector<Slot> slots=std::vector<Slot>(kSlots);
    std::vector<Entry> entries;
    uint32_t gen=1;
    bool spilled=false;
    std::unordered_map<ModelKey81,uint64_t,ModelKeyHash81> spill;
public:
    FlatExact131(){entries.reserve(kSlots);}
    void clear() {
        if(++gen==0){for(auto& s:slots)s.gen=0;gen=1;}
        entries.clear();
        if(spilled)spill.clear(),spilled=false;
    }
    const uint64_t* find(const ModelKey81& k,uint64_t hash) const {
        size_t at=size_t(hash)&(kSlots-1);
        for(size_t n=0;n<kProbe;++n,at=(at+1)&(kSlots-1)) {
            const auto& s=slots[at];
            if(s.gen!=gen)break;
            if(s.hash==hash&&entries[s.index].key==k)return &entries[s.index].id;
        }
        if(spilled){auto it=spill.find(k);if(it!=spill.end())return &it->second;}
        return nullptr;
    }
    const uint64_t* find(const ModelKey81& k) const{return find(k,ModelKeyHash81{}(k));}
    void emplace(const ModelKey81& k,uint64_t id,uint64_t hash) {
        size_t at=size_t(hash)&(kSlots-1);
        for(size_t n=0;n<kProbe;++n,at=(at+1)&(kSlots-1)) {
            auto& s=slots[at];
            if(s.gen!=gen) {
                const uint32_t index=uint32_t(entries.size());
                entries.push_back({k,id});s={gen,index,hash};return;
            }
            if(s.hash==hash&&entries[s.index].key==k)return;
        }
        spill.emplace(k,id),spilled=true;
    }
    void emplace(const ModelKey81& k,uint64_t id){emplace(k,id,ModelKeyHash81{}(k));}
};

class ModelInstanceTracker81 {
    struct Basis {
        std::array<float, 9> unit{};
        std::array<double, 3> scale{};
        bool valid = false, mirrored = false;
        uint32_t mxcsr_after=0;
    };
    static Basis basis_of(const float* m) {
#pragma STDC FENV_ACCESS ON
        Basis b;
        auto finish=[&] {b.mxcsr_after=_mm_getcsr();return b;};
        for (unsigned c = 0; c < 3; ++c) {
            double squared = 0;
            for (unsigned r = 0; r < 3; ++r) squared += double(m[4*r+c]) * m[4*r+c];
            b.scale[c] = std::sqrt(squared);
            if (!std::isfinite(b.scale[c]) || b.scale[c] <= 1e-20) return finish();
            for (unsigned r = 0; r < 3; ++r) b.unit[3*r+c] = float(m[4*r+c] / b.scale[c]);
        }
        // The relative-rotation trace formula requires orthogonal bases.
        // An exact full matrix still matches even if this motion test is unsafe.
        for (unsigned a = 0; a < 3; ++a)
            for (unsigned c = a + 1; c < 3; ++c) {
                float dot = 0;
                for (unsigned r = 0; r < 3; ++r) dot += b.unit[3*r+a] * b.unit[3*r+c];
                if (std::fabs(dot) > .001f) return finish();
            }
        const auto& u = b.unit;
        const float determinant = u[0]*(u[4]*u[8]-u[5]*u[7]) -
                                  u[1]*(u[3]*u[8]-u[5]*u[6]) +
                                  u[2]*(u[3]*u[7]-u[4]*u[6]);
        if (!std::isfinite(determinant) || std::fabs(determinant) < .99f) return finish();
        b.mirrored = determinant < 0;
        b.valid = true;
        return finish();
    }
    static bool affine_distance(const Basis& previous, const Basis& current,
                                float translation, double& distance) {
        if (!previous.valid || !current.valid || previous.mirrored != current.mirrored) return false;
        double difference = 0, trace = 0;
        for (unsigned c = 0; c < 3; ++c) {
            const double ratio = current.scale[c] / previous.scale[c];
            if (ratio < .9 - 1e-6 || ratio > 1.1 + 1e-6) return false;
            difference += (ratio - 1) * (ratio - 1);
        }
        for (unsigned i = 0; i < 9; ++i) {
            trace += double(previous.unit[i]) * current.unit[i];
            const double d = double(previous.unit[i]) - current.unit[i];
            difference += d * d;
        }
        // trace(Rprevious^T*Rcurrent) = 1 + 2*cos(angle). The explicit
        // 45-degree gate rejects large copy/teleport changes at every scale.
        if (trace < 2.414213562373095 - 1e-6) return false;
        distance = double(translation) + 625.0 * difference;
        return true;
    }
    struct Instance {
        uint64_t id, frame;
        std::array<float, 16> matrix;
        Basis basis;
        uint64_t key_hash=0; // mesh plus exact stored matrix bytes
    };
    struct BasisSummary {
        uint64_t frame=~0ull;
        bool finite=true,any=false;
        std::array<float,9> low{},high{};
        void add(const float* matrix) {
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
                const unsigned n=r*3+c;const float value=matrix[r*4+c];
                if(!std::isfinite(value)){finite=false;continue;}
                if(!any)low[n]=high[n]=value;
                else {low[n]=std::min(low[n],value);high[n]=std::max(high[n],value);}
            }
            any=true;
        }
        bool differs(const float* matrix) const {
            if(!any)return false;
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
                const unsigned n=r*3+c;const float v=matrix[r*4+c];
                if(std::fabs(low[n]-v)>1e-5f||std::fabs(high[n]-v)>1e-5f)return true;
            }
            return false;
        }
    };

    static int summary_mode() {static const int value=[] {const auto* e=std::getenv("CATHERINE_TRACKER_SUMMARY");return e?std::atoi(e):1;}();return value;}
    struct MeshInstances {
        std::vector<Instance> values;BasisSummary summary;
        uint64_t cursor_frame=~0ull;size_t cursor=0;
        Basis cached_basis;std::array<uint32_t,9> cached_columns{};bool basis_cached=false;
    };
    Basis basis_for(MeshInstances& history,const float* matrix) {
        static const int mode=[] {const auto* e=std::getenv("CATHERINE_TRACKER_BASIS_CACHE");return e?std::atoi(e):1;}();
        const unsigned now=mode?_mm_getcsr():0;
        const auto& saved=history.cached_basis;
        // The saved status includes every flag raised by this same basis math.
        // If those flags already exist now, recomputation cannot change status.
        bool match=mode&&history.basis_cached&&saved.valid&&
            (now&0x1f80u)==0x1f80u&&((now^saved.mxcsr_after)&0xffc0u)==0&&
            (saved.mxcsr_after&0x3fu&~now)==0;
        if(match)for(unsigned row=0;row<3;++row)
            match&=std::memcmp(history.cached_columns.data()+row*3,matrix+row*4,12)==0;
        if(match){
            ++basis_hits;
            if(mode==2){
                const auto reference=basis_of(matrix);const auto after=_mm_getcsr();
                if(after!=now||reference.valid!=saved.valid||reference.mirrored!=saved.mirrored||
                   std::memcmp(reference.unit.data(),saved.unit.data(),sizeof saved.unit)||
                   std::memcmp(reference.scale.data(),saved.scale.data(),sizeof saved.scale)) {
                    std::fprintf(stderr,"Tracker basis cache mismatch CSR %x/%x\n",now,after);std::abort();
                }
                ++basis_compared;
            }
            return saved;
        }
        auto computed=basis_of(matrix);
        if(mode&&computed.valid){
            history.cached_basis=computed;history.basis_cached=true;
            for(unsigned row=0;row<3;++row)std::memcpy(history.cached_columns.data()+row*3,matrix+row*4,12);
        }
        return computed;
    }
    std::unordered_map<uint64_t,MeshInstances> meshes;
    uint64_t next_id = 1, last_pruned = 0;
    FlatExact131 exact;
    FlatExact131 previous_exact; // earliest eligible vector index for each exact prior-frame matrix
    uint64_t previous_frame = ~0ull;
    bool previous_index_built=false;
    void index_previous(uint64_t frame) {
        previous_exact.clear();
        for (const auto& [mesh,history] : meshes)
            for (size_t n=0;n<history.values.size();++n) {
                const auto& i=history.values[n];
                if (i.frame>=frame || frame-i.frame>2) continue;
                ModelKey81 key{mesh,{}};
                std::memcpy(key.m.data(),i.matrix.data(),64);
                static const int cached=[] {const auto*e=std::getenv("CATHERINE_TRACKER_INSTANCE_HASH");return e?std::atoi(e):1;}();
                if(cached==2&&i.key_hash!=ModelKeyHash81{}(key))std::abort();
                if(cached)previous_exact.emplace(key,uint64_t(n),i.key_hash);
                else previous_exact.emplace(key,uint64_t(n));
            }
        previous_frame=frame;previous_index_built=true;
    }
    std::unordered_set<uint64_t> shared_basis_meshes;
    uint64_t exact_frame = ~0ull;
    bool exact_complete = true;  // cmd98: every current-frame entry is in `exact`
    static bool on98() {
        static const bool on = [] { const char* e = std::getenv("CATHERINE_PARALLEL98"); return !(e && e[0] == '0'); }();
        return on;
    }

public:
    uint64_t index_hits = 0, scans = 0, basis_hits=0, basis_compared=0;
    void clear() { meshes.clear(), exact.clear(), previous_exact.clear(), shared_basis_meshes.clear(), exact_complete = true; previous_frame=~0ull; previous_index_built=false; }
    bool ambiguous(uint64_t mesh) const { return shared_basis_meshes.contains(mesh); }
    uint64_t resolve(uint64_t mesh, const float *matrix, uint64_t frame) {
        if (frame > last_pruned + 64) {
            last_pruned = frame;
            for (auto it = meshes.begin(); it != meshes.end();) {
                auto &entries = it->second.values;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const Instance &i) {
                                                 return frame > i.frame &&
                                                        frame - i.frame > 80;
                                             }),
                              entries.end());
                if (entries.empty()) {
                    it = meshes.erase(it);
                }
                else
                    ++it;
            }
        }
        if(frame!=previous_frame){previous_exact.clear();previous_frame=frame;previous_index_built=false;}
        if (frame != exact_frame) exact.clear(), exact_frame = frame, exact_complete = true;
        ModelKey81 key{mesh, {}};
        std::memcpy(key.m.data(), matrix, 64);
        const uint64_t key_hash=ModelKeyHash81{}(key);
        const bool fast98 = on98();
        if (!fast98) (void)meshes[mesh];  // original order: the mesh entry first
        if (const uint64_t* hit = exact.find(key,key_hash)) {
            ++index_hits;
            return *hit;
        }
        auto& history=meshes[mesh];
        auto &instances=history.values;
        ++scans;
        if (!fast98 || !exact_complete)
            for (const auto &i : instances)
                if (i.frame == frame && std::memcmp(i.matrix.data(), matrix, 64) == 0) {
                    exact.emplace(key, i.id,key_hash);
                    return i.id;
                }
        Instance *match = nullptr;
        double closest = 75.0 * 75.0;
        const Basis current_basis = basis_for(history,matrix);
        // Two differently oriented copies in one presentation are distinct
        // instances, but this generic fallback has no native instance owner.
        // Never let the renderer infer their animation history from proximity.
        BasisSummary* summary=nullptr;
        if (!shared_basis_meshes.contains(mesh)) {
            auto original_differs=[&] {
                for(const auto& i:instances)if(i.frame==frame) {
                    bool different=false;
                    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                        different|=std::fabs(i.matrix[r*4+c]-matrix[r*4+c])>1e-5f;
                    if(different)return true;
                }
                return false;
            };
            bool different;
            if(summary_mode()) {
                summary=&history.summary;
                if(summary->frame!=frame) {
                    *summary=BasisSummary{};summary->frame=frame;
                    for(const auto& i:instances)if(i.frame==frame)summary->add(i.matrix.data());
                }
                bool finite=summary->finite;
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)finite&=std::isfinite(matrix[r*4+c]);
                different=finite?summary->differs(matrix):original_differs();
                if(summary_mode()==2&&different!=original_differs())std::abort();
            } else different=original_differs();
            if(different){shared_basis_meshes.insert(mesh);summary=nullptr;}
        }
        // Try only the earliest still-eligible vector entry. A byte-exact hit
        // is necessarily the same first exact match as the ordered search.
        // Other orders/motion retain the complete prior-index/affine path.
        static const bool cursor_enabled=[] {const auto* e=std::getenv("CATHERINE_TRACKER_CURSOR");return !e||std::strcmp(e,"0");}();
        if(cursor_enabled){
            if(history.cursor_frame!=frame){history.cursor_frame=frame;history.cursor=0;}
            while(history.cursor<instances.size()){
                const auto& i=instances[history.cursor];
                if(i.frame<frame&&frame-i.frame<=2)break;
                ++history.cursor;
            }
            if(history.cursor<instances.size()){
                auto& i=instances[history.cursor];
                if(std::memcmp(i.matrix.data(),matrix,64)==0){
                    i.basis=current_basis;i.frame=frame;++history.cursor;
                    if(summary)summary->add(matrix);
                    exact.emplace(key,i.id,key_hash);return i.id;
                }
            }
        }
        if(!previous_index_built&&instances.size()>8)index_previous(frame);
        if (const uint64_t* index=previous_exact.find(key,key_hash); index && *index<instances.size()) {
            auto& i=instances[size_t(*index)];
            // A prior entry may have been consumed by another instance this frame.
            // Validate it and use the original ordered search if it changed.
            if (i.frame<frame && frame-i.frame<=2 && std::memcmp(i.matrix.data(),matrix,64)==0) {
                i.basis=current_basis;
                i.frame=frame;
                if(summary)summary->add(matrix);
                exact.emplace(key,i.id,key_hash);
                return i.id;
            }
        }
        for (auto &i : instances) {
            if (i.frame >= frame || frame - i.frame > 2)
                continue;
            float distance = 0;
            for (unsigned c : {3u, 7u, 11u}) {
                float d = matrix[c] - i.matrix[c];
                distance += d * d;
            }
            if (distance == 0.f && std::memcmp(i.matrix.data(), matrix, 64) == 0) {
                match = &i;
                break;
            }
            if (!(distance < closest)) continue;
            double metric;
            if (affine_distance(i.basis, current_basis, distance, metric) && metric < closest) {
                closest = metric;
                match = &i;
            }
        }
        if (match) {
            std::memcpy(match->matrix.data(), matrix, 64);
            match->basis = current_basis;match->key_hash=key_hash;
            match->frame = frame;
            if(summary)summary->add(matrix);
            exact.emplace(key, match->id,key_hash);
            return match->id;
        }
        if (instances.size() > 4096) {
            instances.clear();history.cursor_frame=~0ull;history.cursor=0;
            if(summary){*summary=BasisSummary{};summary->frame=frame;}
            previous_exact.clear();
            exact.clear();
            exact_complete = false;  // other meshes keep current-frame entries the index lost
        }
        const size_t previous_size=instances.size();
        instances.erase(std::remove_if(instances.begin(), instances.end(),
                                       [&](const Instance &i) {
                                           return frame > i.frame &&
                                                  frame - i.frame > 80;
                                       }),
                        instances.end());
        // Erasure changes vector indices; an incomplete index simply falls back
        // to the original exact/affine search for the rest of this presentation.
        if(instances.size()!=previous_size){previous_exact.clear();history.cursor_frame=~0ull;history.cursor=0;}
        Instance fresh{next_id++, frame, {}};
        std::memcpy(fresh.matrix.data(), matrix, 64);
        fresh.basis = current_basis;fresh.key_hash=key_hash;
        instances.push_back(fresh);
        if(summary)summary->add(matrix);
        exact.emplace(key, fresh.id,key_hash);
        return fresh.id;
    }
};
