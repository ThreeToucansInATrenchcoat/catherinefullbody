// bridge84: decoded NVN state objects cached by their bytes (nvn_state.cpp only).
// The state binds decode a guest NVN state object by calling the real driver's getters (recompiled guest code, a full
// register save/restore per call; 2 calls per vertex attribute, 10 per depth-stencil state, ...). Those getters only
// read fields of the object, so the decoded value is a function of the object's bytes: the first decode of given
// bytes is kept and later binds of the same bytes reuse it. Per thread, so no locking. Direct mapped; the full key is
// compared, so a collision is only a miss.
// CATHERINE_PARALLEL84=0: off (decode every time, as before). =2: verify: decode every time AND compare with the
// cache; mismatches are counted and logged (they would mean a state object is larger than the bytes keyed).
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace nx::nvn::bridge84 {

inline int mode() {
    static const int m = [] {
        const char* e = std::getenv("CATHERINE_PARALLEL84");
        return e && e[0] == '0' ? 0 : e && e[0] == '2' ? 2 : 1;
    }();
    return m;
}

struct Stats {
    std::atomic<uint64_t> hits{0}, misses{0}, verified{0}, mismatches{0};
};
inline Stats g_stats;
inline std::atomic<uint64_t> g_type_mismatch[8], g_type_verified[8];

template <size_t MaxKey, class Value, size_t Slots = 1024>
struct Cache {
    struct Entry {
        bool used = false;
        uint32_t len = 0;
        uint8_t key[MaxKey];
        Value value;
    };
    Entry table[Slots];
    // Only picks the slot (find() compares the whole key), so any mixing is exact; 8 bytes per step (bridge94: the
    // byte-at-a-time FNV was ~128 dependent multiplies per vertex-attrib bind on the game thread).
    static uint64_t hash(const uint8_t* p, uint32_t n) {
        uint64_t h = 0x9e3779b97f4a7c15ull ^ n;
        uint32_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t w;
            std::memcpy(&w, p + i, 8);
            h = (h ^ w) * 0xbf58476d1ce4e5b9ull;
            h ^= h >> 31;
        }
        for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
        return h ^ (h >> 29);
    }
    const Value* find(const uint8_t* key, uint32_t n) const {
        const Entry& e = table[hash(key, n) & (Slots - 1)];
        return e.used && e.len == n && std::memcmp(e.key, key, n) == 0 ? &e.value : nullptr;
    }
    void put(const uint8_t* key, uint32_t n, const Value& v) {
        Entry& e = table[hash(key, n) & (Slots - 1)];
        e.used = true, e.len = n;
        std::memcpy(e.key, key, n);
        e.value = v;
    }
};

// Decodes through `decode` unless cached; in verify mode always decodes and compares.
template <class Value, size_t MaxKey, class Decode>
Value cached(Cache<MaxKey, Value>& cache, const uint8_t* key, uint32_t n, Decode&& decode, int type = 0) {
    const int m = mode();
    if (m == 0 || n > MaxKey || !key) return decode();  // a null object: decoded as before, never read here
    if (const Value* hit = cache.find(key, n)) {
        if (m != 2) {
            g_stats.hits.fetch_add(1, std::memory_order_relaxed);
            return *hit;
        }
        const Value fresh = decode();
        g_stats.verified.fetch_add(1, std::memory_order_relaxed);
        g_type_verified[type & 7].fetch_add(1, std::memory_order_relaxed);
        if (std::memcmp(&fresh, hit, sizeof(Value)) != 0) {
            g_stats.mismatches.fetch_add(1, std::memory_order_relaxed);
            g_type_mismatch[type & 7].fetch_add(1, std::memory_order_relaxed);
            cache.put(key, n, fresh);
        }
        return fresh;
    }
    g_stats.misses.fetch_add(1, std::memory_order_relaxed);
    const Value fresh = decode();
    cache.put(key, n, fresh);
    return fresh;
}

}  // namespace nx::nvn::bridge84
