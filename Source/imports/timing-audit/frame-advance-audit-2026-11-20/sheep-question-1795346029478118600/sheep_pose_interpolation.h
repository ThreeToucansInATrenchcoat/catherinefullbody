#pragma once
#include "character_pose_coherence.h"
namespace catherine::visual::puzzle_sheep {
class SheepHistory {
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
    for (size_t i = 0; i < 65 * 16; i += 16)
      linear_affine(a + i, b + i, t, out + i);
  }
  static bool blend_skin_palette(const float *a, const float *b, float t,
                                 float *out) {
    for (size_t i = 0; i < 65 * 16; i += 16)
      if (!finite_affine(a + i) || !finite_affine(b + i))
        return false;
    if (t == 0 || t == 1) {
      linear_palette(a, b, t, out);
      return true;
    }
    // Authored palette index15 is the pelvis/root;0 is a tongue bone.
    constexpr size_t common_word = 7 * 16;
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
    for (size_t i = 0; i < 65 * 16; i += 16) {
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
    // The 65-bone nightwear rig is Vincent's puzzle model. Other rigs are
    // untouched.
    constexpr size_t puzzle_pose_bytes = 65 * 64;
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

class SheepCoherence {
  static constexpr size_t words = 65 * 16;
  static constexpr size_t head_bone = 4;
  struct SourceAlias {
    uint64_t key, last_seen;
  };
  struct Actor {
    uint64_t id = 0, frame = ~0ull, source_key = 0;
    std::array<float, words> raw{}, rendered{};
    std::array<float, 16> head_delta{}, head_inverse{};
    std::vector<SourceAlias> source_aliases;
    bool head_valid = false, ambiguous = false;
  };
  struct Attachment {
    std::array<float, 16> local{};
    uint64_t frame = ~0ull;
    unsigned samples = 0;
    bool stable = false;
  };
  std::unordered_map<uint64_t, Attachment> attachments;
  std::vector<std::unique_ptr<Actor>> actors;
  std::deque<std::array<float, 16>> scratch;
  uint64_t next_id = 1, scratch_frame = ~0ull;
  uint64_t last_attachment_pruned = 0;
  size_t scratch_used = 0;
  bool active = false;
  static float distance(const float *a, const float *b) {
    float d = 0;
    for (unsigned i : {3u, 7u, 11u}) {
      float x = a[i] - b[i];
      d += x * x;
    }
    return d;
  }

  static bool known_source(const Actor &actor, uint64_t key, uint64_t frame) {
    if (!key)
      return false;
    for (const auto &alias : actor.source_aliases)
      if (alias.key == key && frame >= alias.last_seen &&
          frame - alias.last_seen <= 8)
        return true;
    return false;
  }
  static void remember_source(Actor &actor, uint64_t key, uint64_t frame) {
    if (!key)
      return;
    for (auto &alias : actor.source_aliases)
      if (alias.key == key) {
        alias.last_seen = frame;
        return;
      }
    actor.source_aliases.push_back({key, frame});
  }
  static bool finite_pose(const float *pose) {
    for (size_t bone = 0; bone < 65; ++bone)
      for (size_t j = 0; j < 12; ++j)
        if (!std::isfinite(pose[bone * 16 + j]))
          return false;
    return true;
  }
  static bool same_pose(const float *a, const float *b) {
    for (size_t bone = 0; bone < 65; ++bone)
      for (size_t j = 0; j < 12; ++j) {
        const float x = a[bone * 16 + j], y = b[bone * 16 + j];
        // Accommodate tiny upload roundoff only. Translation is in world
        // units; basis tolerances remain independent of world coordinates.
        const float tolerance = j % 4 == 3 ? 1e-3f : 1e-5f;
        if (!std::isfinite(x) || !std::isfinite(y) ||
            std::fabs(x - y) > tolerance)
          return false;
      }
    return true;
  }

public:
  SheepHistory history;
  uint64_t shared_meshes = 0, head_models = 0, variant_palettes = 0;
  uint64_t rejected_palettes = 0, ambiguous_head_models = 0;
  uint64_t same_source_updates = 0, alias_source_updates = 0;
  uint64_t spatial_source_updates = 0;
  // Retain the obsolete orientation counter for existing diagnostic consumers.
  uint64_t near_head_rejected_orientation = 0, near_head_stale = 0;
  float variant_max_error = 0;
  const uint8_t *palette(const uint8_t *bytes, size_t size, uint64_t frame,
                         uint64_t clock, uint64_t source_key = 0) {
    if (!bytes || size != words * 4)
      return bytes;
    if (!(clock & 0x80000000u)) {
      if (active) {
        actors.clear();
        history.clear();
        attachments.clear();
        active = false;
      }
      return bytes;
    }
    active = true;
    const auto *raw = reinterpret_cast<const float *>(bytes);
    Actor *same_frame = nullptr;
    // Native palette storage identifies the producer's snapshot within a
    // render frame. The guest may update that storage while draws replay;
    // every mesh from that producer must keep the first captured pose.
    if (source_key)
      for (auto &actor : actors)
        if (actor->frame == frame && actor->source_key == source_key &&
            distance(actor->raw.data(), raw) < 75.f * 75.f) {
          same_frame = actor.get();
          break;
        }
    if (!same_frame && source_key)
      for (auto &actor : actors)
        if (actor->frame == frame && known_source(*actor, source_key, frame) &&
            distance(actor->raw.data(), raw) < 75.f * 75.f) {
          same_frame = actor.get();
          break;
        }
    if (!same_frame)
      for (auto &actor : actors)
        if (actor->frame == frame && distance(actor->raw.data(), raw) < .25f) {
          same_frame = actor.get();
          break;
        }
    if (same_frame) {
      const bool same_source =
          source_key && same_frame->source_key == source_key;
      if (std::memcmp(same_frame->raw.data(), bytes, size) != 0) {
        ++variant_palettes;
        // Inspect a small number of mismatches. The unused fourth row can
        // carry unrelated padding bits, so compare only meaningful 3x4 rows.
        if (variant_palettes <= 8)
          for (size_t bone = 0; bone < 65; bone++)
            for (size_t j = 0; j < 12; j++) {
              const float delta = std::fabs(same_frame->raw[bone * 16 + j] -
                                            raw[bone * 16 + j]);
              if (std::isfinite(delta))
                variant_max_error = std::max(variant_max_error, delta);
            }
#ifdef CATHERINE_POSE_DIAGNOSTICS
        // Diagnostic records: four native little-endian uint64 fields, then
        // canonical and incoming raw 65x4x4 float palettes (including
        // padding). Keep replay collection bounded and readable while the game
        // runs.
        static std::FILE *variant_dump = [] {
#ifdef _WIN32
          std::FILE *file = nullptr;
          ::fopen_s(&file, "pose-variants.bin", "wb");
          return file;
#else
          return std::fopen("pose-variants.bin", "wb");
#endif
        }();
        static unsigned dumped_variants = 0;
        if (variant_dump && dumped_variants < 256) {
          const uint64_t metadata[] = {frame, same_frame->source_key,
                                       source_key, same_frame->id};
          const bool written =
              std::fwrite(metadata, sizeof metadata, 1, variant_dump) == 1 &&
              std::fwrite(same_frame->raw.data(), words * sizeof(float), 1,
                          variant_dump) == 1 &&
              std::fwrite(bytes, words * sizeof(float), 1, variant_dump) == 1;
          std::fflush(variant_dump);
          if (written)
            ++dumped_variants;
          else
            dumped_variants = 256;
        }
#endif
        if (same_source)
          ++same_source_updates;
        // Native single-player buffers can advance while older draws replay.
        // Nearby head snapshots belong to the same65-entry avatar even when
        // their remaining bones have advanced. Unkeyed callers stay strict.
        else if (!same_pose(same_frame->raw.data(), raw)) {
          // A recently verified producer alias can advance while this frame
          // replays, just like the canonical storage itself. The candidate
          // lookup above still bounds known aliases to75 world units.
          if (known_source(*same_frame, source_key, frame)) {
            ++alias_source_updates;
          } else if (source_key && same_frame->source_key &&
                     distance(same_frame->raw.data(), raw) < .25f) {
            ++spatial_source_updates;
          } else {
            ++rejected_palettes;
            same_frame->ambiguous = true;
            return bytes;
          }
        }
      }
      // Initial equality establishes producer lineage; accepted nearby
      // updates maintain it while that producer is continuously observed.
      // Keys absent for more than8 frames must prove their lineage again.
      remember_source(*same_frame, source_key, frame);
      ++shared_meshes;
      return reinterpret_cast<const uint8_t *>(same_frame->rendered.data());
    }
    if (!finite_pose(raw)) {
      ++rejected_palettes;
      return bytes;
    }
    Actor *selected = nullptr, *same_source = nullptr;
    float closest = 75.f * 75.f, same_source_distance = closest;
    for (auto &actor : actors)
      if (frame > actor->frame && frame - actor->frame <= 2) {
        float d = distance(actor->raw.data(), raw);
        if (source_key && actor->source_key == source_key &&
            d < same_source_distance) {
          same_source_distance = d;
          same_source = actor.get();
        }
        if (d < closest) {
          closest = d;
          selected = actor.get();
        }
      }
    if (same_source)
      selected = same_source;
    if (!selected) {
      actors.erase(std::remove_if(actors.begin(), actors.end(),
                                  [&](const auto &a) {
                                    return frame > a->frame &&
                                           frame - a->frame > 80;
                                  }),
                   actors.end());
      auto actor = std::make_unique<Actor>();
      actor->id = next_id++;
      selected = actor.get();
      actors.push_back(std::move(actor));
    }
    // Replay overlaps the native producer. Capture once before blending so
    // interpolation and the attachment inverse use the identical input pose.
    std::memcpy(selected->raw.data(), bytes, size);
    const auto *captured =
        reinterpret_cast<const uint8_t *>(selected->raw.data());
    const auto *interpolated =
        history.render(selected->id, captured, size, frame, clock);
    std::memcpy(selected->rendered.data(), interpolated, size);
    selected->frame = frame;
    selected->source_key = source_key;
    selected->source_aliases.erase(
        std::remove_if(
            selected->source_aliases.begin(), selected->source_aliases.end(),
            [&](const SourceAlias &alias) {
              return frame > alias.last_seen && frame - alias.last_seen > 8;
            }),
        selected->source_aliases.end());
    remember_source(*selected, source_key, frame);
    selected->ambiguous = false;
    float inverse[16];
    selected->head_valid = inverse_affine(selected->raw.data() + head_bone * 16, inverse);
    if (selected->head_valid) {
      std::memcpy(selected->head_inverse.data(), inverse, 64);
      multiply_affine(selected->rendered.data() + head_bone * 16, inverse,
                      selected->head_delta.data());
    }
    return reinterpret_cast<const uint8_t *>(selected->rendered.data());
  }
  const uint8_t *head_model(const uint8_t *bytes, uint64_t frame,
                            uint64_t clock, uint64_t geometry = 0) {
    if (!active || !(clock & 0x80000000u) || !bytes)
      return nullptr;
    if (frame > last_attachment_pruned + 64) {
      last_attachment_pruned = frame;
      for (auto it = attachments.begin(); it != attachments.end();) {
        if (frame > it->second.frame && frame - it->second.frame > 80)
          it = attachments.erase(it);
        else
          ++it;
      }
    }
    const auto *model = reinterpret_cast<const float *>(bytes);
    for (unsigned i = 0; i < 12; i++)
      if (!std::isfinite(model[i]))
        return nullptr;
    Actor *selected = nullptr;
    float nearest = 32.f * 32.f;
    const Attachment *trusted = nullptr;
    if (geometry) {
      auto found = attachments.find(geometry);
      if (found != attachments.end() && found->second.stable &&
          frame >= found->second.frame && frame - found->second.frame <= 80)
        trusted = &found->second;
    }
    for (auto &actor : actors) {
      if (!actor->head_valid)
        continue;
      float d = distance(actor->raw.data(), model);
      if (d >= 32.f * 32.f)
        continue;
      if (actor->frame != frame) {
        ++near_head_stale;
        continue;
      }
      // Attachment bind bases differ by over 100 degrees in real hair/face
      // draws. Use the measured head-region radius, not an orientation gate.
      if (trusted) {
        float expected[16];
        multiply_affine(actor->raw.data() + head_bone * 16,
                        trusted->local.data(), expected);
        d = 0;
        for (size_t i = 0; i < 12; ++i) {
          float error = model[i] - expected[i];
          // Include bind orientation so crossing/replayed head snapshots do
          // not exchange a mesh merely because their origins are nearby.
          if (i % 4 != 3)
            error *= 25.f;
          d += error * error;
        }
      }
      if (d < nearest) {
        nearest = d;
        selected = actor.get();
      }
    }
    if (!selected)
      return nullptr;
    if (selected->ambiguous) {
      ++ambiguous_head_models;
      return nullptr;
    }
#ifdef CATHERINE_POSE_DIAGNOSTICS
    if (pose_qa_options(frame) & 64u) {
      static std::FILE *attachment_dump = std::fopen("head-attachments.bin", "wb");
      static unsigned records = 0;
      if (attachment_dump && records < 24000) {
        const uint64_t metadata[] = {frame, geometry, selected->id, clock};
        std::fwrite(metadata, sizeof metadata, 1, attachment_dump);
        std::fwrite(selected->raw.data() + head_bone * 16, 64, 1, attachment_dump);
        std::fwrite(selected->rendered.data() + head_bone * 16, 64, 1, attachment_dump);
        std::fwrite(bytes, 64, 1, attachment_dump);
        std::fflush(attachment_dump);
        ++records;
      }
    }
#endif
    if (scratch_frame != frame) {
      scratch_frame = frame;
      scratch_used = 0;
    }
    if (scratch_used == scratch.size())
      scratch.emplace_back();
    auto &out = scratch[scratch_used++];
    const float *local = nullptr;
    if (geometry) {
      float observed[16];
      multiply_affine(selected->head_inverse.data(), model, observed);
      auto &attachment = attachments[geometry];
      if (attachment.frame != frame) {
        if (!attachment.samples || frame < attachment.frame ||
            frame - attachment.frame > 80) {
          std::memcpy(attachment.local.data(), observed, 64);
          attachment.samples = 1;
          attachment.stable = false;
        } else if (!attachment.stable) {
          bool agrees = true;
          for (size_t i = 0; i < 12; ++i)
            if (std::fabs(observed[i] - attachment.local[i]) >
                (i % 4 == 3 ? .35f : .006f))
              agrees = false;
          if (agrees) {
            if (++attachment.samples >= 4)
              attachment.stable = true;
          } else {
            std::memcpy(attachment.local.data(), observed, 64);
            attachment.samples = 1;
          }
        }
        attachment.frame = frame;
      }
      if (attachment.stable)
        local = attachment.local.data();
    }
    // Once the local bind is measured consistently, stop recomputing it from
    // guest draw constants. Those constants may already be the next native
    // pose while the body uses this frame's captured palette.
    if (local)
      multiply_affine(selected->rendered.data() + head_bone * 16, local, out.data());
    else
      multiply_affine(selected->head_delta.data(), model, out.data());
    ++head_models;
    return reinterpret_cast<const uint8_t *>(out.data());
  }
};

}
namespace nx::nvn::replay {
extern "C" { inline uint64_t catherine_sheep_pose_counters[3] = {}; }
inline const uint8_t* sheep_pose_palette(const uint8_t* palette,uint64_t size,uint32_t count,uint64_t source) {
 // Stock c21_00_0001.GMD has one 3384-vertex mesh and 65 bones.
 // Authored pelvis=7, head=4. Simulation and native input palettes stay unchanged.
 if(!palette||size!=65*64||count!=3384)return palette;
 static catherine::visual::puzzle_sheep::SheepCoherence sheep;
 const auto* out=sheep.palette(palette,size,puzzle_render_frame(),puzzle_render_clock(),source);
 ++catherine_sheep_pose_counters[0];
 catherine_sheep_pose_counters[1]=sheep.history.interpolated;
 catherine_sheep_pose_counters[2]=sheep.history.resets;
 if(out!=palette)written_pose_palette_interpolated();
 return out;
}
}
