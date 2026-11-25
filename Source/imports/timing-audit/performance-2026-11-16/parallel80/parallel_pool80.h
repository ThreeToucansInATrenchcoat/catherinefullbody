// parallel80: a small helper pool for the replay thread's bulk loops (CPU skin kernel, write-back diff).
// Only the replay thread submits work, one job at a time, and it takes part in the job itself, so a job never
// waits on a sleeping helper to start. Chunks are claimed from the job's own counter (P-cores and E-cores mix on
// this CPU), and run() returns only when every chunk has finished AND no helper still holds the job.
// A job lives on the caller's stack and is published through one atomic pointer. A helper registers in users_
// before loading the pointer; the caller clears the pointer and then waits for users_ to drain. With seq_cst on
// both sides, a helper that registers after that drain loads the cleared (or a newer) pointer, never a dead job.
// Helpers spin briefly after each job (a submit's skin dispatches arrive close together), then sleep on
// WaitOnAddress. CATHERINE_PARALLEL80=0: no helpers; every loop runs exactly as before on the replay thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <immintrin.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace nx::nvn::parallel80 {

struct Stats {
    std::atomic<uint64_t> jobs{0}, chunks{0}, helper_chunks{0}, serial_jobs{0};
};
inline Stats g_stats;

class Pool {
public:
    using Fn = void (*)(void* ctx, uint32_t chunk);

    static Pool& get() {
        static Pool pool;
        return pool;
    }
    bool on() const { return helpers_ > 0; }

    // Runs fn(ctx, k) for every k in [0, chunks) on the caller and the helpers; returns when all are done.
    void run(uint32_t chunks, Fn fn, void* ctx) {
        join();  // perf 2026-11-22: an asynchronous job (run_async) always finishes first
        g_stats.jobs.fetch_add(1, std::memory_order_relaxed);
        g_stats.chunks.fetch_add(chunks, std::memory_order_relaxed);
        if (!helpers_ || chunks < 2) {
            g_stats.serial_jobs.fetch_add(1, std::memory_order_relaxed);
            for (uint32_t k = 0; k < chunks; ++k) fn(ctx, k);
            return;
        }
        Job job{fn, ctx, chunks};
        active_.store(&job, std::memory_order_seq_cst);
        generation_.fetch_add(1, std::memory_order_seq_cst);
        WakeByAddressAll(&generation_);
        work(job, false);
        while (job.done.load(std::memory_order_acquire) != chunks) _mm_pause();
        active_.store(nullptr, std::memory_order_seq_cst);
        while (users_.load(std::memory_order_seq_cst) != 0) _mm_pause();
    }

    // perf 2026-11-22: one asynchronous job at a time, started and joined by the replay thread only. The helpers
    // run every chunk; the caller continues and later joins (taking part in whatever is left). complete() may be
    // polled from any thread: true once every chunk has finished (the job's results are written).
    void run_async(uint32_t chunks, Fn fn, void* ctx) {
        join();
        if (!helpers_ || chunks < 1) {
            for (uint32_t k = 0; k < chunks; ++k) fn(ctx, k);
            return;
        }
        g_stats.jobs.fetch_add(1, std::memory_order_relaxed);
        g_stats.chunks.fetch_add(chunks, std::memory_order_relaxed);
        async_.fn = fn, async_.ctx = ctx, async_.total = chunks;
        async_.next.store(0, std::memory_order_relaxed);
        async_.done.store(0, std::memory_order_relaxed);
        async_pending_.store(true, std::memory_order_seq_cst);
        active_.store(&async_, std::memory_order_seq_cst);
        generation_.fetch_add(1, std::memory_order_seq_cst);
        WakeByAddressAll(&generation_);
    }
    bool complete() const {
        return !async_pending_.load(std::memory_order_acquire) ||
               async_.done.load(std::memory_order_acquire) == async_.total;
    }
    void join() {
        if (!async_pending_.load(std::memory_order_relaxed)) return;
        work(async_, false);
        while (async_.done.load(std::memory_order_acquire) != async_.total) _mm_pause();
        active_.store(nullptr, std::memory_order_seq_cst);
        while (users_.load(std::memory_order_seq_cst) != 0) _mm_pause();
        async_pending_.store(false, std::memory_order_release);
    }

private:
    struct Job {
        Fn fn;
        void* ctx;
        uint32_t total;
        alignas(64) std::atomic<uint32_t> next{0};
        alignas(64) std::atomic<uint32_t> done{0};
    };

