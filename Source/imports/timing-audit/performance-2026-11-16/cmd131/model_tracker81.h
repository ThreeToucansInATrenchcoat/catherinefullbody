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
        uint64_t h = k.mesh * 0x9e3779b97f4a7c15ull;
        for (uint64_t w : k.m) h = (h ^ w) * 0xbf58476d1ce4e5b9ull, h ^= h >> 29;
        return size_t(h);
    }
};


// cmd131: flat same-frame index (see build131.py).
class FlatExact131 {
    struct Slot { uint32_t gen = 0; ModelKey81 key; uint64_t id = 0; };
    static constexpr size_t kSlots = 8192, kProbe = 32;
    std::vector<Slot> slots = std::vector<Slot>(kSlots);
    uint32_t gen = 1;
    bool spilled = false;
    std::unordered_map<ModelKey81, uint64_t, ModelKeyHash81> spill;
public:
    void clear() {
        if (++gen == 0) { for (Slot& s : slots) s.gen = 0; gen = 1; }
        if (spilled) spill.clear(), spilled = false;
    }
    const uint64_t* find(const ModelKey81& k) const {
        size_t at = ModelKeyHash81{}(k) & (kSlots - 1);
        for (size_t n = 0; n < kProbe; ++n, at = (at + 1) & (kSlots - 1)) {
            const Slot& s = slots[at];
            if (s.gen != gen) break;  // an empty slot ends the probe (entries are never removed between clears)
            if (s.key == k) return &s.id;
        }
        if (spilled) { auto it = spill.find(k); if (it != spill.end()) return &it->second; }
        return nullptr;
    }
    void emplace(const ModelKey81& k, uint64_t id) {
        if (find(k)) return;  // insert if absent, as unordered_map::emplace
        size_t at = ModelKeyHash81{}(k) & (kSlots - 1);
        for (size_t n = 0; n < kProbe; ++n, at = (at + 1) & (kSlots - 1)) {
            Slot& s = slots[at];
            if (s.gen != gen) { s.gen = gen, s.key = k, s.id = id; return; }
        }
        spill.emplace(k, id), spilled = true;
    }
};

class ModelInstanceTracker81 {
    struct Basis {
        std::array<float, 9> unit{};
        std::array<double, 3> scale{};
        bool valid = false, mirrored = false;
    };
    static Basis basis_of(const float* m) {
        Basis b;
        for (unsigned c = 0; c < 3; ++c) {
            double squared = 0;
            for (unsigned r = 0; r < 3; ++r) squared += double(m[4*r+c]) * m[4*r+c];
            b.scale[c] = std::sqrt(squared);
            if (!std::isfinite(b.scale[c]) || b.scale[c] <= 1e-20) return b;
            for (unsigned r = 0; r < 3; ++r) b.unit[3*r+c] = float(m[4*r+c] / b.scale[c]);
        }
        // The relative-rotation trace formula requires orthogonal bases.
        // An exact full matrix still matches even if this motion test is unsafe.
        for (unsigned a = 0; a < 3; ++a)
            for (unsigned c = a + 1; c < 3; ++c) {
                float dot = 0;
                for (unsigned r = 0; r < 3; ++r) dot += b.unit[3*r+a] * b.unit[3*r+c];
                if (std::fabs(dot) > .001f) return b;
            }
        const auto& u = b.unit;
        const float determinant = u[0]*(u[4]*u[8]-u[5]*u[7]) -
                                  u[1]*(u[3]*u[8]-u[5]*u[6]) +
                                  u[2]*(u[3]*u[7]-u[4]*u[6]);
        if (!std::isfinite(determinant) || std::fabs(determinant) < .99f) return b;
        b.mirrored = determinant < 0;
        b.valid = true;
        return b;
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
    };
    std::unordered_map<uint64_t, std::vector<Instance>> meshes;
    uint64_t next_id = 1, last_pruned = 0;
    FlatExact131 exact;
    std::unordered_set<uint64_t> shared_basis_meshes;
    uint64_t exact_frame = ~0ull;
    bool exact_complete = true;  // cmd98: every current-frame entry is in `exact`
    static bool on98() {
        static const bool on = [] { const char* e = std::getenv("CATHERINE_PARALLEL98"); return !(e && e[0] == '0'); }();
        return on;
    }

public:
    uint64_t index_hits = 0, scans = 0;
    void clear() { meshes.clear(), exact.clear(), shared_basis_meshes.clear(), exact_complete = true; }
    bool ambiguous(uint64_t mesh) const { return shared_basis_meshes.contains(mesh); }
    uint64_t resolve(uint64_t mesh, const float *matrix, uint64_t frame) {
        if (frame > last_pruned + 64) {
            last_pruned = frame;
            for (auto it = meshes.begin(); it != meshes.end();) {
                auto &entries = it->second;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const Instance &i) {
                                                 return frame > i.frame &&
                                                        frame - i.frame > 80;
                                             }),
                              entries.end());
                if (entries.empty())
                    it = meshes.erase(it);
                else
                    ++it;
            }
        }
        if (frame != exact_frame) exact.clear(), exact_frame = frame, exact_complete = true;
        ModelKey81 key{mesh, {}};
        std::memcpy(key.m.data(), matrix, 64);
        const bool fast98 = on98();
        if (!fast98) (void)meshes[mesh];  // original order: the mesh entry first
        if (const uint64_t* hit = exact.find(key)) {
            ++index_hits;
            return *hit;
        }
        auto &instances = meshes[mesh];
        ++scans;
        if (!fast98 || !exact_complete)
            for (const auto &i : instances)
                if (i.frame == frame && std::memcmp(i.matrix.data(), matrix, 64) == 0) {
                    exact.emplace(key, i.id);
                    return i.id;
                }
        Instance *match = nullptr;
        double closest = 75.0 * 75.0;
        const Basis current_basis = basis_of(matrix);
        // Two differently oriented copies in one presentation are distinct
        // instances, but this generic fallback has no native instance owner.
        // Never let the renderer infer their animation history from proximity.
        for (const auto& i : instances) if (i.frame == frame) {
            bool different = false;
            for (unsigned row = 0; row < 3; ++row)
                for (unsigned col = 0; col < 3; ++col)
                    different |= std::fabs(i.matrix[row*4+col]-matrix[row*4+col]) > 1e-5f;
            if (different) { shared_basis_meshes.insert(mesh); break; }
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
            match->basis = current_basis;
            match->frame = frame;
            exact.emplace(key, match->id);
            return match->id;
        }
        if (instances.size() > 4096) {
            instances.clear();
            exact.clear();
            exact_complete = false;  // other meshes keep current-frame entries the index lost
        }
        instances.erase(std::remove_if(instances.begin(), instances.end(),
                                       [&](const Instance &i) {
                                           return frame > i.frame &&
                                                  frame - i.frame > 80;
                                       }),
                        instances.end());
        Instance fresh{next_id++, frame, {}};
        std::memcpy(fresh.matrix.data(), matrix, 64);
        fresh.basis = current_basis;
        instances.push_back(fresh);
        exact.emplace(key, fresh.id);
        return fresh.id;
    }
};
