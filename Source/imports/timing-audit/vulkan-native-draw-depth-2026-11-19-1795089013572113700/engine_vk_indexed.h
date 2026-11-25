#pragma once
#include <cstdlib>
#include <cstring>
#include "nvn.h"

namespace nx::nvn::engine_vk {
inline constexpr uint64_t indexed_tag = 0x5844494b56474e45ull;
inline constexpr uint64_t arrays_tag = 0x59525241564b4745ull;
static_assert(sizeof(VkDrawIndirectCommand) == 16);
inline constexpr uint64_t dispatch_tag = 0x48435053444b5645ull;
static_assert(sizeof(VkDispatchIndirectCommand) == 12);

inline bool dispatch_present(const Cmd& c) {
    return c.op == Cmd::DispatchCompute && c.n == 0 && c.a[5] == dispatch_tag;
}

inline void encode_dispatch(Cmd& c) {
    const VkDispatchIndirectCommand d{uint32_t(c.i[0]), uint32_t(c.i[1]), uint32_t(c.i[2])};
    std::memcpy(&c.a[6], &d, sizeof d);
    c.a[5] = dispatch_tag;
}

inline VkDispatchIndirectCommand decode_dispatch(const Cmd& c) {
    VkDispatchIndirectCommand d{};
    std::memcpy(&d, &c.a[6], sizeof d);
    return d;
}

inline bool arrays_present(const Cmd& c) {
    return c.op == Cmd::DrawArrays && c.a[5] == arrays_tag;
}

inline void encode_arrays(Cmd& c) {
    const VkDrawIndirectCommand d{uint32_t(c.i[1]), uint32_t(c.i[3]), uint32_t(c.i[0]), uint32_t(c.i[2])};
    std::memcpy(&c.a[6], &d, sizeof d);
    c.a[5] = arrays_tag;
}

inline VkDrawIndirectCommand decode_arrays(const Cmd& c) {
    VkDrawIndirectCommand d{};
    std::memcpy(&d, &c.a[6], sizeof d);
    return d;
}
struct IndexedDraw {
    VkDrawIndexedIndirectCommand args;
    VkIndexType index_type;
};
static_assert(sizeof(IndexedDraw) == 24);

inline int mode() {
    static const int setting = [] {
        const auto* value = std::getenv("CATHERINE_VULKAN_ENGINE_DRAW");
        return value ? std::atoi(value) : 1;
    }();
    return setting;
}

inline bool present(const Cmd& c) {
    return c.op == Cmd::DrawElements && c.a[5] == indexed_tag;
}

inline void encode(Cmd& c) {
    // The legacy fields remain available to code that resolves live resources.
    const IndexedDraw d{{uint32_t(c.i[0]), uint32_t(c.i[3] > 0 ? c.i[3] : 1), 0,
                         c.i[1], uint32_t(c.i[2])},
                        c.mask == 0 ? VK_INDEX_TYPE_UINT8_EXT :
                        c.mask == 1 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32};
    std::memcpy(&c.a[6], &d, sizeof d);
    c.a[4] = uint64_t(d.args.indexCount) * (d.index_type == VK_INDEX_TYPE_UINT8_EXT ? 1 :
                                       d.index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    c.a[5] = indexed_tag;
}

inline IndexedDraw decode(const Cmd& c) {
    IndexedDraw d{};
    std::memcpy(&d, &c.a[6], sizeof d);
    return d;
}
}  // namespace nx::nvn::engine_vk
