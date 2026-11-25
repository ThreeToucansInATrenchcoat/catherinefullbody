// NVN copies between textures (docs/NVN.md, N6). nvnCommandBufferCopyTextureToTexture(cmd, const NVNtexture* src,
// const NVNtextureView* srcView, const NVNcopyRegion* srcRegion, const NVNtexture* dst, const NVNtextureView* dstView,
// const NVNcopyRegion* dstRegion, int copyFlags) (Wonder's call sites). NVNcopyRegion {int x, y, z, width, height,
// depth}; for array textures z/depth are layers. Flags: 1 linear filter, 4/8/16 mirror x/y/z (2 selects the 2D
// engine: no difference here). Replayed as a vkCmdCopyImage when formats and sizes match, else a vkCmdBlitImage.
// Wonder copies its frame into a texture for screen transitions (the opening's cloud wipe).
// Copies within one image (another level or layer, or another rectangle of the same one: blur chains, mips, atlases;
// textures over one storage share an image since #36) are replayed with the image in GENERAL, as Vulkan allows when
// the regions do not overlap; overlapping ones are dropped (G10). NX_COPY_NO_SAME_IMAGE=1: all dropped, the old way.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "nvn_astc.h"
#include "nvn_pipeline.h"
#include "nvn_vk.h"
#include "nvn_controller_art.h"
#include "native_upload.h"
#include "native_transfers.h"

