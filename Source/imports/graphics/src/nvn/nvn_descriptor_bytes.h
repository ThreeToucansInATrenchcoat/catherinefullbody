#pragma once

#include "nvn_draw_state.h"

namespace nx::nvn {
// A resolved binding prepared on the replay thread. Each record owns the small Vulkan payload.
struct DescriptorValue {
    VkDescriptorType type;
    uint32_t binding;
    uint8_t set;
    bool uses_buffer;
    union {
        VkDescriptorBufferInfo buffer;
        VkDescriptorImageInfo image;
    };

    DescriptorValue(VkDescriptorType t, uint32_t b, uint8_t s, const VkDescriptorBufferInfo& info)
        : type(t), binding(b), set(s), uses_buffer(true), buffer(info) {}
    DescriptorValue(VkDescriptorType t, uint32_t b, uint8_t s, const VkDescriptorImageInfo& info)
        : type(t), binding(b), set(s), uses_buffer(false), image(info) {}
};

const VkPhysicalDeviceDescriptorBufferPropertiesEXT* descriptor_buffer_properties();
VkDeviceAddress descriptor_ring_address();
VkBuffer descriptor_ring_buffer();
size_t descriptor_byte_size(VkDescriptorType type);
void descriptor_bytes_begin();
void descriptor_bytes_legacy_bind(VkCommandBuffer cb);
void descriptor_bytes_log();
void bind_descriptor_values(VkCommandBuffer cb, VkPipelineBindPoint point, const ProgramLayout& layout,
                            const std::vector<DescriptorValue>& values);
void bind_descriptor_bytes(VkCommandBuffer cb, VkPipelineBindPoint point, const ProgramLayout& layout,
                           const std::vector<VkWriteDescriptorSet>& writes, const std::vector<uint8_t>& set_indices);
}
