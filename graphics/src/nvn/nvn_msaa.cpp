// Resolve multisampled geometry into the guest's original single-sample textures.
#include <atomic>
#include <map>

#include "nvn_vk.h"

namespace nx::nvn {
using namespace nx::vk;

namespace {
constexpr unsigned kSampleCounts = VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
constexpr VkDeviceSize kSidecarBudget = 2ull << 30;
std::atomic<unsigned> g_sample_mask{0};
VkDeviceSize g_sidecar_bytes = 0;  // protected by gpu_lock

bool color_resolve_supported(VkFormat format) {
    // Only ordinary color targets. Integer attachments use sample-zero, which
    // would change the coverage of ID/mask buffers.
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return true;
        default:
            return false;
    }
}

VkImageUsageFlags attachment_usage(VkImageAspectFlags aspect) {
    return aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                               : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
}

bool supports_image(const GpuImage& src, VkFormat format, VkSampleCountFlagBits samples) {
    static std::map<std::pair<VkFormat, VkImageUsageFlags>, VkImageFormatProperties> properties;
    const auto usage = attachment_usage(src.aspect);
    auto [it, fresh] = properties.try_emplace({format, usage});
    if (fresh && p_vkGetPhysicalDeviceImageFormatProperties(vk_physical(), format, VK_IMAGE_TYPE_2D,
                                                           VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                                           &it->second) != VK_SUCCESS)
        it->second = {};
    const auto& p = it->second;
    return (p.sampleCounts & samples) && src.width <= p.maxExtent.width && src.height <= p.maxExtent.height;
}

void destroy_sidecar(GpuImage& img) {
    const VkDevice device = vk_device();
    if (img.view) p_vkDestroyImageView(device, img.view, nullptr);
    if (img.image) p_vkDestroyImage(device, img.image, nullptr);
    if (img.memory) p_vkFreeMemory(device, img.memory, nullptr);
}

bool create_sidecar(GpuImage& src, VkFormat format, VkSampleCountFlagBits samples) {
    const VkDevice device = vk_device();
    auto image = std::make_shared<GpuImage>();
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {src.width, src.height, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = samples;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = attachment_usage(src.aspect);
    if (p_vkCreateImage(device, &ci, nullptr, &image->image) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    p_vkGetImageMemoryRequirements(device, image->image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {src.aspect, 0, 1, 0, 1};
    if (req.size > kSidecarBudget - g_sidecar_bytes ||
        p_vkAllocateMemory(device, &ai, nullptr, &image->memory) != VK_SUCCESS ||
        p_vkBindImageMemory(device, image->image, image->memory, 0) != VK_SUCCESS ||
        p_vkCreateImageView(device, &view, nullptr, &image->view) != VK_SUCCESS) {
        destroy_sidecar(*image);
        return false;
    }

    if (src.msaa) retire_gpu_image(std::move(src.msaa));
    g_sidecar_bytes += req.size;
    image->width = src.width;
    image->height = src.height;
    image->format = format;
    image->aspect = src.aspect;
    image->samples = samples;
    image->msaa_bytes = req.size;
    src.msaa = std::move(image);
    src.msaa_valid = false;
    return true;
}

bool matching(const GpuImage& src, VkFormat format, VkSampleCountFlagBits samples) {
    return src.msaa && src.msaa->format == format && src.msaa->samples == samples;
}

bool ready(const GpuImage& src, VkFormat format, VkSampleCountFlagBits samples, VkExtent2D extent,
           bool loads_previous) {
    if (src.view_type != VK_IMAGE_VIEW_TYPE_2D || src.levels != 1 || src.layers != 1 ||
        src.width != extent.width || src.height != extent.height)
        return false;
    if (loads_previous) return src.msaa_valid && matching(src, format, samples);
    return supports_image(src, format, samples);
}

void set_resolve(VkRenderingAttachmentInfo& attachment, const GpuImage& sidecar, VkResolveModeFlagBits mode) {
    attachment.resolveMode = mode;
    attachment.resolveImageView = attachment.imageView;
    attachment.resolveImageLayout = attachment.imageLayout;
    attachment.imageView = sidecar.view;
    attachment.imageLayout = sidecar.layout;
}
}  // namespace

void init_msaa_caps() {
    if (!p_vkGetPhysicalDeviceProperties2 || !p_vkCmdBeginRendering || render_pass_objects()) return;
    VkPhysicalDeviceDepthStencilResolveProperties resolve{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &resolve};
    p_vkGetPhysicalDeviceProperties2(vk_physical(), &props);
    if (!(resolve.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) ||
        !(resolve.supportedStencilResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT))
        return;
    const auto& lim = props.properties.limits;
    const unsigned mask = kSampleCounts & lim.framebufferColorSampleCounts & lim.framebufferDepthSampleCounts &
                          lim.framebufferStencilSampleCounts;
    g_sample_mask.store(mask, std::memory_order_release);
}

unsigned msaa_sample_mask() { return g_sample_mask.load(std::memory_order_acquire); }

VkSampleCountFlagBits prepare_msaa_pass(VkCommandBuffer cb, GpuImage* const (&images)[9],
                                        int requested, uint32_t count, const VkFormat* formats, VkExtent2D extent,
                                        VkRenderingAttachmentInfo* colors, VkRenderingAttachmentInfo& depth,
                                        VkRenderingAttachmentInfo& stencil) {
    if (requested < 2 || (requested & (requested - 1)) || !(msaa_sample_mask() & unsigned(requested)))
        return VK_SAMPLE_COUNT_1_BIT;
    const auto samples = VkSampleCountFlagBits(requested);
    GpuImage* dimg = depth.imageView ? images[8] : nullptr;
    bool has_color = false;
    // Geometry can load previously rendered color. Expand that color to every
    // sample on first use, then keep the individual samples in later passes.
    // Color-only effects can continue a geometry pass's sidecar.
    for (uint32_t k = 0; k < count; ++k) {
        if (!colors[k].imageView) continue;
        const auto& src = *images[k];
        for (uint32_t prior = 0; prior < k; ++prior)
            if (colors[prior].imageView && images[prior] == images[k]) return VK_SAMPLE_COUNT_1_BIT;
        if (!color_resolve_supported(formats[k]) || !ready(src, formats[k], samples, extent, !dimg))
            return VK_SAMPLE_COUNT_1_BIT;
        has_color = true;
    }
    if (dimg) {
        const bool load_depth = (dimg->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && depth.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
        const bool load_stencil = (dimg->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && stencil.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
        // Catherine clears its main depth/stencil in a separate pass before
        // any geometry. This gives subsequent LOADs the exact per-sample data.
        if (!has_color && (!(dimg->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) || load_depth || load_stencil))
            return VK_SAMPLE_COUNT_1_BIT;
        if (!ready(*dimg, dimg->format, samples, extent, load_depth || load_stencil)) return VK_SAMPLE_COUNT_1_BIT;
    } else if (!has_color) {
        return VK_SAMPLE_COUNT_1_BIT;
    }
    for (uint32_t k = 0; k < count; ++k)
        if (colors[k].imageView && !matching(*images[k], formats[k], samples) &&
            !create_sidecar(*images[k], formats[k], samples))
            return VK_SAMPLE_COUNT_1_BIT;
    if (dimg && !matching(*dimg, dimg->format, samples) && !create_sidecar(*dimg, dimg->format, samples))
        return VK_SAMPLE_COUNT_1_BIT;

    for (uint32_t k = 0; k < count; ++k) {
        if (!colors[k].imageView) continue;
        const auto& source = *images[k];
        auto& sidecar = *source.msaa;
        transition(cb, sidecar, VK_IMAGE_LAYOUT_GENERAL);
        if (!source.msaa_valid && colors[k].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
            !seed_msaa_color(cb, colors[k], sidecar))
            return VK_SAMPLE_COUNT_1_BIT;
    }
    // Commit the rewritten attachment descriptors only when all loads can be preserved.
    for (uint32_t k = 0; k < count; ++k) {
        if (!colors[k].imageView) continue;
        const auto& sidecar = *images[k]->msaa;
        set_resolve(colors[k], sidecar, VK_RESOLVE_MODE_AVERAGE_BIT);
    }
    if (dimg) {
        auto& sidecar = *dimg->msaa;
        transition(cb, sidecar, VK_IMAGE_LAYOUT_GENERAL);
        set_resolve(depth, sidecar, VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
        set_resolve(stencil, sidecar, VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
    }
    return samples;
}

void free_msaa_attachment(GpuImage& img) {
    // The caller has retired all submits referencing this image and its sidecar.
    if (img.msaa) {
        destroy_sidecar(*img.msaa);
        g_sidecar_bytes -= img.msaa->msaa_bytes;
        img.msaa.reset();
    }
    g_sidecar_bytes -= img.msaa_bytes;
}
}  // namespace nx::nvn
