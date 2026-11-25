#pragma once
#include "pose_qa_options.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace catherine::visual {
struct Quaternion {
  float x = 0, y = 0, z = 0, w = 1;
};
inline Quaternion rotation(const float *m, const float *scale) {
  float a = m[0] / scale[0], b = m[1] / scale[1], c = m[2] / scale[2];
  float d = m[4] / scale[0], e = m[5] / scale[1], f = m[6] / scale[2];
  float g = m[8] / scale[0], h = m[9] / scale[1], i = m[10] / scale[2];
  Quaternion q;
  float trace = a + e + i, s;
  if (trace > 0) {
    s = std::sqrt(trace + 1.f) * 2;
    q = {(h - f) / s, (c - g) / s, (d - b) / s, .25f * s};
  } else if (a > e && a > i) {
    s = std::sqrt(1.f + a - e - i) * 2;
    q = {.25f * s, (b + d) / s, (c + g) / s, (h - f) / s};
  } else if (e > i) {
    s = std::sqrt(1.f + e - a - i) * 2;
    q = {(b + d) / s, .25f * s, (f + h) / s, (c - g) / s};
  } else {
    s = std::sqrt(1.f + i - a - e) * 2;
    q = {(c + g) / s, (f + h) / s, .25f * s, (d - b) / s};
  }
  float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (n > 1e-8f) {
    q.x /= n;
    q.y /= n;
    q.z /= n;
    q.w /= n;
  }
  return q;
}
inline bool decompose(const float *m, float *s) {
  for (unsigned c = 0; c < 3; c++) {
    s[c] = std::sqrt(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]);
    if (!std::isfinite(s[c]) || s[c] < 1e-6f)
      return false;
  }
  const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                    m[1] * (m[4] * m[10] - m[6] * m[8]) +
                    m[2] * (m[4] * m[9] - m[5] * m[8]);
  if (!(det > 0))
    return false;
  for (unsigned a = 0; a < 3; a++)
    for (unsigned b = a + 1; b < 3; b++)
      if (std::fabs((m[a] * m[b] + m[4 + a] * m[4 + b] + m[8 + a] * m[8 + b]) /
                    (s[a] * s[b])) > .025f)
        return false;
  return true;
}
inline void blend_matrix(const float *a, const float *b, float t, float *out) {
  // Keep the unused fourth row exactly as authored, including its padding bits.
  std::memcpy(out, b, 64);
  float sa[3], sb[3];
  if (!decompose(a, sa) || !decompose(b, sb)) {
    for (unsigned i = 0; i < 12; i++)
      out[i] = a[i] + (b[i] - a[i]) * t;
    return;
  }
  auto qa = rotation(a, sa), qb = rotation(b, sb);
  if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0) {
    qb.x = -qb.x;
    qb.y = -qb.y;
    qb.z = -qb.z;
    qb.w = -qb.w;
  }
  Quaternion q = {qa.x + (qb.x - qa.x) * t, qa.y + (qb.y - qa.y) * t,
                  qa.z + (qb.z - qa.z) * t, qa.w + (qb.w - qa.w) * t};
  float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (!(n > 1e-8f)) {
    for (unsigned i = 0; i < 12; i++)
      out[i] = a[i] + (b[i] - a[i]) * t;
    return;
  }
  q.x /= n;
  q.y /= n;
  q.z /= n;
  q.w /= n;
  float rot[9] = {1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y - q.z * q.w),
                  2 * (q.x * q.z + q.y * q.w),     2 * (q.x * q.y + q.z * q.w),
                  1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z - q.x * q.w),
                  2 * (q.x * q.z - q.y * q.w),     2 * (q.y * q.z + q.x * q.w),
                  1 - 2 * (q.x * q.x + q.y * q.y)};
  for (unsigned row = 0; row < 3; row++) {
    for (unsigned col = 0; col < 3; col++)
      out[row * 4 + col] =
          rot[row * 3 + col] * (sa[col] + (sb[col] - sa[col]) * t);
    out[row * 4 + 3] = a[row * 4 + 3] + (b[row * 4 + 3] - a[row * 4 + 3]) * t;
  }
}
// Rigid vertex streams may rotate through upload buffers. Their authored data,
// rather than a transient GPU address, identifies the shared geometry.
inline uint64_t model_geometry_identity(const uint8_t *vertices,
                                        size_t available, uint32_t stride) {
  uint64_t hash = 1469598103934665603ull;
  const size_t bytes = std::min<size_t>(available, 64);
  for (size_t i = 0; i < bytes; i++) {
    hash ^= vertices[i];
    hash *= 1099511628211ull;
  }
  hash ^= stride;
  hash *= 1099511628211ull;
  return hash;
}
// Draw constants are allocated in draw order, so their addresses cannot
// identify an object when culling changes. Match each mesh instance by its last
// rendered world transform, with one claim per frame and exact reuse between
// passes.
class ModelInstanceTracker {
  struct Instance {
    uint64_t id, frame;
    std::array<float, 16> matrix;
  };
  std::unordered_map<uint64_t, std::vector<Instance>> meshes;
  uint64_t next_id = 1, last_pruned = 0;

public:
  void clear() { meshes.clear(); }
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
    auto &instances = meshes[mesh];
    for (const auto &i : instances)
      if (i.frame == frame && std::memcmp(i.matrix.data(), matrix, 64) == 0)
        return i.id;
    Instance *match = nullptr;
    float closest = 75.f * 75.f;
    for (auto &i : instances) {
      if (i.frame >= frame || frame - i.frame > 2)
        continue;
      float distance = 0;
      for (unsigned c : {3u, 7u, 11u}) {
        float d = matrix[c] - i.matrix[c];
        distance += d * d;
      }
      // Equal translations alone cannot distinguish differently oriented
      // copies. Keep searching until an exact previous transform is found.
      if (distance == 0.f && std::memcmp(i.matrix.data(), matrix, 64) == 0) {
        match = &i;
        break;
      }
      if (distance < closest) {
        closest = distance;
        match = &i;
      }
    }
    if (match) {
      std::memcpy(match->matrix.data(), matrix, 64);
      match->frame = frame;
      return match->id;
    }
    if (instances.size() > 4096)
      instances.clear();
    instances.erase(std::remove_if(instances.begin(), instances.end(),
                                   [&](const Instance &i) {
                                     return frame > i.frame &&
                                            frame - i.frame > 80;
                                   }),
                    instances.end());
    Instance fresh{next_id++, frame, {}};
    std::memcpy(fresh.matrix.data(), matrix, 64);
    instances.push_back(fresh);
    return fresh.id;
  }
};
struct PoseHistory {
  uint32_t tick = 0;
  uint64_t frame = ~0ull;
  bool ready = false;
  std::vector<float> previous, current, output, frame_input;
};
class PoseInterpolator {
  std::unordered_map<uint64_t, PoseHistory> poses;
  uint64_t last_pruned = 0;
  static bool finite_affine(const float *m) {
    for (size_t i = 0; i < 12; ++i)
      if (!std::isfinite(m[i]))
        return false;
    return true;
  }
  static bool inverse_common(const float *m, float *out) {
    if (!finite_affine(m))
      return false;
    const double d =
        double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) -
        double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8]) +
        double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
    if (!std::isfinite(d) || std::fabs(d) < 1e-8)
      return false;
    const double v = 1. / d;
    const double a[9] = {(double(m[5]) * m[10] - double(m[6]) * m[9]) * v,
                         (double(m[2]) * m[9] - double(m[1]) * m[10]) * v,
                         (double(m[1]) * m[6] - double(m[2]) * m[5]) * v,
                         (double(m[6]) * m[8] - double(m[4]) * m[10]) * v,
                         (double(m[0]) * m[10] - double(m[2]) * m[8]) * v,
                         (double(m[2]) * m[4] - double(m[0]) * m[6]) * v,
                         (double(m[4]) * m[9] - double(m[5]) * m[8]) * v,
                         (double(m[1]) * m[8] - double(m[0]) * m[9]) * v,
                         (double(m[0]) * m[5] - double(m[1]) * m[4]) * v};
    for (size_t r = 0; r < 3; ++r) {
      for (size_t c = 0; c < 3; ++c)
        out[4 * r + c] = float(a[3 * r + c]);
      out[4 * r + 3] = float(
          -(a[3 * r] * m[3] + a[3 * r + 1] * m[7] + a[3 * r + 2] * m[11]));
    }
    out[12] = out[13] = out[14] = 0;
    out[15] = 1;
    return finite_affine(out);
  }
  static bool affine_product(const float *a, const float *b, float *out) {
    for (size_t r = 0; r < 3; ++r) {
      for (size_t c = 0; c < 3; ++c) {
        double value = 0;
        for (size_t k = 0; k < 3; ++k)
          value += double(a[4 * r + k]) * b[4 * k + c];
        out[4 * r + c] = float(value);
      }
      double value = a[4 * r + 3];
      for (size_t k = 0; k < 3; ++k)
        value += double(a[4 * r + k]) * b[4 * k + 3];
      out[4 * r + 3] = float(value);
    }
    out[12] = out[13] = out[14] = 0;
    out[15] = 1;
    return finite_affine(out);
  }
  static void linear_affine(const float *a, const float *b, float t,
                            float *out) {
    std::memcpy(out, b, 64);
    // Double intermediates avoid overflow in b-a for large finite endpoints.
    for (size_t i = 0; i < 12; ++i)
      out[i] =
          float(double(a[i]) * (1. - double(t)) + double(b[i]) * double(t));
  }
  static void linear_palette(const float *a, const float *b, float t,
                             float *out) {
    for (size_t i = 0; i < 103 * 16; i += 16)
      linear_affine(a + i, b + i, t, out + i);
  }
  static bool blend_skin_palette(const float *a, const float *b, float t,
                                 float *out) {
    for (size_t i = 0; i < 103 * 16; i += 16)
      if (!finite_affine(a + i) || !finite_affine(b + i))
        return false;
    if (t == 0 || t == 1) {
      linear_palette(a, b, t, out);
      return true;
    }
    // Authored palette index15 is the pelvis/root;0 is a tongue bone.
    constexpr size_t common_word = 15 * 16;
    float inverse_a[16], inverse_b[16], common[16];
    if (!inverse_common(a + common_word, inverse_a) ||
        !inverse_common(b + common_word, inverse_b)) {
      linear_palette(a, b, t, out);
      return true;
    }
    blend_matrix(a + common_word, b + common_word, t, common);
    if (!finite_affine(common)) {
      linear_palette(a, b, t, out);
      return true;
    }
    // Skin matrices contain inverse-bind offsets. Blending each world-space
    // translation independently from its rotation breaks those offsets during
    // turns. Factor the authored pelvis frame once, blend relative skinning
    // matrices linearly, then put every bone back into the same rendered frame.
    std::memcpy(out + common_word, common, 64);
    for (size_t i = 0; i < 103 * 16; i += 16) {
      if (i == common_word)
        continue;
      float relative_a[16], relative_b[16], relative[16], rendered[16];
      if (!affine_product(inverse_a, a + i, relative_a) ||
          !affine_product(inverse_b, b + i, relative_b)) {
        linear_affine(a + i, b + i, t, out + i);
        continue;
      }
      linear_affine(relative_a, relative_b, t, relative);
      if (!affine_product(common, relative, rendered)) {
        linear_affine(a + i, b + i, t, out + i);
        continue;
      }
      std::memcpy(out + i, b + i, 64);
      std::memcpy(out + i, rendered, 12 * sizeof(float));
    }
    return true;
  }

