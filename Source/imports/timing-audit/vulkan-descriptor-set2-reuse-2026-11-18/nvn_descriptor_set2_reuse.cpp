#include "nvn_descriptor_bytes.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace nx::nvn::replay::memo87 {extern std::atomic<uint64_t>g_epoch;}

namespace nx::nvn {
using namespace nx::vk;

namespace {
struct Key {
    VkDescriptorType type{};
    uint64_t first = 0, second = 0, third = 0;
    bool operator==(const Key&) const = default;
};

constexpr size_t kCapacity = 8192;
constexpr size_t kMaxBytes = 128;

struct Entry {
    uint64_t generation = 0;
    Key key;
    std::array<uint8_t, kMaxBytes> bytes;
};
std::array<Entry, kCapacity> cache;
uint64_t generation = 0, hits = 0, misses = 0;
VkCommandBuffer bound = VK_NULL_HANDLE;

size_t hash(const Key& key) {
    uint64_t h = key.first * 0x9e3779b97f4a7c15ull ^ key.second * 0x94d049bb133111ebull ^
                 key.third * 0xbf58476d1ce4e5b9ull ^ uint64_t(key.type);
    h ^= h >> 30;
    h *= 0x9e3779b97f4a7c15ull;
    return size_t(h >> 32);
}

VkDeviceSize align_up(VkDeviceSize at, VkDeviceSize alignment) {
    return (at + alignment - 1) & ~(alignment - 1);
}

void write_bytes(uint8_t* target, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
                 const VkDescriptorImageInfo* image) {
    const size_t size = descriptor_byte_size(type);
    Key key{type};
    if (buffer) {
        key.first = uintptr_t(buffer->buffer);
        key.second = buffer->offset;
        key.third = buffer->range;
    } else {
        key.first = uintptr_t(image->sampler);
        key.second = uintptr_t(image->imageView);
        key.third = image->imageLayout;
    }
    Entry* vacant = nullptr;
    const size_t start = hash(key);
    for (size_t step = 0; step < 16; ++step) {
        Entry& e = cache[(start + step) & (kCapacity - 1)];
        if (e.generation != generation) {
            vacant = &e;
            break;
        }
        if (e.key == key) {
            std::memcpy(target, e.bytes.data(), size);
            ++hits;
            return;
        }
    }
    VkDescriptorGetInfoEXT get{VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get.type = type;
    VkDescriptorAddressInfoEXT address{VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
    if (buffer) {
        VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        info.buffer = buffer->buffer;
        const auto base = info.buffer == descriptor_ring_buffer() ? descriptor_ring_address()
                                                                 : p_vkGetBufferDeviceAddress(vk_device(), &info);
        address.address = base + buffer->offset;
        address.range = buffer->range;
        address.format = VK_FORMAT_UNDEFINED;
        if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            get.data.pUniformBuffer = &address;
        else
            get.data.pStorageBuffer = &address;
    } else if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        get.data.pCombinedImageSampler = image;
    } else {
        get.data.pStorageImage = image;
    }
    if (vacant) {
        p_vkGetDescriptorEXT(vk_device(), &get, size, vacant->bytes.data());
        std::memcpy(target, vacant->bytes.data(), size);
        vacant->key = key;
        vacant->generation = generation;
    } else {
        std::array<uint8_t, kMaxBytes> scratch;
        p_vkGetDescriptorEXT(vk_device(), &get, size, scratch.data());
        std::memcpy(target, scratch.data(), size);
    }
    ++misses;
}
void bind_buffer(VkCommandBuffer cb) {
    if (bound == cb) return;
    VkDescriptorBufferBindingInfoEXT binding{VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
    binding.address = descriptor_ring_address();
    binding.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                    VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
    p_vkCmdBindDescriptorBuffersEXT(cb, 1, &binding);
    bound = cb;
}

void bind_offsets(VkCommandBuffer cb, VkPipelineBindPoint point, VkPipelineLayout layout,
                  const std::array<VkDeviceSize, 4>& offsets) {
    bind_buffer(cb);
    const std::array<uint32_t, 4> indices{};
    p_vkCmdSetDescriptorBufferOffsetsEXT(cb, point, layout, 0, uint32_t(offsets.size()), indices.data(), offsets.data());
}

Upload allocate_descriptor_bytes(const ProgramLayout& layout, std::array<VkDeviceSize, 4>& offsets) {
    const VkDeviceSize alignment = descriptor_buffer_properties()->descriptorBufferOffsetAlignment;
    VkDeviceSize total = 0;
    for (size_t k = 0; k < offsets.size(); ++k) {
        offsets[k] = total;
        total = align_up(total + layout.bytes[k]->size, alignment);
    }
    return upload_alloc(total, alignment);
}

void bind_uploaded_descriptor_bytes(VkCommandBuffer cb, VkPipelineBindPoint point, const ProgramLayout& layout,
                                    std::array<VkDeviceSize, 4>& offsets, VkDeviceSize base) {
    for (size_t set = 0; set < offsets.size(); ++set)
        offsets[set] = layout.bytes[set]->size ? base + offsets[set] : 0;
    bind_offsets(cb, point, layout.layout, offsets);
}

#include "set2_reuse.inc"

}  // namespace

void descriptor_bytes_begin() {
    ++generation;
    bound = VK_NULL_HANDLE;
}

void descriptor_bytes_legacy_bind(VkCommandBuffer cb) {
    if (bound == cb) bound = VK_NULL_HANDLE;
}

void descriptor_bytes_log() {
    set2_reuse::report();
    if (hits + misses)
        log("nvn/vk: descriptor bytes %llu reused, %llu generated",
            (unsigned long long)hits, (unsigned long long)misses);
    hits = misses = 0;
}

void bind_descriptor_bytes(VkCommandBuffer cb, VkPipelineBindPoint point, const ProgramLayout& layout,
                           const std::vector<VkWriteDescriptorSet>& writes, const std::vector<uint8_t>& set_indices) {
    if(set2_reuse::writes(cb,point,layout,writes,set_indices))return;
    if (writes.empty()) return;
    std::array<VkDeviceSize, 4> offsets{};
    const Upload data = allocate_descriptor_bytes(layout, offsets);
    for (size_t i = 0; i < writes.size(); ++i) {
        const auto& w = writes[i];
        const uint8_t set = set_indices[i];
        const auto& binding = *layout.bytes[set];
        write_bytes(data.ptr + offsets[set] + binding.offsets[w.dstBinding], w.descriptorType,
                    w.pBufferInfo, w.pImageInfo);
    }
    bind_uploaded_descriptor_bytes(cb, point, layout, offsets, data.offset);
}

void bind_descriptor_values(VkCommandBuffer cb, VkPipelineBindPoint point, const ProgramLayout& layout,
                            const std::vector<DescriptorValue>& values) {
    if(set2_reuse::values(cb,point,layout,values))return;
    if (values.empty()) return;
    std::array<VkDeviceSize, 4> offsets{};
    const Upload data = allocate_descriptor_bytes(layout, offsets);
    for (const auto& value : values) {
        const auto& binding = *layout.bytes[value.set];
        write_bytes(data.ptr + offsets[value.set] + binding.offsets[value.binding], value.type,
                    value.uses_buffer ? &value.buffer : nullptr, value.uses_buffer ? nullptr : &value.image);
    }
    bind_uploaded_descriptor_bytes(cb, point, layout, offsets, data.offset);
}

}  // namespace nx::nvn
