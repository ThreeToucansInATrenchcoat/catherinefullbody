#pragma once
#include "catherine_shadow_quality.h"

namespace nx::nvn::shadow_quality {
// GPU/replay owner only. Freeze Auto for the entire submit so all aliases and
// both cascades agree even while the window/config thread changes resolution.
inline unsigned wanted = 0, render_height = 0;
inline void begin_submit() {
    render_height = catherine_resolution_applied_height();
    wanted = catherine_shadow_resolution_for_height(catherine_shadow_resolution_setting(), render_height);
}
inline unsigned requested() { if (!wanted) begin_submit(); return wanted; }
inline bool target(const Texture& t) {
    // The game's two live shadow maps: verified on piano, bar and puzzle traces.
    // Screen depth, light-probe cubes, uploaded colour textures and MSAA targets
    // retain their original allocation/layout.
    return t.format == 0x32 && t.width == 2048 && t.height == 2048 &&
           t.depth <= 1 && t.levels == 1 && t.target == 1 && t.samples <= 1 && !t.stride;
}
inline unsigned smaller(unsigned size) { return size > 4096 ? 4096 : 2048; }
inline bool scaled(const GpuImage& img) {
    return img.shadow_base_width && img.shadow_base_height &&
           (img.width != img.shadow_base_width || img.height != img.shadow_base_height);
}
inline void raster(const GpuImage& img, VkViewport& vp, VkRect2D& sc, bool viewport_explicit, bool scissor_explicit) {
    if (!scaled(img)) return;
    const float sx = float(img.width) / img.shadow_base_width, sy = float(img.height) / img.shadow_base_height;
    if (viewport_explicit) { vp.x *= sx; vp.y *= sy; vp.width *= sx; vp.height *= sy; }
    if (scissor_explicit) {
        sc.offset.x = int32_t(sc.offset.x * sx); sc.offset.y = int32_t(sc.offset.y * sy);
        sc.extent.width = uint32_t(sc.extent.width * sx); sc.extent.height = uint32_t(sc.extent.height * sy);
        sc.offset.x = std::clamp(sc.offset.x, 0, int32_t(img.width));
        sc.offset.y = std::clamp(sc.offset.y, 0, int32_t(img.height));
        sc.extent.width = std::min(sc.extent.width, img.width - uint32_t(sc.offset.x));
        sc.extent.height = std::min(sc.extent.height, img.height - uint32_t(sc.offset.y));
    }
}
inline void rectangle(const GpuImage& img, int32_t* region) {
    if (!scaled(img)) return;
    const unsigned sx = img.width / img.shadow_base_width, sy = img.height / img.shadow_base_height;
    region[0] *= int32_t(sx); region[1] *= int32_t(sy);
    region[3] *= int32_t(sx); region[4] *= int32_t(sy);
}
inline void transfer(const GpuImage& src, const GpuImage& dst,
                     VkImageCopy& copy, VkImageBlit& blit, bool& direct) {
    if (!scaled(src) && !scaled(dst)) return;
    const unsigned sx = src.shadow_base_width ? src.width / src.shadow_base_width : 1;
    const unsigned sy = src.shadow_base_height ? src.height / src.shadow_base_height : 1;
    const unsigned dx = dst.shadow_base_width ? dst.width / dst.shadow_base_width : 1;
    const unsigned dy = dst.shadow_base_height ? dst.height / dst.shadow_base_height : 1;
    copy.srcOffset.x *= int32_t(sx); copy.srcOffset.y *= int32_t(sy);
    copy.dstOffset.x *= int32_t(dx); copy.dstOffset.y *= int32_t(dy);
    copy.extent.width *= sx; copy.extent.height *= sy;
    for (unsigned i=0;i!=2;++i) {
        blit.srcOffsets[i].x *= int32_t(sx); blit.srcOffsets[i].y *= int32_t(sy);
        blit.dstOffsets[i].x *= int32_t(dx); blit.dstOffsets[i].y *= int32_t(dy);
    }
    direct = direct && sx == dx && sy == dy;
}
}
