#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace catherine::visual::movement {
// Private math only. The caller must publish a genuine owner-lifetime token and
// the source movement clock at its native setter, then carry the chosen result
// into owned skin AND rigid commands. Pointers, palette size and replay time do
// not establish ownership or source publication time.
using Position = std::array<float, 3>;
struct Clock {
  uint32_t epoch = 0;
  float phase = 0;
  bool enabled = false;
};
inline bool decode_clock(uint64_t packed, Clock &out) {
  const uint32_t low = uint32_t(packed), bits = low & 0x7fffffffu;
  if ((bits & 0x7f800000u) == 0x7f800000u) return false;
  float phase; std::memcpy(&phase, &bits, 4);
  if (phase < 0 || phase >= 1) return false;
  out = {uint32_t(packed >> 32), phase, bool(low >> 31)};
  return true;
}
// Epoch subtraction is unsigned to permit UINT32_MAX -> 0. A reverse or large
// jump is rejected before converting to a continuous elapsed tick count.
inline bool elapsed_ticks(uint64_t earlier, uint64_t later, double maximum,
                          double &elapsed) {
  Clock a, b;
  if (!decode_clock(earlier, a) || !decode_clock(later, b) ||
      a.enabled != b.enabled || !std::isfinite(maximum) || maximum < 0 ||
      maximum > 65536) return false;
  const uint32_t epochs = b.epoch - a.epoch;
  if (double(epochs) > std::ceil(maximum) + 1) return false;
  elapsed = double(epochs) + double(b.phase) - a.phase;
  return elapsed >= 0 && elapsed <= maximum;
}
struct Limits {
  double max_step = 256.0;
  double max_observation_gap = 2.0;
  double max_render_age = 2.0;
};
struct Translation {
  uint64_t owner_lifetime = 0;
  Position delta{};
  Position visual{};
  bool has_delta = false;
};
enum class Publication { reset, invalid, unchanged, changed, coalesced };

class History {
  uint64_t owner_ = 0, source_ = 0, observed_ = 0;
  Position previous_{}, current_{};
  bool have_ = false, segment_ = false;
  Limits limits_;
  static bool finite(const Position &p) {
    for (float f : p) {
      uint32_t bits; std::memcpy(&bits, &f, 4);
      if ((bits & 0x7f800000u) == 0x7f800000u) return false;
    }
    return true;
  }
  static bool same(const Position &a, const Position &b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
  }
  void seed(uint64_t owner, const Position &p, uint64_t clock) {
    owner_ = owner; previous_ = current_ = p; source_ = observed_ = clock;
    have_ = true; segment_ = false;
  }
public:
  explicit History(Limits limits = {}) : limits_(limits) {}
  void reset() { owner_ = source_ = observed_ = 0; have_ = segment_ = false; }
  Publication publish(uint64_t owner, const Position &native_previous,
                      const Position &native_current, uint64_t source_clock) {
    Clock decoded;
    if (!owner || !finite(native_previous) || !finite(native_current) ||
        !decode_clock(source_clock, decoded) ||
        !std::isfinite(limits_.max_step) || limits_.max_step < 0) {
      reset(); return Publication::invalid;
    }
    double observation_gap;
    if (!have_ || owner != owner_ ||
        !elapsed_ticks(observed_, source_clock, limits_.max_observation_gap, observation_gap) ||
        !same(native_previous, current_)) {
      seed(owner, native_current, source_clock); return Publication::reset;
    }
    double distance2 = 0;
    for (size_t i = 0; i != 3; ++i) {
      const double d = double(native_current[i]) - native_previous[i];
      distance2 += d * d;
    }
    if (distance2 > limits_.max_step * limits_.max_step) {
      seed(owner, native_current, source_clock); return Publication::reset;
    }
    observed_ = source_clock;
    if (same(native_current, current_)) return Publication::unchanged;
    const bool coalesced = segment_ && source_clock == source_;
    if (coalesced) {
      double combined2 = 0;
      for (size_t i = 0; i != 3; ++i) {
        const double d = double(native_current[i]) - previous_[i];
        combined2 += d * d;
      }
      if (combined2 > limits_.max_step * limits_.max_step) {
        seed(owner, native_current, source_clock); return Publication::reset;
      }
    }
    // A second setter at the same publication instant is one final movement
    // endpoint, not an extra interpolation interval (native walk end: -7.5).
    if (!coalesced) previous_ = native_previous;
    current_ = native_current; source_ = source_clock;
    segment_ = !same(previous_, current_);
    return coalesced ? Publication::coalesced : Publication::changed;
  }
  // represented_movement must come from the exact owned matrix/palette being
  // corrected, NOT a later live model position. A native apply=0 correction may
  // leave one intermediate render matrix even after the final setter ran.
  Translation sample(uint64_t owner, const Position &represented_movement,
                     uint64_t render_clock, bool high_refresh) const {
    Translation out{owner, {}, represented_movement, false};
    Clock source, render; double elapsed;
    if (!high_refresh || !owner || !have_ || owner != owner_ || !segment_ ||
        !finite(represented_movement) || !same(represented_movement, current_) ||
        !decode_clock(source_, source) || !source.enabled ||
        !decode_clock(render_clock, render) || !render.enabled ||
        !elapsed_ticks(source_, render_clock, limits_.max_render_age, elapsed))
      return out;
    if (elapsed >= 1.0) return out;
    const double phase = elapsed;
    for (size_t i = 0; i != 3; ++i) {
      const double visual = double(previous_[i]) +
          (double(current_[i]) - previous_[i]) * phase;
      const double delta = visual - current_[i];
      if (!std::isfinite(visual) || !std::isfinite(delta) ||
          std::abs(visual) > std::numeric_limits<float>::max() ||
          std::abs(delta) > std::numeric_limits<float>::max()) return Translation{owner, {}, represented_movement, false};
      out.visual[i] = float(visual); out.delta[i] = float(delta);
    }
    out.has_delta = out.delta[0] != 0 || out.delta[1] != 0 || out.delta[2] != 0;
    return out;
  }
};

