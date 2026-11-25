#pragma once
#include "block20_command_packet.h"
#include "block20_root_translation.h"
#include "pose_replay_context.h"

namespace nx::nvn::replay {
const uint8_t* block20_gpu_palette(const uint8_t*, uint64_t, const ShaderInfo&);

inline const uint8_t* block20_pose_palette(
    const uint8_t* palette, uint64_t palette_bytes, const uint8_t* source,
    uint64_t source_bytes, uint32_t count, uint32_t stride) {
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
    static catherine::visual::Block20RootTranslation history;
    const uint64_t clock = catherine::visual::pose_replay_clock();
    if (!(clock & 0x80000000u)) {
        history.clear();
        return palette;
    }
    const auto& range = g_state.storage[5][0];
    const auto* owner = nx::nvn::block20_bound_owner(range.address, range.size);
    if (!owner || !owner->lifetime ||
        (owner->type != 2 && owner->type != 3)) return palette;
    catherine::visual::Block20Owner identity{
        owner->block, owner->generation, owner->model, owner->parent,
        owner->lifetime};
    const std::array<float, 3> root{
        owner->world[12], owner->world[13], owner->world[14]};
    const auto* result = history.render(
        true, identity, root, palette, palette_bytes, source, source_bytes,
        count, stride, catherine::visual::pose_replay_frame(), clock);
    if (result != palette) written_pose_palette_interpolated();
    return result;
#else
    return palette;
#endif
}
} // namespace nx::nvn::replay
