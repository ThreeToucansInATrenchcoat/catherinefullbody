#pragma once
#include <cstdint>
#include <utility>

#if defined(CATHERINE_HIGH_REFRESH_BUILD)
extern "C" uint64_t catherine_movie_replay_present(void);
extern "C" uint64_t catherine_animation_replay_clock(void);
#endif

namespace catherine::visual {

struct PoseReplaySnapshot {
    uint64_t frame = 0;
    uint64_t clock = 0;
};

// Called at queue enqueue under the submit lock. Several submits can belong to
// one present, and the guest may advance before the worker replays any of them.
// Sample the guest's atomic animation clock only for the first submit, then
// carry this immutable pair with every worker job for that present.
class PoseReplayEnqueueClock {
public:
    template<class ReadClock>
    PoseReplaySnapshot capture(uint64_t frame, ReadClock&& read_clock) {
        if (!valid_ || snapshot_.frame != frame) {
            snapshot_ = {frame, std::forward<ReadClock>(read_clock)()};
            valid_ = true;
        }
        return snapshot_;
    }

private:
    PoseReplaySnapshot snapshot_{};
    bool valid_ = false;
};

inline uint64_t pose_replay_frame() {
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
    return catherine_movie_replay_present();
#else
    return 0;
#endif
}

inline uint64_t pose_replay_clock() {
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
    return catherine_animation_replay_clock();
#else
    return 0;
#endif
}

} // namespace catherine::visual