// The layouts are intentionally explicit: native palette rows use [3,7,11],
// while the SDK rigid world matrix uses column-major [12,13,14]. All other
// words, including native current-frame animation and padding, are byte-copied.
inline bool translate(const Translation &t, uint64_t owner, const void *source,
                      void *destination, size_t bytes, size_t matrices,
                      const std::array<size_t, 3> &translation_words) {
  if (!owner || t.owner_lifetime != owner || !t.has_delta || !source ||
      !destination || bytes != matrices * 64) return false;
  const auto *src = static_cast<const unsigned char *>(source);
  for (float d : t.delta) if (!std::isfinite(d)) return false;
  // Validate all translated words before any output mutation, including alias.
  for (size_t m = 0; m != matrices; ++m) for (size_t axis = 0; axis != 3; ++axis) {
    float v; std::memcpy(&v, src + m * 64 + translation_words[axis] * 4, 4);
    const double sum = double(v) + t.delta[axis];
    if (!std::isfinite(sum) || std::abs(sum) > std::numeric_limits<float>::max()) return false;
  }
  std::memmove(destination, source, bytes);
  auto *dst = static_cast<unsigned char *>(destination);
  for (size_t m = 0; m != matrices; ++m) for (size_t axis = 0; axis != 3; ++axis) {
    float v; std::memcpy(&v, dst + m * 64 + translation_words[axis] * 4, 4);
    v = float(double(v) + t.delta[axis]);
    std::memcpy(dst + m * 64 + translation_words[axis] * 4, &v, 4);
  }
  return true;
}
inline bool translate_palette89(const Translation &t, uint64_t owner,
                                const void *source, void *destination, size_t bytes) {
  return translate(t, owner, source, destination, bytes, 89, {3, 7, 11});
}
inline bool translate_rigid_world(const Translation &t, uint64_t owner,
                                 const void *source, void *destination, size_t bytes) {
  return translate(t, owner, source, destination, bytes, 1, {12, 13, 14});
}
} // namespace catherine::visual::movement
