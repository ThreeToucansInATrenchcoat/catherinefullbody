// perf 2026-11-22: replay-thread phase timers. CATHERINE_PERF_PHASES=1 enables them (default off: one
// predictable branch per scope). perf22::report(frames) logs microseconds per frame and calls per frame.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <intrin.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <x86intrin.h>
#include "C:/catherine/recomp/Source/platform/host_clock.h"
#endif

namespace perf22 {
enum Phase : int {
    ReplayList, Draw, DrawGather, DrawSets, DrawInputs, DrawPipeline, DrawBind, DrawPre,
    Dispatch, DispSkin, DispMorph, FrameSubmit, Present, DrawCommand, SkPalette, SkPrep, SkKernel, SkCommit, F89Match, F89Capture, DrawMid1, DrawMid2, PreLayout, PreFlush, PreHead, PreBlock, PreSnap, PreDebug, SkSerial, SkParallel, WriteBacks, PrAcquire, PrRecord, PrSubmit, PrQueue, MorphScan, BdSets, BdVertex, BdRaster, BdCmd, BdPost, DescMiss, DescAddr, BatchEnqueue, BatchMatrix, Geo9Compare, Geo12Compare, MatPrelude, MatSkinned, MatRoot9, MatGeneric, GenericGeometry, GenericHead, GenericTracker, GenericHistory, kPhases
};
inline const char* const kNames[kPhases] = {
    "replay_list", "draw", " gather", " sets", " inputs", " pipeline", " bind+cmd", " pre",
    "dispatch", " skin", " morph", "frame_submit", "present", "draw_command", "  sk_palette", "  sk_prep", "  sk_kernel", "  sk_commit", " f89_match", " f89_capture", " mid1", " mid2", "  pre_layout", "  pre_pending", "  pre_head", "  pre_block", "  pre_snap", "  pre_debug", "   sk_serial", "   sk_parallel", " write_backs", " pr_acquire", " pr_record", " pr_submit", " pr_queue", "  morph_scan", "  bd_sets", "  bd_vertex", "  bd_raster", "  bd_cmd", "  bd_post", "   desc_miss", "   desc_addr", " batch_enqueue", " batch_matrix", " geo9_compare", " geo12_compare", " mat_prelude", " mat_skinned", " mat_root9", " mat_generic", " generic_geometry", " generic_head", " generic_tracker", " generic_history"};

// Production builds compile the timers out; set this to 1 for a measurement build.
#ifndef CATHERINE_PERF_PHASES_BUILD
#define CATHERINE_PERF_PHASES_BUILD 0
#endif
inline bool on() {
    if constexpr (!CATHERINE_PERF_PHASES_BUILD) return false;
    static const bool v = [] { const char* e = std::getenv("CATHERINE_PERF_PHASES"); return e && e[0] == '1'; }();
    return v;
}
struct Acc { uint64_t ticks[kPhases]; uint64_t calls[kPhases]; };
inline thread_local Acc t_acc{};

struct Scope {
    Phase phase;
    uint64_t t0;
    explicit Scope(Phase p) : phase(p), t0(on() ? __rdtsc() : 0) {}
    ~Scope() {
        if (t0) { t_acc.ticks[phase] += __rdtsc() - t0; t_acc.calls[phase]++; }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

inline uint64_t start() { return on() ? __rdtsc() : 0; }
inline void add(Phase p, uint64_t t0) { if (t0) { t_acc.ticks[p] += __rdtsc() - t0; t_acc.calls[p]++; } }

inline double tsc_per_us() {
    static const double v = [] {
#ifdef _WIN32
        LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        const uint64_t t0 = __rdtsc();
        do QueryPerformanceCounter(&b); while (b.QuadPart - a.QuadPart < f.QuadPart / 50);
        return double(__rdtsc() - t0) / (double(b.QuadPart - a.QuadPart) * 1e6 / double(f.QuadPart));
#else
        const uint64_t start = catherine_host_nanoseconds();
        const uint64_t t0 = __rdtsc();
        uint64_t stop;
        do stop = catherine_host_nanoseconds(); while (stop - start < 20000000);
        return double(__rdtsc() - t0) * 1000.0 / double(stop - start);
#endif
    }();
    return v;
}

// CATHERINE_TRACE_SEQ=<frame>: log that frame's replay sequence (D draw, s skin, m morph, c other dispatch).
inline bool seq_on() { static const long long f = [] { const char* e = std::getenv("CATHERINE_TRACE_SEQ"); return e ? std::atoll(e) : 0ll; }(); return f > 0; }
inline thread_local std::string* t_seq = nullptr;
inline void seq(char k) { if (seq_on()) { if (!t_seq) t_seq = new std::string; t_seq->push_back(k); } }
inline void seq_frame() {
    if (!seq_on()) return;
    static thread_local long long n = 0;
    static const long long want = std::atoll(std::getenv("CATHERINE_TRACE_SEQ"));
    if (++n >= want && (n - want) % 1000 == 0 && t_seq) std::fprintf(stderr, "perf22 seq: %s%c", t_seq->c_str(), 10);
    if (t_seq) t_seq->clear();
}
// Call on the replay thread once per presented frame.
inline void frame(uint64_t every = 300) {
    if (!on()) return;
    static thread_local uint64_t frames = 0;
    if (++frames % every) return;
    const double k = tsc_per_us();
    char line[4096]; int n = std::snprintf(line, sizeof line, "perf22 phases (us/frame, calls/frame):");
    for (int i = 0; i < kPhases && n < int(sizeof line) - 64; ++i)
        n += std::snprintf(line + n, sizeof line - n, " %s=%.1f/%.0f", kNames[i], double(t_acc.ticks[i]) / k / double(every),
                           double(t_acc.calls[i]) / double(every));
    std::fprintf(stderr, "%s\n", line);
    t_acc = Acc{};
}
}  // namespace perf22
