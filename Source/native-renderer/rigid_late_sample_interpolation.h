#pragma once
#include "character_pose_interpolation.h"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <utility>
#include "C:/catherine/recomp/Source/imports/timing-audit/frame-advance-audit-2026-11-20/safety-root-candidate-1795300767913069600/candidate/cyclic_rigid_interpolation.h"
namespace catherine::visual {
// One-for-one history replacement for path88 rigid model transforms only.
// The existing global character/palette/root helpers remain byte-identical.
class RigidLateSampleInterpolator {
  struct FixedPose {
    uint32_t tick=0;uint64_t frame=~0ull;bool ready=false,has_current=false;
    std::array<float,16> previous,current,output,frame_input;
  };
  struct IdentityHash {size_t operator()(uint64_t v)const noexcept {v^=v>>30;v*=0xbf58476d1ce4e5b9ull;v^=v>>27;v*=0x94d049bb133111ebull;return size_t(v^(v>>31));}};
  std::unordered_map<uint64_t,FixedPose,IdentityHash> poses;
  struct HotSlot {uint64_t id=0;FixedPose* value=nullptr;};
  std::array<HotSlot,4096> hot{};
  static size_t cache_slots(){static const size_t value=[] {const char* e=std::getenv("CATHERINE_NATIVE_RIGID_CACHE_SLOTS");return e&&std::atoi(e)==1024?size_t(1024):size_t(4096);}();return value;}
  static int cache_mode(){static const int value=[] {const char* e=std::getenv("CATHERINE_NATIVE_RIGID_CACHE");return e?std::atoi(e):1;}();return value;}
  void invalidate_hot(){hot.fill({});}
  FixedPose& pose(uint64_t id){
    const int mode=cache_mode();if(!mode)return poses[id];
    auto& slot=hot[id&(cache_slots()-1)];
    if(mode==2){static uint64_t queries=0,hits=0;++queries;hits+=slot.value&&slot.id==id;if(queries==1||queries%1000000==0)std::fprintf(stderr,"Rigid history cache: queries %llu hits %llu slots %zu\n",(unsigned long long)queries,(unsigned long long)hits,cache_slots());}
    if(slot.value&&slot.id==id){
      if(mode==2){auto it=poses.find(id);if(it==poses.end()||&it->second!=slot.value){std::fprintf(stderr,"Rigid history cache pointer mismatch\n");std::abort();}}
      return *slot.value;
    }
    auto& value=poses[id];slot={id,&value};return value;
  }
  uint64_t last_pruned = 0;
public:
  uint64_t interpolated = 0, resets = 0;
  RigidLateSampleInterpolator()=default;
  RigidLateSampleInterpolator(const RigidLateSampleInterpolator& o):poses(o.poses),last_pruned(o.last_pruned),interpolated(o.interpolated),resets(o.resets){}
  RigidLateSampleInterpolator& operator=(const RigidLateSampleInterpolator& o){if(this!=&o){invalidate_hot();poses=o.poses;last_pruned=o.last_pruned;interpolated=o.interpolated;resets=o.resets;}return *this;}
  RigidLateSampleInterpolator(RigidLateSampleInterpolator&& o):poses(std::move(o.poses)),last_pruned(o.last_pruned),interpolated(o.interpolated),resets(o.resets){o.invalidate_hot();}
  RigidLateSampleInterpolator& operator=(RigidLateSampleInterpolator&& o){if(this!=&o){invalidate_hot();o.invalidate_hot();poses=std::move(o.poses);last_pruned=o.last_pruned;interpolated=o.interpolated;resets=o.resets;}return *this;}
  void clear() { invalidate_hot();poses.clear(); }
  const uint8_t *render(uint64_t identity, const uint8_t *input, size_t bytes,
                        uint64_t frame, uint64_t clock,
                        bool model_transform = false, unsigned rotation_sectors = 1) {
    // The 103-bone nightwear rig is Vincent's puzzle model. Other rigs are
    // untouched.
    if (!input || !model_transform || bytes != 64) return input;
    // Expire unused histories without resetting visible objects when temporary
    // render buffers generate many short-lived identities.
    if (frame > last_pruned + 64) {
      last_pruned = frame;
      invalidate_hot();
      for (auto it = poses.begin(); it != poses.end();) {
        const auto used = it->second.frame;
        if (used == ~0ull || (frame > used && frame - used > 80))
          it = poses.erase(it);
        else
          ++it;
      }
    }
    auto &p = pose(identity);
    uint32_t fraction_bits = uint32_t(clock) & 0x7fffffffu,
             tick = uint32_t(clock >> 32);
    float t;
    std::memcpy(&t, &fraction_bits, 4);
    const bool enabled =
        (uint32_t(clock) & 0x80000000u) && std::isfinite(t) && t >= 0 && t <= 1;
    const auto *values = reinterpret_cast<const float *>(input);
    size_t words = bytes / 4;
    auto reset = [&]() {
      std::memcpy(p.previous.data(),values,64);p.has_current=true;
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
      p.has_current=false;
      return input;
    }
    if (!p.has_current || frame > p.frame + 80 || frame < p.frame) {
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
    std::memcpy(p.frame_input.data(),values,64);
    if (tick != p.tick) {
      p.previous = p.current;
      std::memcpy(p.current.data(),values,64);
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
      if (unchanged_basis) std::memcpy(p.current.data(),values,64);
    }
    if (!p.ready) {
      std::memcpy(p.output.data(),values,64);
      return input;
    }
    // Most puzzle blocks are stationary. Preserve their exact authored
    // transform and avoid decomposing hundreds of identical matrices every
    // render frame.
    if (std::memcmp(p.previous.data(), p.current.data(), bytes) == 0) {
      p.output = p.current;
      return reinterpret_cast<const uint8_t *>(p.output.data());
    }
    blend_cyclic_z(p.previous.data(), p.current.data(), t, p.output.data(),rotation_sectors);
    ++interpolated;
    return reinterpret_cast<const uint8_t *>(p.output.data());
  }
};
}
