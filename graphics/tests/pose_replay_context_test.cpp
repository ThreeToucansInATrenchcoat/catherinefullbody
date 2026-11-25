#include "../src/nvn/pose_replay_context.h"
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace {
thread_local uint64_t replay_frame = 0, replay_clock = 0;
}
extern "C" uint64_t catherine_movie_replay_present(void) { return replay_frame; }
extern "C" uint64_t catherine_animation_replay_clock(void) { return replay_clock; }

int main() {
    using namespace catherine::visual;
    PoseReplayEnqueueClock enqueue;
    std::atomic<uint64_t> native_clock{0x1180000000ull};
    unsigned samples = 0;
    auto read_clock = [&] { ++samples; return native_clock.load(); };
    std::vector<PoseReplaySnapshot> jobs;
    jobs.push_back(enqueue.capture(42, read_clock));
    native_clock = 0x2280000000ull;
    jobs.push_back(enqueue.capture(42, read_clock));
    jobs.push_back(enqueue.capture(42, read_clock));
    assert(samples == 1);
    assert(jobs[0].clock == 0x1180000000ull && jobs[2].clock == jobs[0].clock);

    jobs.push_back(enqueue.capture(43, read_clock));
    native_clock = 0x3380000000ull;
    jobs.push_back(enqueue.capture(43, read_clock));
    assert(samples == 2 && jobs[3].clock == 0x2280000000ull);

    // Hold the first replay while the producer advances another two presents.
    // Neither a live producer counter nor the current Native clock may leak
    // into any draw of the delayed worker job.
    std::promise<void> entered, producer_advanced;
    auto continue_replay = producer_advanced.get_future();
    std::thread worker([&] {
        for (size_t i = 0; i < jobs.size(); ++i) {
            replay_frame = jobs[i].frame;
            replay_clock = jobs[i].clock;
            if (i == 0) {
                entered.set_value();
                continue_replay.wait();
            }
            for (unsigned draw = 0; draw != 20; ++draw) {
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
                assert(pose_replay_frame() == jobs[i].frame);
                assert(pose_replay_clock() == jobs[i].clock);
#else
                assert(pose_replay_frame() == 0 && pose_replay_clock() == 0);
#endif
            }
        }
    });
    entered.get_future().wait();
    auto next = enqueue.capture(44, read_clock);
    native_clock = 0x4480000000ull;
    auto later = enqueue.capture(45, read_clock);
    assert(next.frame == 44 && next.clock == 0x3380000000ull);
    assert(later.frame == 45 && later.clock == 0x4480000000ull);
    producer_advanced.set_value();
    worker.join();
    assert(samples == 4);
    std::cout << "pose replay context: delayed worker, multiple submits and next-present snapshots passed\n";
}
