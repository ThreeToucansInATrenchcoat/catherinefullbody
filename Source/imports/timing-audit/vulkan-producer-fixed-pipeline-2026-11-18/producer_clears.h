#pragma once
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>
#include "nvn_draw_state.h"

namespace nx::nvn::producer_clears {
constexpr uint64_t packet_tag = 0x31524c43504b5655ull;
struct Intent {
    VkClearAttachment attachment{};
    VkColorComponentFlags color_write_mask = 0;
    uint32_t stencil_write_mask = 0;
    uint32_t color = 0;
    uint32_t trace_mask = 0; // diagnostics only; not interpreted to execute a clear
};
static_assert(std::is_trivially_copyable_v<Intent>);
// perf 2026-11-22: default 0 (legacy reference path). The producer lowering cost ~30 FPS at the
// Stage 9/6 boss and ~12 FPS in the Day 5 bar; set the variable to 1 (or 2 to verify) to re-enable.
inline int mode() {
    static const int value = [] {
        const auto* setting = std::getenv("CATHERINE_VULKAN_PRODUCER_CLEARS");
        return setting ? std::atoi(setting) : 0;
    }();
    return value;
}
// Producer-side lowering. The packet owns Vulkan values, with no guest addresses,
// image handles, or assumptions about inherited target bindings.
inline bool encode(const Cmd& command, Intent& result) {
    if (command.op != Cmd::ClearColor && command.op != Cmd::ClearDepthStencil) return false;
    std::memset(&result, 0, sizeof result);
    result.color = command.op == Cmd::ClearColor;
    result.trace_mask = command.mask;
    if (result.color) {
        result.attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        result.attachment.colorAttachment = command.n;
        std::memcpy(result.attachment.clearValue.color.float32, command.f, 16);
        if (command.mask & 1) result.color_write_mask |= VK_COLOR_COMPONENT_R_BIT;
        if (command.mask & 2) result.color_write_mask |= VK_COLOR_COMPONENT_G_BIT;
        if (command.mask & 4) result.color_write_mask |= VK_COLOR_COMPONENT_B_BIT;
        if (command.mask & 8) result.color_write_mask |= VK_COLOR_COMPONENT_A_BIT;
    } else {
        if (command.mask & 1) result.attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        result.stencil_write_mask = (command.mask >> 8) & 255;
        if (result.stencil_write_mask) result.attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        result.attachment.clearValue.depthStencil = {command.f[0], uint32_t(command.i[0])};
    }
    return true;
}
inline std::atomic<uint64_t> recorded_packets{0};
inline uint64_t replayed_packets = 0, compared_packets = 0;
inline void attach(Cmd& command, const void*& blob, size_t& bytes, std::vector<uint8_t>& owned) {
    if (!mode()) return;
    Intent intent;
    if (!encode(command, intent)) return;
    const size_t prefix = blob ? bytes : 0;
    owned.resize(prefix + sizeof intent);
    if (prefix) std::memcpy(owned.data(), blob, prefix);
    std::memcpy(owned.data() + prefix, &intent, sizeof intent);
    command.a[7] = prefix;
    command.a[8] = packet_tag;
    blob = owned.data();
    bytes = owned.size();
    recorded_packets.fetch_add(1, std::memory_order_relaxed);
}
inline bool read(const Cmd& command, const CmdList& list, Intent& intent) {
    if (!mode() || command.a[8] != packet_tag ||
        (command.op != Cmd::ClearColor && command.op != Cmd::ClearDepthStencil) ||
        command.blob >= list.blobs.size()) return false;
    const auto& blob = list.blobs[command.blob];
    if (command.a[7] > blob.size() || sizeof intent > blob.size() - size_t(command.a[7])) return false;
    std::memcpy(&intent, blob.data() + size_t(command.a[7]), sizeof intent);
    ++replayed_packets;
    if (mode() == 2) {
        Intent legacy;
        if (!encode(command, legacy) || std::memcmp(&legacy, &intent, sizeof intent))
            fatal("Producer Vulkan clear intent disagrees with compatibility arguments");
        if (++compared_packets == 1 || compared_packets % 10000 == 0)
            log("Producer Vulkan clears verified: %llu packets", (unsigned long long)compared_packets);
    }
    return true;
}
}

namespace nx::nvn {
void draw_clear_native(VkCommandBuffer, const Targets&, const producer_clears::Intent&);
}