    // CATHERINE_PARALLEL80_HELPERS (0-8, default 3), CATHERINE_PARALLEL80_SPIN (pause loops after a job, default
    // 1000, about 30 us), CATHERINE_PARALLEL80_PRIORITY (Win32 thread priority, default -1 = below normal: the
    // replay thread always works the job itself, so a helper that is not scheduled only means less help).
    static int setting(const char* name, int fallback, int lo, int hi) {
        const char* e = std::getenv(name);
        if (!e || !*e) return fallback;
        const int v = std::atoi(e);
        return v < lo ? lo : v > hi ? hi : v;
    }
    Pool() {
        const char* e = std::getenv("CATHERINE_PARALLEL80");
        if (e && e[0] == '0') return;
        const unsigned hw = std::thread::hardware_concurrency();
        helpers_ = hw >= 6 ? unsigned(setting("CATHERINE_PARALLEL80_HELPERS", 3, 0, 8)) : 0;
        spin_ = setting("CATHERINE_PARALLEL80_SPIN", 1000, 0, 100000);
        priority_ = setting("CATHERINE_PARALLEL80_PRIORITY", THREAD_PRIORITY_BELOW_NORMAL, -2, 2);
        pcore_ = setting("CATHERINE_PARALLEL80_PCORE", 0, 0, 1) != 0;
        for (unsigned k = 0; k < helpers_; ++k) std::thread([this] { loop(); }).detach();
    }

    static void work(Job& j, bool helper) {
        for (;;) {
            const uint32_t k = j.next.fetch_add(1, std::memory_order_relaxed);
            if (k >= j.total) return;
            j.fn(j.ctx, k);
            if (helper) g_stats.helper_chunks.fetch_add(1, std::memory_order_relaxed);
            j.done.fetch_add(1, std::memory_order_release);
        }
    }

    void loop() {
        SetThreadDescription(GetCurrentThread(), L"nvn parallel80");
        SetThreadPriority(GetCurrentThread(), priority_);
        if (pcore_) pin_performance_cores();
        uint64_t seen = generation_.load(std::memory_order_seq_cst);
        for (;;) {
            uint64_t now = seen;
            for (int spin = 0; spin < spin_ && (now = generation_.load(std::memory_order_seq_cst)) == seen; ++spin)
                _mm_pause();
            while (now == seen) {
                WaitOnAddress(&generation_, &seen, sizeof seen, INFINITE);
                now = generation_.load(std::memory_order_seq_cst);
            }
            seen = now;
            users_.fetch_add(1, std::memory_order_seq_cst);
            if (Job* j = active_.load(std::memory_order_seq_cst)) work(*j, true);
            users_.fetch_sub(1, std::memory_order_seq_cst);
        }
    }

    // perf 2026-11-22: CATHERINE_PARALLEL80_PCORE=1 keeps helpers on the highest efficiency class (P-cores) and
    // unthrottled, like plat::perf_hint_thread does for the frame threads. Scheduling only; results are unchanged.
    static void pin_performance_cores() {
        THREAD_POWER_THROTTLING_STATE throttling{};
        throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof throttling);
        ULONG length = 0;
        GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
        if (!length) return;
        uint8_t* buffer = static_cast<uint8_t*>(std::malloc(length));
        if (!buffer) return;
        if (GetSystemCpuSetInformation(reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer), length, &length, GetCurrentProcess(), 0)) {
            BYTE best = 0, worst = 255;
            for (ULONG at = 0; at < length;) {
                auto* x = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer + at);
                if (x->Type == CpuSetInformation) { if (x->CpuSet.EfficiencyClass > best) best = x->CpuSet.EfficiencyClass; if (x->CpuSet.EfficiencyClass < worst) worst = x->CpuSet.EfficiencyClass; }
                at += x->Size;
            }
            ULONG ids[256]; ULONG n = 0;
            for (ULONG at = 0; at < length && best != worst;) {
                auto* x = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer + at);
                if (x->Type == CpuSetInformation && x->CpuSet.EfficiencyClass == best && n < 256) ids[n++] = x->CpuSet.Id;
                at += x->Size;
            }
            if (n) SetThreadSelectedCpuSets(GetCurrentThread(), ids, n);
        }
        std::free(buffer);
    }

    unsigned helpers_ = 0;
    bool pcore_ = false;
    int spin_ = 0, priority_ = 0;
    alignas(64) std::atomic<Job*> active_{nullptr};
    alignas(64) std::atomic<uint32_t> users_{0};
    alignas(64) std::atomic<uint64_t> generation_{0};
    Job async_{nullptr, nullptr, 0};
    std::atomic<bool> async_pending_{false};
};

}  // namespace nx::nvn::parallel80