namespace nx::nvn {

using namespace vk;

namespace {

using CopyInfo=native_transfers::Legacy;

void CopyTextureToTexture(CPU& c) {
    CopyInfo info;
    info.src_view = read_view(c, c.x[2]);
    info.dst_view = read_view(c, c.x[5]);
    for (int k = 0; k < 6; k++) {
        info.src[k] = c.x[3] ? ld<int32_t>(c.x[3] + 4ull * k) : 0;
        info.dst[k] = c.x[6] ? ld<int32_t>(c.x[6] + 4ull * k) : 0;
    }
    info.flags = uint32_t(c.x[7]);
    Cmd cmd{Cmd::CopyTextureToTexture};
    cmd.a[0] = c.x[1], cmd.a[1] = c.x[4];
    std::vector<uint8_t> native_blob;
    if(native_transfers::attach(cmd,info,native_blob))record(c.x[0],cmd,native_blob.data(),native_blob.size());
    else record(c.x[0],cmd,&info,sizeof info);
}

struct TextureRegion {
    ViewInfo view;
    int32_t region[6];
};

TextureRegion read_region(CPU& c, uint64_t view, uint64_t region) {
    TextureRegion info{read_view(c, view), {}};
    for (int i = 0; i < 6; ++i) info.region[i] = ld<int32_t>(region + 4ull * i);
    return info;
}

void CopyBufferToTexture(CPU& c) {
    if (!c.x[4]) return;
    const TextureRegion info = read_region(c, c.x[3], c.x[4]);
    Cmd cmd{Cmd::CopyBufferToTexture};
    cmd.a[0] = c.x[1];  // GPU address of the source bytes, read at submit
    cmd.a[1] = c.x[2];  // destination texture
    cmd.mask = uint32_t(c.x[5]);
    if(!native_upload::mode()){record(c.x[0],cmd,&info,sizeof info);return;}
    const auto native=native_upload::prepare(info.view,info.region);
    struct Packet { TextureRegion legacy; native_upload::Intent intent; } packet{};
    packet.legacy=info;packet.intent=native;
    cmd.a[8]=native_upload::tag;
    record(c.x[0],cmd,&packet,sizeof packet);
}

void copy_buffer_to_texture(VkCommandBuffer cb, const Cmd& cmd, const CmdList& list) {
    struct Packet { TextureRegion legacy; native_upload::Intent intent; };
    if(cmd.blob>=list.blobs.size())return;
    const auto& blob=list.blobs[cmd.blob];
    const bool native=cmd.a[8]==native_upload::tag&&blob.size()==sizeof(Packet);
    if(!native&&blob.size()!=sizeof(TextureRegion))return;
    TextureRegion info{};std::memcpy(&info,blob.data(),sizeof info);
    native_upload::Intent intent{};
    if(native)std::memcpy(&intent,blob.data()+offsetof(Packet,intent),sizeof intent);
    const auto* texture = get<Texture>(cmd.a[1]);
    auto* image = texture ? image_of(cmd.a[1]) : nullptr;
    if (!image) return;
    auto [x, y, z, width, height, depth] = info.region;
    if (texture->target == 3) {  // NVN 1D arrays select layers along Y
        z = y;
        depth = height;
        y = 0;
        height = 1;
    }

    const uint32_t level = info.view.base_level;
    if (level >= image->levels || x < 0 || y < 0 || z < 0 || width <= 0 || height <= 0 || depth <= 0) return;
    const uint32_t mip_width = std::max(1u, image->width >> level);
    const uint32_t mip_height = std::max(1u, image->height >> level);
    const bool volume = image->view_type == VK_IMAGE_VIEW_TYPE_3D;
    const uint64_t slice = uint64_t(z) + (volume ? 0 : info.view.min_layer);
    const uint32_t slices = volume ? std::max(1u, image->depth >> level) : image->layers;
    if (uint64_t(x) + width > mip_width || uint64_t(y) + height > mip_height || slice + depth > slices ||
        (info.view.layers && !volume && uint64_t(z) + depth > info.view.layers))
        return;

    const uint32_t format = info.view.format ? info.view.format : texture->format;
    const bool astc = astc_decoded(format);
    const bool bc = bc_host_decode(format);
    uint32_t bw, bh, bytes;
    format_block(format, bw, bh, bytes);
    const uint32_t host_bytes = vk_block_bytes(image->format);
    const uint32_t decoded_bytes = astc ? 4 : bc ? bc_decoded_bytes(format) : 0;
    const bool different_format = info.view.format && format != texture->format &&
                                  vk_format(format) != image->format;
    if ((cmd.mask & (4 | 8 | 16)) || texture->samples > 1 || image->aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
        different_format || (host_bytes && host_bytes != (decoded_bytes ? decoded_bytes : bytes))) {
        static unsigned reported = 0;
        if (reported++ < 8)
            log("nvn: unsupported buffer-to-texture copy (format %#x, host %d, flags %#x)", format, int(image->format), cmd.mask);
        return;
    }

    if (uint32_t(x) % bw || uint32_t(y) % bh ||
        (uint32_t(width) % bw && uint32_t(x + width) != mip_width) ||
        (uint32_t(height) % bh && uint32_t(y + height) != mip_height))
        return;
    const uint64_t row_bytes = ((uint64_t(width) + bw - 1) / bw) * bytes;
    const uint64_t rows = (uint64_t(height) + bh - 1) / bh;
    const uint64_t row_stride = cmd.a[2] ? cmd.a[2] : row_bytes;
    const uint64_t max_staged = 256ull << 20;
    if (row_stride < row_bytes || row_stride > max_staged / rows) return;
    const uint64_t image_stride = cmd.a[3] ? cmd.a[3] : row_stride * rows;
    const uint64_t image_bytes = (rows - 1) * row_stride + row_bytes;
    if (image_stride < image_bytes || image_stride > max_staged / uint32_t(depth)) return;
    const uint64_t needed = (uint32_t(depth) - 1) * image_stride + image_bytes;

    uint64_t pool_offset = 0;
    const MemoryPool* pool = pool_at_gpu(cmd.a[0], &pool_offset);
    if (!pool || needed > pool->size - pool_offset) {
        static unsigned reported = 0;
        if (reported++ < 8) log("nvn: buffer-to-texture source %#llx is outside its pool", (unsigned long long)cmd.a[0]);
        return;
    }
    const uint64_t packed_size = row_bytes * rows * uint32_t(depth);
    const uint64_t upload_size = decoded_bytes ? uint64_t(width) * height * depth * decoded_bytes : packed_size;
    if (upload_size > max_staged) return;
    // A dedicated buffer also aligns packed RGB formats, whose byte count does not divide the ring's alignment.
    Upload staging = upload_size >= (1u << 20) || (bytes & (bytes - 1))
                         ? staging_alloc(upload_size)
                         : upload_alloc(upload_size, 16);
    std::vector<uint8_t> compressed_data(decoded_bytes ? packed_size : 0);
    uint8_t* packed = decoded_bytes ? compressed_data.data() : staging.ptr;
    for (uint32_t layer = 0; layer < uint32_t(depth); ++layer) {
        for (uint64_t row = 0; row < rows; ++row) {
            const uint64_t offset = pool_offset + layer * image_stride + row * row_stride;
            const uint64_t host = pool_host(*pool, offset);
            if (!host || (!pool->memory && pool_host(*pool, offset + row_bytes - 1) != host + row_bytes - 1)) return;
            std::memcpy(packed + (layer * rows + row) * row_bytes, reinterpret_cast<const void*>(host), row_bytes);
        }
    }
    if (astc)
        astc_decode_image(packed, width, height, depth, bw, bh, format >= 0x87, staging.ptr);
    else if (bc)
        bc_decode(format, packed, width, height, depth, staging.ptr);

    image = ensure_contents(cb, cmd.a[1]);
    if (!image) return;
    image->controller_art_id = 0;
    if (!decoded_bytes && !level && !x && !y && !slice && depth == 1 &&
        uint32_t(width) == image->width && uint32_t(height) == image->height)
        controller_art_identify(*image, packed, packed_size);
    // Make earlier writes visible even if initialization left the image in TRANSFER_DST.
    if (image->layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        transition(cb, *image, VK_IMAGE_LAYOUT_GENERAL);
    transition(cb, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    const bool use_native=native&&native_upload::mode();
    if(use_native) {
        copy=intent.regions[texture->target==3?2:volume?1:0];
        copy.bufferOffset=staging.offset;copy.imageSubresource.aspectMask=image->aspect;
    }
    if(!use_native||native_upload::mode()==2) {
        VkBufferImageCopy expected{};
        expected.bufferOffset=staging.offset;
        expected.imageSubresource={image->aspect,level,volume?0u:uint32_t(slice),volume?1u:uint32_t(depth)};
        expected.imageOffset={x,y,volume?int32_t(slice):0};
        expected.imageExtent={uint32_t(width),uint32_t(height),volume?uint32_t(depth):1u};
        if(use_native&&std::memcmp(&expected,&copy,sizeof copy))fatal("Native texture upload region differs from original");
        if(!use_native)copy=expected;
    }
    if(use_native){static uint64_t n=0;if(++n==1||n%1000==0)log("Native texture upload regions consumed: %llu mode %d",(unsigned long long)n,native_upload::mode());}
    p_vkCmdCopyBufferToImage(cb, staging.buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image->texels = true;  // later CPU pool uploads must not overwrite these GPU-side texels
    transition(cb, *image, image->rendered ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    static unsigned copied = 0;
    if (++copied <= 8)
        log("nvn: copied buffer to texture %llx level %u, %dx%dx%d format %#x", (unsigned long long)cmd.a[1],
            level, width, height, depth, format);
}

void ClearTexture(CPU& c) {
    if (!c.x[3] || !c.x[4] || !(c.x[5] & 0xf)) return;
    const TextureRegion info = read_region(c, c.x[2], c.x[3]);
    Cmd cmd{Cmd::ClearTexture};
    cmd.a[0] = c.x[1];
    cmd.mask = uint32_t(c.x[5]);
    for (int i = 0; i < 4; ++i) cmd.f[i] = ld<float>(c.x[4] + 4ull * i);
    if(!native_upload::mode()){record(c.x[0],cmd,&info,sizeof info);return;}
    struct Packet {TextureRegion legacy;native_upload::ClearIntent intent;} packet{};
    packet.legacy=info;packet.intent.geometry=native_upload::prepare(info.view,info.region);
    std::memcpy(&packet.intent.color,cmd.f,sizeof packet.intent.color);
    cmd.a[8]=native_upload::clear_tag;record(c.x[0],cmd,&packet,sizeof packet);
}

void clear_texture(VkCommandBuffer cb, const Cmd& cmd, const CmdList& list) {
    struct Packet {TextureRegion legacy;native_upload::ClearIntent intent;};
    if(cmd.blob>=list.blobs.size())return;
    const auto& blob=list.blobs[cmd.blob];
    const bool native=cmd.a[8]==native_upload::clear_tag&&blob.size()==sizeof(Packet);
    if(!native&&blob.size()!=sizeof(TextureRegion))return;
    TextureRegion info{};std::memcpy(&info,blob.data(),sizeof info);
    native_upload::ClearIntent intent{};
    if(native)std::memcpy(&intent,blob.data()+offsetof(Packet,intent),sizeof intent);
    const auto* texture = get<Texture>(cmd.a[0]);
    auto* image = texture ? image_of(cmd.a[0]) : nullptr;
    if (!image) return;
    auto [x, y, z, width, height, depth] = info.region;
    if (texture->target == 3) {
        z = y;
        depth = height;
        y = 0;
        height = 1;
    }

    const uint32_t level = info.view.base_level;
    if (level >= image->levels || z < 0 || depth <= 0) return;
    const bool volume = image->view_type == VK_IMAGE_VIEW_TYPE_3D;
    const uint64_t layer = volume ? 0 : uint64_t(info.view.min_layer) + z;
    const bool full_depth = !volume || (z == 0 && uint32_t(depth) == std::max(1u, image->depth >> level));
    if ((cmd.mask & 0xf) != 0xf || image->aspect != VK_IMAGE_ASPECT_COLOR_BIT || !vk_block_bytes(image->format) ||
        x || y || !full_depth ||
        width != int32_t(std::max(1u, image->width >> level)) ||
        height != int32_t(std::max(1u, image->height >> level)) ||
        (!volume && (layer + depth > image->layers || (info.view.layers && uint64_t(z) + depth > info.view.layers))) ||
        (info.view.format && view_format(*image, info.view.format) != image->format)) {
        static unsigned reported = 0;
        if (reported++ < 8) log("nvn: unsupported ClearTexture region, format or channel mask");
        return;
    }
    image = ensure_contents(cb, cmd.a[0]);
    if (!image) return;
    if (image->layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        transition(cb, *image, VK_IMAGE_LAYOUT_GENERAL);
    transition(cb, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue color{};VkImageSubresourceRange range{};
    const bool use_native=native&&native_upload::mode();
    if(use_native){color=intent.color;range=native_upload::clear_range(intent.geometry,texture->target==3?2:volume?1:0,image->aspect);}
    if(!use_native||native_upload::mode()==2){
        const VkClearColorValue expected_color{{cmd.f[0],cmd.f[1],cmd.f[2],cmd.f[3]}};
        const VkImageSubresourceRange expected_range{image->aspect,level,1,uint32_t(layer),volume?1u:uint32_t(depth)};
        if(use_native&&(std::memcmp(&color,&expected_color,sizeof color)||std::memcmp(&range,&expected_range,sizeof range)))fatal("Native texture clear differs from original");
        if(!use_native){color=expected_color;range=expected_range;}
    }
    if(use_native){static uint64_t n=0;if(++n==1||n%1000==0)log("Native texture clears consumed: %llu mode %d",(unsigned long long)n,native_upload::mode());}
    p_vkCmdClearColorImage(cb, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    image->rendered = true;
    transition(cb, *image, VK_IMAGE_LAYOUT_GENERAL);
    static unsigned cleared = 0;
    if (++cleared <= 4)
        log("nvn: cleared texture %llx level %u, %dx%dx%d to (%g, %g, %g, %g)", (unsigned long long)cmd.a[0],
            level, width, height, depth, cmd.f[0], cmd.f[1], cmd.f[2], cmd.f[3]);
}

bool compressed(VkFormat f) {
    return (f >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && f <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK);
}

// A copy within one image keeps it GENERAL, where transition() records nothing: the writes before it (a pass,
// another copy) and the reads after it need a barrier of their own.
void general_barrier(VkCommandBuffer cb, const GpuImage& img) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = {img.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &b);
}

// Half-open ranges [a0, a0 + an) and [b0, b0 + bn) intersect.
bool ranges_meet(int64_t a0, int64_t an, int64_t b0, int64_t bn) { return a0 < b0 + bn && b0 < a0 + an; }

uint64_t g_same_image = 0, g_same_overlap = 0;

}  // namespace

void replay_copy_compat(VkCommandBuffer cb, const Cmd& c, const CmdList& list) {
    if (c.op == Cmd::ClearTexture) {
        clear_texture(cb, c, list);
        return;
    }
    static const bool off = std::getenv("NX_NO_COPY") != nullptr;  // debug: skip texture copies
    if (off) return;
    if (c.op == Cmd::CopyBufferToTexture) {
        copy_buffer_to_texture(cb, c, list);
        return;
    }
    if (c.blob >= list.blobs.size() || list.blobs[c.blob].size() != sizeof(CopyInfo)) return;
    CopyInfo info;
    std::memcpy(&info, list.blobs[c.blob].data(), sizeof info);
    GpuImage* src = ensure_contents(cb, c.a[0]);
    GpuImage* dst = ensure_contents(cb, c.a[1]);
    if (!src || !dst) return;
    bool same = src == dst;
    static const bool no_same = std::getenv("NX_COPY_NO_SAME_IMAGE") != nullptr;  // bisecting: the old way
    if (same && no_same) return;
    uint32_t sl = std::min(info.src_view.base_level, src->levels - 1), dl = std::min(info.dst_view.base_level, dst->levels - 1);
    bool s3d = src->view_type == VK_IMAGE_VIEW_TYPE_3D, d3d = dst->view_type == VK_IMAGE_VIEW_TYPE_3D;
    auto sub = [](const GpuImage& img, uint32_t level, int32_t z, int32_t depth, uint32_t view_layer, bool is3d) {
        VkImageAspectFlags aspect = img.aspect;
        uint32_t layer = is3d ? 0 : std::min<uint32_t>(uint32_t(std::max(z, 0)) + view_layer, img.layers - 1);
        uint32_t count = is3d ? 1 : std::clamp<uint32_t>(uint32_t(std::max(depth, 1)), 1, img.layers - layer);
        return VkImageSubresourceLayers{aspect, level, layer, count};
    };
    VkImageSubresourceLayers ss = sub(*src, sl, info.src[2], info.src[5], info.src_view.min_layer, s3d);
    VkImageSubresourceLayers ds = sub(*dst, dl, info.dst[2], info.dst[5], info.dst_view.min_layer, d3d);
    int32_t sw = std::max(info.src[3], 1), sh = std::max(info.src[4], 1);
    int32_t dw = std::max(info.dst[3], 1), dh = std::max(info.dst[4], 1);
    if (same) {
        // One subresource and layer range, and the rectangles (and 3D slices) meet: Vulkan forbids it, dropped.
        bool overlap = ss.mipLevel == ds.mipLevel &&
                       ranges_meet(ss.baseArrayLayer, ss.layerCount, ds.baseArrayLayer, ds.layerCount) &&
                       ranges_meet(info.src[0], sw, info.dst[0], dw) && ranges_meet(info.src[1], sh, info.dst[1], dh) &&
                       (!s3d || ranges_meet(info.src[2], std::max(info.src[5], 1), info.dst[2],
                                            std::max(info.dst[5], 1)));
        g_same_image++, g_same_overlap += overlap;
        if (g_same_image == 1 || g_same_image % 1000 == 0 || (overlap && g_same_overlap <= 5))
            log("nvn: %llu copies within one image (%llu overlapping, dropped); this one %llx level %u -> %u, "
                "layer %u -> %u%s",
                (unsigned long long)g_same_image, (unsigned long long)g_same_overlap, (unsigned long long)c.a[0],
                ss.mipLevel, ds.mipLevel, ss.baseArrayLayer, ds.baseArrayLayer, overlap ? " (overlapping)" : "");
        if (overlap) return;
    }
    static const bool trace_cubes = std::getenv("NX_TRACE_CUBES") != nullptr;
    static int cube_logs = 0;
    if (trace_cubes && (dst->view_type == VK_IMAGE_VIEW_TYPE_CUBE || dst->view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) &&
        cube_logs++ < 400)
        log("cubes: copy %llx -> %llx level %u layer %u+%u (z %d depth %d view layer %u) %dx%d", (unsigned long long)c.a[0],
            (unsigned long long)c.a[1], ds.mipLevel, ds.baseArrayLayer, ds.layerCount, info.dst[2], info.dst[5],
            info.dst_view.min_layer, info.dst[3], info.dst[4]);
    VkImageLayout sly = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkImageLayout dly = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition(cb, *src, sly);
    transition(cb, *dst, dly);
    if (same) general_barrier(cb, *src);
    bool mirror = info.flags & (4 | 8 | 16);
    if (src->format == dst->format && sw == dw && sh == dh && !mirror && ss.layerCount == ds.layerCount) {
        VkImageCopy r{};
        r.srcSubresource = ss, r.dstSubresource = ds;
        r.srcOffset = {info.src[0], info.src[1], s3d ? info.src[2] : 0};
        r.dstOffset = {info.dst[0], info.dst[1], d3d ? info.dst[2] : 0};
        r.extent = {uint32_t(sw), uint32_t(sh), s3d ? uint32_t(std::max(info.src[5], 1)) : 1u};
        p_vkCmdCopyImage(cb, src->image, sly, dst->image, dly, 1, &r);
    } else if (!compressed(src->format) && !compressed(dst->format) && src->aspect == dst->aspect) {
        VkImageBlit b{};
        b.srcSubresource = ss, b.dstSubresource = ds;
        ds.layerCount = ss.layerCount = std::min(ss.layerCount, ds.layerCount);
        b.srcSubresource.layerCount = b.dstSubresource.layerCount = ss.layerCount;
        b.srcOffsets[0] = {info.src[0], info.src[1], 0};
        b.srcOffsets[1] = {info.src[0] + sw, info.src[1] + sh, 1};
        b.dstOffsets[0] = {info.dst[0], info.dst[1], 0};
        b.dstOffsets[1] = {info.dst[0] + dw, info.dst[1] + dh, 1};
        if (info.flags & 4) std::swap(b.dstOffsets[0].x, b.dstOffsets[1].x);
        if (info.flags & 8) std::swap(b.dstOffsets[0].y, b.dstOffsets[1].y);
        bool linear = (info.flags & 1) && src->aspect == VK_IMAGE_ASPECT_COLOR_BIT;
        p_vkCmdBlitImage(cb, src->image, sly, dst->image, dly, 1, &b, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    } else {
        static int logged = 0;
        if (logged++ < 5)
            log("nvn: copy %llx -> %llx not replayed (formats %d -> %d, %dx%d -> %dx%d)", (unsigned long long)c.a[0],
                (unsigned long long)c.a[1], int(src->format), int(dst->format), sw, sh, dw, dh);
        return;
    }
    if (same) general_barrier(cb, *dst);
    dst->msaa_valid = false;
    dst->rendered = true;  // GPU-written from now on
}


#include "native_transfer_gpu.inc"

NX_HLE("nvnCommandBufferCopyTextureToTexture", CopyTextureToTexture);
NX_HLE("nvnCommandBufferCopyBufferToTexture", CopyBufferToTexture);
NX_HLE("nvnCommandBufferClearTexture", ClearTexture);

}  // namespace nx::nvn
