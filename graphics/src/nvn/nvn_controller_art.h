#pragma once
#include "nvn_vk.h"
#include <cstddef>
#include <cstdint>
namespace nx::nvn {
// Full, linear BC3 base-level uploads only. Calls never read guest memory.
void controller_art_identify(GpuImage &image, uint8_t *blocks, size_t size);
bool controller_art_changed(const GpuImage &image);
void controller_art_refresh(VkCommandBuffer cb, GpuImage &image);
} // namespace nx::nvn
