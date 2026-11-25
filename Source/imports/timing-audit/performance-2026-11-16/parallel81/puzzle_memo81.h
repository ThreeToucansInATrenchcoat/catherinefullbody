// parallel81: per-frame memo for puzzle_model_transform81 (nvn_sets.cpp only; unique names, no ODR overlap).
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>

struct PuzzleKey81 {
    uint64_t frame, clock, mesh;
    std::array<uint64_t, 8> m;
    bool operator==(const PuzzleKey81& o) const {
        return frame == o.frame && clock == o.clock && mesh == o.mesh && m == o.m;
    }
};
struct PuzzleKeyHash81 {
    size_t operator()(const PuzzleKey81& k) const {
        uint64_t h = (k.mesh ^ k.clock * 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
        for (uint64_t w : k.m) h = (h ^ w) * 0xbf58476d1ce4e5b9ull, h ^= h >> 29;
        return size_t(h);
    }
};
struct PuzzleMemoEntry81 {
    uint32_t calls = 0;
    const uint8_t* result = nullptr;  // nullptr: the call's own input bytes
};
struct PuzzleMemo81 {
    std::unordered_map<PuzzleKey81, PuzzleMemoEntry81, PuzzleKeyHash81> map;
    uint64_t frame = ~0ull;
    uint64_t hits = 0, misses = 0;
};
inline PuzzleMemo81& puzzle_memo81() {
    static PuzzleMemo81 memo;
    return memo;
}
inline void puzzle_memo81_reset() {
    puzzle_memo81().map.clear();
    puzzle_memo81().frame = ~0ull;
}
