// Pass-local pipeline-key fields only. Included inside nx::nvn's private namespace.
struct PipelineAttachmentKey {
    std::array<VkFormat, 8> colors{};
    VkFormat depth = VK_FORMAT_UNDEFINED;
    uint32_t ufloat = 0;
    bool operator==(const PipelineAttachmentKey&) const = default;

    void apply(PipelineKey& key) const {
        std::copy(colors.begin(), colors.end(), key.color_formats);
        key.depth_format = depth;
        key.ufloat_colors = ufloat;
    }
};

template <typename ImageLookup, typename ViewFormat>
PipelineAttachmentKey resolve_pipeline_attachment_key(const Targets& t, const Targets& effective,
                                                       ImageLookup image, ViewFormat view,
                                                       VkFormat unused) {
    PipelineAttachmentKey value;
    for (uint32_t k = 0; k < t.color_count; ++k) {
        const auto* img = t.colors[k] ? image(t.colors[k]) : nullptr;
        value.colors[k] = img ? view(*img, t.views.format[k]) : unused;
        if (img && (img->nvn_format == 0x3f || t.views.format[k] == 0x3f)) value.ufloat |= 1u << k;
    }
    const auto* depth = effective.depth ? image(effective.depth) : nullptr;
    value.depth = depth && depth->aspect != VK_IMAGE_ASPECT_COLOR_BIT ? depth->format : VK_FORMAT_UNDEFINED;
    return value;
}

struct PassPipelineAttachmentCache {
    PipelineAttachmentKey key;
    bool valid = false;
    void invalidate() { valid = false; }
};