public:
  uint64_t interpolated = 0, resets = 0;
  void clear() { poses.clear(); }
  const uint8_t *render(uint64_t identity, const uint8_t *input, size_t bytes,
                        uint64_t frame, uint64_t clock,
                        bool model_transform = false) {
    // The 103-bone nightwear rig is Vincent's puzzle model. Other rigs are
    // untouched.
    constexpr size_t puzzle_pose_bytes = 103 * 64;
    if (!input || bytes != (model_transform ? 64 : puzzle_pose_bytes))
      return input;
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
    if (model_transform) {
      blend_matrix(p.previous.data(), p.current.data(), t, p.output.data());
    } else {
#if defined(CATHERINE_POSE_DIAGNOSTICS)
      const uint32_t options = pose_qa_options(frame);
      if (options & PoseQaLinearPalette)
        linear_palette(p.previous.data(), p.current.data(), t, p.output.data());
      else if (options & PoseQaLegacyPalette)
        for (size_t i = 0; i < words; i += 16)
          blend_matrix(p.previous.data() + i, p.current.data() + i, t,
                       p.output.data() + i);
      else
#endif
          if (!blend_skin_palette(p.previous.data(), p.current.data(), t,
                                  p.output.data())) {
        reset();
        return input;
      }
    }
    ++interpolated;
    return reinterpret_cast<const uint8_t *>(p.output.data());
  }
};
} // namespace catherine::visual
