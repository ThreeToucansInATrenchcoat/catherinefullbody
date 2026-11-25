#pragma once
#include "character_pose_coherence.h"
#include "character_pose_interpolation.h"
#include "pose_qa_options.h"
#include "pose_replay_context.h"
#include "world_model_shaders.h"
namespace nx::nvn::replay {
inline uint64_t puzzle_render_frame() {
  return catherine::visual::pose_replay_frame();
}
inline uint64_t puzzle_render_clock() {
  return catherine::visual::pose_replay_clock();
}
inline catherine::visual::CharacterPoseCoherence &character_poses() {
  static catherine::visual::CharacterPoseCoherence poses;
  return poses;
}
inline const uint8_t *vincent_pose_palette(const uint8_t *palette,
                                           uint64_t size, uint64_t mesh,
                                           uint64_t source_key) {
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
  auto &poses = character_poses();
  auto &history = poses.history;
  const uint64_t frame = puzzle_render_frame();
  const uint64_t clock = (catherine::visual::pose_qa_options(frame) & 4u)
                             ? puzzle_render_clock() & ~uint64_t(0x80000000u)
                             : puzzle_render_clock();
  const auto *result = poses.palette(palette, size, frame, clock, source_key);
  if (result != palette)
    written_pose_palette_interpolated();
#ifdef CATHERINE_POSE_DIAGNOSTICS
  if (size == 103 * 64 && (clock & 0x80000000u)) {
    struct Probe {
      uint64_t frame = ~0ull, repeated = 0, intermediate = 0, last_log = 0;
      std::vector<uint8_t> raw, out;
    };
    static std::unordered_map<uint64_t, Probe> probes;
    if (probes.size() > 32768)
      probes.clear();
    auto &probe = probes[mesh];
    if (probe.frame != frame) {
      if (probe.raw.size() == size &&
          std::memcmp(probe.raw.data(), palette, size) == 0) {
        ++probe.repeated;
        if (std::memcmp(probe.out.data(), result, size) != 0)
          ++probe.intermediate;
      }
      probe.frame = frame;
      probe.raw.assign(palette, palette + size);
      probe.out.assign(result, result + size);
      if (frame > probe.last_log + 120) {
        probe.last_log = frame;
        log("VincentPose frame=%llu mesh=%llx blended=%llu sourceHeld=%llu "
            "heldButRenderedChanged=%llu resets=%llu",
            (unsigned long long)frame, (unsigned long long)mesh,
            (unsigned long long)history.interpolated,
            (unsigned long long)probe.repeated,
            (unsigned long long)probe.intermediate,
            (unsigned long long)history.resets);
      }
    }
  }
#endif
  return result;
#else
  return palette;
#endif
}
// Rigid attachments and puzzle blocks use c3 model matrices rather than the
// skeletal palette. Track instances across passes: transient constant-buffer
// addresses follow draw order, and mesh-only keys would mix neighbouring
// blocks.
inline const uint8_t *puzzle_model_transform(const uint8_t *bytes,
                                             uint64_t size, uint64_t mesh,
                                             const ShaderInfo &shader,
                                             bool rigid_geometry = true) {
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
  if (!bytes || size != 64 || shader.stage != ShaderStage::Vertex ||
      !std::binary_search(catherine::visual::world_model_shaders.begin(),
                          catherine::visual::world_model_shaders.end(),
                          std::string_view(shader.hash)))
    return bytes;
  float m[16];
  std::memcpy(m, bytes, 64);
  for (float value : m)
    if (!std::isfinite(value))
      return bytes;
  if (std::fabs(m[12]) > 1e-5f || std::fabs(m[13]) > 1e-5f ||
      std::fabs(m[14]) > 1e-5f || std::fabs(m[15] - 1.f) > 1e-5f)
    return bytes;
  static catherine::visual::PoseInterpolator models;
  static catherine::visual::ModelInstanceTracker instances;
  static bool active = false;
  uint64_t frame = puzzle_render_frame(),
           clock = puzzle_render_clock();
  const uint32_t qa_options = catherine::visual::pose_qa_options(frame);
  if (const auto *attached = (qa_options & 1u) ? nullptr :
          character_poses().head_model(reinterpret_cast<const uint8_t *>(m), frame, clock, mesh)) {
#ifdef CATHERINE_POSE_DIAGNOSTICS
    static std::unordered_map<uint64_t, uint64_t> last_log;
    auto &last = last_log[mesh];
    if (frame > last + 120) {
      last = frame;
      float fixed[16];
      std::memcpy(fixed, attached, 64);
      log("HeadAnchor frame=%llu geometry=%llx rigid=%u attached=%llu shared=%llu "
          "variants=%llu rejected=%llu source_updates=%llu alias_updates=%llu spatial_updates=%llu max_error=%.6f raw=(%.4f,%.4f,%.4f) rendered=(%.4f,%.4f,%.4f)",
          (unsigned long long)frame, (unsigned long long)mesh,
          unsigned(rigid_geometry),
          (unsigned long long)character_poses().head_models,
          (unsigned long long)character_poses().shared_meshes,
          (unsigned long long)character_poses().variant_palettes,
          (unsigned long long)character_poses().rejected_palettes,
          (unsigned long long)character_poses().same_source_updates,
          (unsigned long long)character_poses().alias_source_updates,
          (unsigned long long)character_poses().spatial_source_updates,
          character_poses().variant_max_error, m[3], m[7],
          m[11], fixed[3], fixed[7], fixed[11]);
    }
#endif
    return attached;
  }
  if (qa_options & 2u)
    return bytes;
  if (!rigid_geometry)
    return bytes;
  if (!(clock & 0x80000000u)) {
    if (active) {
      models.clear();
      instances.clear();
      active = false;
    }
    return bytes;
  }
  active = true;
  uint64_t identity = instances.resolve(mesh, m, frame);
  const uint8_t *out = models.render(identity, bytes, 64, frame, clock, true);
#ifdef CATHERINE_POSE_DIAGNOSTICS
  if ((clock & 0x80000000u) && out != bytes &&
      std::memcmp(out, bytes, 64) != 0) {
    struct Probe {
      uint64_t frame = ~0ull, changed = 0, last_log = 0;
    };
    static std::unordered_map<uint64_t, Probe> probes;
    if (probes.size() > 32768)
      probes.clear();
    auto &probe = probes[identity];
    if (probe.frame != frame) {
      probe.frame = frame;
      ++probe.changed;
      if (frame > probe.last_log + 120) {
        probe.last_log = frame;
        float blended[16];
        std::memcpy(blended, out, 64);
        log("ModelBlend frame=%llu object=%llx count=%llu raw=(%.4f,%.4f,%.4f) "
            "rendered=(%.4f,%.4f,%.4f)",
            (unsigned long long)frame, (unsigned long long)identity,
            (unsigned long long)probe.changed, m[3], m[7], m[11], blended[3],
            blended[7], blended[11]);
      }
    }
  }
#endif
  return out;
#else
  return bytes;
#endif
}
inline bool vincent_skin_shader(const ShaderInfo &shader) {
  return shader.hash == "eb49fe48c6f84ebf" || shader.hash == "01c7cddd0aab2b5c";
}
} // namespace nx::nvn::replay
