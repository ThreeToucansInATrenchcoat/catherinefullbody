#pragma once
#include "character_pose_interpolation.h"
namespace catherine::visual {
// One-for-one history replacement for path88 rigid model transforms only.
// The existing global character/palette/root helpers remain byte-identical.
class RigidLateSampleInterpolator {
  std::unordered_map<uint64_t, PoseHistory> poses;
  uint64_t last_pruned = 0;
public:
  uint64_t interpolated = 0, resets = 0;
  void clear() { poses.clear(); }
  const uint8_t *render(uint64_t identity, const uint8_t *input, size_t bytes,
                        uint64_t frame, uint64_t clock,
                        bool model_transform = false) {
    // The 103-bone nightwear rig is Vincent's puzzle model. Other rigs are
    // untouched.
    if (!input || !model_transform || bytes != 64) return input;
    // Expire unused histories without resetting visible objects when temporary
    // render buffers generate many short-lived identities.
    if (frame > last_pruned + 64) {
      last_pruned = frame;
      for (auto it = poses.begin(); it != poses.end();) {
        const auto used = it->second.frame;
        if (used == ~0ull || (frame > used && frame - used > 80))
          it = poses.erase(it);
        else
          ++it;
      }
    }
    auto &p = poses[identity];
    uint32_t fraction_bits = uint32_t(clock) & 0x7fffffffu,
             tick = uint32_t(clock >> 32);
    float t;
    std::memcpy(&t, &fraction_bits, 4);
    const bool enabled =
        (uint32_t(clock) & 0x80000000u) && std::isfinite(t) && t >= 0 && t <= 1;
    const auto *values = reinterpret_cast<const float *>(input);
    size_t words = bytes / 4;
    auto reset = [&]() {
      p.previous.assign(values, values + words);
      p.current = p.previous;
      p.output = p.previous;
      p.frame_input = p.previous;
      p.tick = tick;
      p.frame = frame;
      p.ready = false;
      ++resets;
    };
    if (!enabled) {
      p.frame = ~0ull;
      p.current.clear();
      return input;
    }
    if (p.current.size() != words || frame > p.frame + 80 || frame < p.frame) {
      reset();
      return input;
    }
    if (p.frame == frame) {
      // Shared geometry can be drawn by two players. Never borrow another
      // instance's pose.
      return std::memcmp(input, p.frame_input.data(), bytes) == 0
                 ? reinterpret_cast<const uint8_t *>(p.output.data())
                 : input;
    }
    float distance = 0;
    for (unsigned i : {3u, 7u, 11u}) {
      float d = values[i] - p.current[i];
      distance += d * d;
    }
    if (!std::isfinite(distance) || distance > 200.f * 200.f ||
        uint32_t(tick - p.tick) > 1) {
      reset();
      return input;
    }
    p.frame = frame;
    p.frame_input.assign(values, values + words);
    if (tick != p.tick) {
      p.previous = p.current;
      p.current.assign(values, values + words);
      p.tick = tick;
      p.ready = true;
    } else if (p.ready && std::memcmp(input, p.current.data(), bytes) != 0) {
      // A rigid native update may finish after the first presented sample for
      // its tick. Only a bit-exact common basis permits refreshing this tick's
      // endpoint. The earlier same-present guard still rejects resampling.
      bool unchanged_basis = true;
      for (size_t i = 0; i < 16; ++i)
        if (i != 3 && i != 7 && i != 11 &&
            std::memcmp(values + i, p.current.data() + i, 4) != 0)
          unchanged_basis = false;
      if (unchanged_basis) p.current.assign(values, values + words);
    }
    if (!p.ready) {
      p.output.assign(values, values + words);
      return input;
    }
    // Most puzzle blocks are stationary. Preserve their exact authored
    // transform and avoid decomposing hundreds of identical matrices every
    // render frame.
    if (std::memcmp(p.previous.data(), p.current.data(), bytes) == 0) {
      p.output = p.current;
      return reinterpret_cast<const uint8_t *>(p.output.data());
    }
    blend_matrix(p.previous.data(), p.current.data(), t, p.output.data());
    ++interpolated;
    return reinterpret_cast<const uint8_t *>(p.output.data());
  }
};
}
