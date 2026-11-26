// Draws (docs/NVN.md, N6): replays the bind, state and draw commands of nvn_state.cpp into Vulkan. Everything a draw
// reads from guest memory (vertices, indices, uniform and storage buffers, textures) is copied at replay time into
// the submit's upload buffer: the replay runs right when the game submits and is waited for, so the memory holds
// what the GPU would have read. Render targets become a dynamic-rendering pass, begun at the first draw and ended
// before any transfer.
#include "C:/catherine/recomp/Source/imports/timing-audit/perf-2026-11-22/phase_timer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <set>

#include "nvn_draw_state.h"
#include "dlss_backend.h"
#include "engine_vk_indexed.h"
namespace nx::nvn::replay { void raster_dynamic_values(VkCommandBuffer, const uint32_t*); }
#include "nvn_descriptor_bytes.h"
#include <array>
#include <map>
#include <memory>
#include "platform.h"
#include "pose_qa_options.h"
#include "pose_replay_context.h"
#include "producer_raster.h"
#include "native_submit_resources.h"
#include "native_material_plans.h"
#include "producer_pipeline.h"
#include "producer_fixed_pipeline.h"
#include "native_pipeline_definition.h"
#include "native_draw_depth.h"
#include <optional>

namespace nx::nvn {

using namespace nx::vk;

// The replay state shared with nvn_draw_trace.cpp (nvn_draw_state.h).
namespace replay {

State g_state;
Stats g_stats;

const uint8_t* guest_bytes(uint64_t gpu) {
    uint64_t off = 0;
    MemoryPool* p = pool_at_gpu(gpu, &off);
    uint64_t host = p ? pool_host(*p, off) : 0;
    return host ? reinterpret_cast<const uint8_t*>(host) : nullptr;
}

}  // namespace replay

using namespace replay;

namespace {

#include "pass-pipeline-attachments.inc"
PassPipelineAttachmentCache g_pipeline_attachments;

bool cache_pipeline_attachments() {
    static const bool enabled = [] {
        const char* value = std::getenv("NX_PASS_PIPELINE_ATTACHMENTS");
        return value && value[0] == '1';
    }();
    return enabled;
}

PipelineAttachmentKey pipeline_attachments(const Targets& targets, const Targets& effective) {
    auto resolve = [&] {
        return resolve_pipeline_attachment_key(targets, effective,
            [](uint64_t guest) { return image_of(guest); },
            [](const GpuImage& image, uint32_t format) { return view_format(image, format); },
            unused_slot_format());
    };
    if (!cache_pipeline_attachments()) {
        return resolve();
    }
    // Effective targets have already passed the existing targets_same check. A
    // depth detach, format alias, transfer/clear break or pass resume starts a new
    // cache lifetime. Never retain object pointers or values across pass ends.
    if (!g_pipeline_attachments.valid) {
        g_pipeline_attachments.key = resolve();
        g_pipeline_attachments.valid = true;
    }
    static const bool verify = [] {
        const char* value = std::getenv("NX_PASS_PIPELINE_ATTACHMENTS_VERIFY");
        return value && value[0] == '1';
    }();
    if (verify && !(g_pipeline_attachments.key == resolve()))
        gpu_hung("pass pipeline attachment key mismatch");
    return g_pipeline_attachments.key;
}

void keep_msaa_samples(GpuImage& image, int requested) {
    if (!image.msaa || int(image.msaa->samples) == requested) return;
    // A new mode can need this memory before it can allocate a replacement.
    retire_gpu_image(std::move(image.msaa));
    image.msaa_valid = false;
}

bool g_rendering = false;
bool g_render_pass = false;
bool g_attachment_only = false;
int g_msaa_requested = 0;
VkSampleCountFlagBits g_pass_samples = VK_SAMPLE_COUNT_1_BIT;
uint64_t g_attachment_barriers = 0, g_attachment_fallbacks = 0;
bool g_vertex_pending = false;  // a narrow pass-end barrier since the last one that covers vertex stages (G4)
uint64_t g_pass_serial = 0, g_vertex_visible_at = 0;  // pass ends so far; the serial a full barrier covered
uint64_t g_vertex_barriers = 0;

// Off by default (NX_PASS_BARRIER_NARROW=1 turns it on): the first S23 run with it showed a black band over the course
// background in 2 of 28 frames (phone_n6def, 15000 and 16200), a timing-dependent hazard of the DEBUG-2 #71 class.
bool narrow_pass_barrier() {
    static const bool on = std::getenv("NX_PASS_BARRIER_NARROW") != nullptr;
    return on;
}

bool scoped_pass_barriers() {
    static const bool enabled = [] {
        const char* value = std::getenv("CATHERINE_NVN_ATTACHMENT_BARRIERS");
        if (!value || std::strcmp(value, "1") != 0 || !p_vkGetPhysicalDeviceFeatures2 || !vk_physical())
            return false;
        // Device creation enables the queried Vulkan 1.3 features. With
        // synchronization2, equal old/new layouts specify a memory barrier.
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f13};
        p_vkGetPhysicalDeviceFeatures2(vk_physical(), &features);
        return f13.synchronization2 != VK_FALSE;
    }();
    return enabled;
}

uint32_t pass_attachment_barriers(const Targets& targets, GpuImage* const (&images)[9],
                                  VkImageMemoryBarrier (&barriers)[9]) {
    uint32_t count = 0;
    for (uint32_t k = 0; k < 9; ++k) {
        if (k < 8 ? k >= targets.color_count || !targets.colors[k] : !targets.depth) continue;
        const GpuImage* img = images[k];
        if (!img) continue;
        bool color = k < 8;
        if (img->layout != VK_IMAGE_LAYOUT_GENERAL || !img->image || !img->memory ||
            (color ? img->aspect != VK_IMAGE_ASPECT_COLOR_BIT
                   : !(img->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))))
            return 0;
        // A VkImage can be bound through more than one texture view in a pass.
        bool duplicate = false;
        for (uint32_t i = 0; i < count; ++i) duplicate |= barriers[i].image == img->image;
        if (duplicate) continue;
        auto& barrier = barriers[count++];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = color ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                      : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = img->image;
        barrier.subresourceRange = {img->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    }
    return count;
}

bool shader_scope_cached(const ShaderInfo* shader) {
    if (!shader) return true;
    struct Proof { const ShaderInfo* shader; bool clean; };
    static std::array<Proof, 256> cache{};
    auto ptr = reinterpret_cast<uintptr_t>(shader);
    const size_t index = ((ptr >> 4) ^ (ptr >> 12)) & (cache.size() - 1);
    auto& slot = cache[index];
    if (slot.shader == shader) return slot.clean;
    static const bool two_way = [] {
        const char* setting = std::getenv("NX_SHADER_SCOPE_TWO_WAY");
        return setting && setting[0] == '1' && setting[1] == '\0';
    }();
    // Keep both immutable proofs when two shaders share a slot.
    if (two_way) {
        static std::array<Proof, 256> alternate{};
        auto& other = alternate[index];
        if (other.shader == shader) return other.clean;
        other = slot;
    }
    slot = {shader, shader_writes_only_attachments(*shader)};
    return slot.clean;
}

// A vertex shader sampling a rendered image after a narrow pass-end barrier: the pass ends (it cannot take a barrier
// inside) and the writes so far are made visible to the vertex stages.
void vertex_read_barrier(VkCommandBuffer cb, const ShaderInfo* vs);  // the open pass is a render pass object (nvn_renderpass.cpp), not dynamic rendering
Targets g_pass;           // what the open pass renders to
GpuImage* g_pass_images[9];  // their images (8: depth), resolved once per pass for gather_textures
uint32_t g_pass_written = 0;  // targets the open pass's draws wrote: bit c colour c, bit 8 depth
uint64_t g_pass_ends[8];  // passes ended, by draw_end_rendering reason
uint64_t g_submit_ns = 0;  // when the submit being replayed started
VkExtent2D g_pass_extent{};

// What this submit's command buffer already has bound and set. Vulkan keeps the pipeline, vertex and index buffers
// and dynamic state of a command buffer across its dynamic-rendering passes, and most draws repeated what the draw
// before them set (the vertex streams and the viewport of a whole pass): only what differs is recorded. Forgotten
// at each submit, on another command buffer, and when something else binds a pipeline there (the masked clear's
// fullscreen draw, nvn_mask_clear.cpp). NX_DRAW_SLOW: forgotten before every draw.
struct Bound {
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    uint32_t vertex_set = 0;  // bit s: binding s holds vertex[s]
    Upload vertex[16];
    bool has_index = false, has_dynamic = false;
    Upload index;
    VkIndexType index_type = VK_INDEX_TYPE_UINT16;
    VkViewport viewport{};
    VkRect2D scissor{};
    float blend[4] = {};
    uint32_t raster[10] = {};  // raster_dynamic's inputs: stencil reference and masks per face, the depth bias
} g_bound;
uint64_t g_state_set = 0, g_state_kept = 0;  // state commands recorded and left out, for the log

template <typename T>
bool same_bytes(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }

void bind_vertex(VkCommandBuffer cb, uint32_t binding, const Upload& u) {
    Upload& now = g_bound.vertex[binding];
    if ((g_bound.vertex_set >> binding & 1) && now.buffer == u.buffer && now.offset == u.offset) {
        g_state_kept++;
        return;
    }
    VkDeviceSize off = u.offset;
    p_vkCmdBindVertexBuffers(cb, binding, 1, &u.buffer, &off);
    now = u, g_bound.vertex_set |= 1u << binding, g_state_set++;
}


using VulkanRasterState = producer_raster::DynamicState;

VulkanRasterState vulkan_raster_state(const State& s) {
    VulkanRasterState state{};
    state.raster[0] = s.stencil_ref[0];
    state.raster[1] = s.stencil_ref[1];
    state.raster[2] = s.stencil_compare_mask[0];
    state.raster[3] = s.stencil_compare_mask[1];
    state.raster[4] = s.stencil_write_mask[0];
    state.raster[5] = s.stencil_write_mask[1];
    std::memcpy(state.raster + 6, &s.bias_factor, 4);
    std::memcpy(state.raster + 7, &s.bias_units, 4);
    std::memcpy(state.raster + 8, &s.bias_clamp, 4);
    state.raster[9] = native_draw_depth::read(s.depth).test && native_draw_depth::read(s.depth).slack;
    std::memcpy(state.blend, s.blend_color, sizeof state.blend);
    return state;
}

void set_vulkan_dynamic(VkCommandBuffer cb, const VkViewport& vp, const VkRect2D& sc,
                        const VulkanRasterState& state) {
    Bound& b = g_bound;
    const bool all = !b.has_dynamic;
    b.has_dynamic = true;
    auto differs = [&](bool d) { return (all || d) ? (g_state_set++, true) : (g_state_kept++, false); };
    if (differs(!same_bytes(b.viewport, vp))) p_vkCmdSetViewport(cb, 0, 1, &vp), b.viewport = vp;
    if (differs(!same_bytes(b.scissor, sc))) p_vkCmdSetScissor(cb, 0, 1, &sc), b.scissor = sc;
    if (differs(std::memcmp(b.blend, state.blend, sizeof b.blend) != 0))
        p_vkCmdSetBlendConstants(cb, state.blend), std::memcpy(b.blend, state.blend, sizeof b.blend);
    if (differs(std::memcmp(b.raster, state.raster, sizeof b.raster) != 0))
        raster_dynamic_values(cb, state.raster), std::memcpy(b.raster, state.raster, sizeof b.raster);
}

void set_dynamic(VkCommandBuffer cb, const VkViewport& vp, const VkRect2D& sc) {
    set_vulkan_dynamic(cb, vp, sc, vulkan_raster_state(g_state));
}

void fit_screen_gradient(DrawInputs& in, const Cmd& cmd, uint32_t mode, bool indexed,
                         const ShaderInfo& vs, const ShaderInfo* fs, VkExtent2D extent) {
    constexpr float width = 1920.0f, height = 1080.0f;
    if (extent.width <= width || extent.height <= height || !indexed || mode != 5 ||
        cmd.i[0] != 126 || cmd.i[1] != 0 || cmd.i[3] != 1 || !fs ||
        vs.hash != "52421ce0a854d776" || fs->hash != "8b7d1022ca3c42ff")
        return;
    const BlendTarget& blend = g_state.blend[0];
    if (!g_state.color.blend_enable[0] || blend.src_rgb != 5 || blend.dst_rgb != 2 ||
        (blend.eq_rgb != 1 && blend.eq_rgb != 3) ||
        !g_state.has_viewport || g_state.viewport[0] || g_state.viewport[1] ||
        g_state.viewport[2] != int32_t(extent.width) || g_state.viewport[3] != int32_t(extent.height) ||
        g_state.attribs.size() < 2)
        return;
    const auto& position = g_state.attribs[0];
    const auto& color = g_state.attribs[1];
    const uint32_t stream = position.stream;
    struct Vertex { float x, y, z; uint32_t color; };
    std::array<Vertex, 72> grid;
    if (stream >= 15 || stream >= g_state.streams.size() || !(in.used & (1u << stream)) ||
        position.format != 34 || position.offset || color.stream != stream || color.format != 37 ||
        color.offset != 12 || g_state.streams[stream].stride != sizeof(Vertex) || g_state.streams[stream].divisor ||
        g_state.vertex_buffers[stream].size < sizeof grid)
        return;
    const auto* indices = in.index.ptr ? in.index.ptr : guest_bytes(cmd.a[0]);
    if (!indices) return;
    const size_t index_bytes = in.index.ptr ? (in.index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4) : index_size(cmd.mask);
    for (int i = 0; i < cmd.i[0]; ++i) {
        uint32_t index = 0;
        std::memcpy(&index, indices + i * index_bytes, index_bytes);
        if (index >= grid.size()) return;
    }
    const auto* source = in.vertex[stream].ptr;
    if (!source) source = guest_bytes(g_state.vertex_buffers[stream].address);
    if (!source) return;
    std::memcpy(grid.data(), source, sizeof grid);
    auto at = [](const Vertex& v, float x, float y) {
        return std::abs(v.x - x) <= 0.01f && std::abs(v.y - y) <= 0.01f;
    };
    if (!at(grid[0], 0, 0) || !at(grid[8], width, 0) ||
        !at(grid[63], 0, height) || !at(grid.back(), width, height))
        return;
    // Additive lighting and subtractive vignette meshes are authored at 1080p.
    // Fit their geometry to the current pixel-space projection, preserving the gradient.
    const float sx = extent.width / grid.back().x, sy = extent.height / grid.back().y;
    for (auto& vertex : grid) {
        vertex.x *= sx;
        vertex.y *= sy;
    }
    in.vertex[stream] = upload(grid.data(), sizeof grid, 16);
}


// cmd148: lazy MSAA resolves (see build148.py).
namespace lazy148 {
struct Group {
    GpuImage* images[9]{};                 // colours 0..7, depth/stencil 8
    VkImageView sidecar[9]{};              // the sidecar views the resolve reads (checked at flush)
    VkRenderingAttachmentInfo colors[8]{};
    VkRenderingAttachmentInfo depth{}, stencil{};
    uint32_t color_count = 0;
    uint32_t pending = 0;                  // bit k colour k, bit 8 depth, bit 9 stencil
    VkExtent2D extent{};
};
std::vector<Group> g_groups;
Group g_current;
bool g_current_on = false;
uint64_t g_deferred = 0, g_dropped = 0, g_flushed[8] = {}, g_flush_passes = 0, g_anomalies = 0, g_open_pending = 0;
enum Cause { BeginOther = 0, BeginSingle = 1, Dispatch = 2, ClearCopy = 3, SubmitEnd = 4, Invalid = 5, Sampled = 6 };
uint64_t g_kept153 = 0, g_draw_breaks153 = 0, g_clear_dropped157 = 0;
bool copy159() {
    static const bool v = [] { const char* e = std::getenv("CATHERINE_PARALLEL159"); return !(e && e[0] == '0'); }();
    return v;
}
bool clear155() {
    static const bool v = [] { const char* e = std::getenv("CATHERINE_PARALLEL155"); return !(e && e[0] == '0'); }();
    return v;
}
bool keep153() {
    static const bool v = [] { const char* e = std::getenv("CATHERINE_PARALLEL153"); return !(e && e[0] == '0'); }();
    return v;
}
bool on() {
    static const bool v = [] { const char* e = std::getenv("CATHERINE_PARALLEL148"); return !(e && e[0] == '0'); }();
    return v;
}
void report() {
    static uint64_t last = 0;
    const uint64_t frame = nx::g_frame.load(std::memory_order_relaxed);
    if (frame < last + 600) return;
    last = frame;
    log("lazy148: deferred %llu, dropped %llu, flushed: begin-other %llu begin-single %llu dispatch %llu clear/copy %llu "
        "submit-end %llu invalid %llu sampled %llu (%llu resolve passes), anomalies %llu, pending-while-open %llu; "
        "cmd153: depth/stencil kept past other passes %llu, draw breaks %llu; cmd157: dropped by clears %llu",
        (unsigned long long)g_deferred, (unsigned long long)g_dropped, (unsigned long long)g_flushed[0],
        (unsigned long long)g_flushed[1], (unsigned long long)g_flushed[2], (unsigned long long)g_flushed[3],
        (unsigned long long)g_flushed[4], (unsigned long long)g_flushed[5], (unsigned long long)g_flushed[6],
        (unsigned long long)g_flush_passes, (unsigned long long)g_anomalies, (unsigned long long)g_open_pending,
        (unsigned long long)g_kept153, (unsigned long long)g_draw_breaks153, (unsigned long long)g_clear_dropped157);
}
// Resolve the slots `mask` of g: an empty pass over the sidecars with the resolve the multisample pass would have done.
void resolve(VkCommandBuffer cb, Group& g, uint32_t mask, Cause cause) {
    mask &= g.pending;
    for (uint32_t k = 0; k < 10; k++) {
        if (!(mask >> k & 1)) continue;
        GpuImage* img = g.images[k < 8 ? k : 8];
        if (!img || !img->msaa || img->msaa->view != g.sidecar[k < 8 ? k : 8]) { mask &= ~(1u << k); g.pending &= ~(1u << k); g_anomalies++; }
    }
    if (!mask) return;
    VkRenderingAttachmentInfo colors[8]{};
    for (uint32_t k = 0; k < g.color_count; k++) {
        colors[k].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (!(mask >> k & 1)) continue;
        colors[k] = g.colors[k];
        colors[k].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[k].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingAttachmentInfo depth = g.depth, stencil = g.stencil;
    depth.loadOp = stencil.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = stencil.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, g.extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = g.color_count;
    ri.pColorAttachments = colors;
    if (mask >> 8 & 1) ri.pDepthAttachment = &depth;
    if (mask >> 9 & 1) ri.pStencilAttachment = &stencil;
    p_vkCmdBeginRendering(cb, &ri);
    p_vkCmdEndRendering(cb);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0,
                           nullptr, 0, nullptr);
    g_flush_passes++;
    g_flushed[cause] += uint32_t(__builtin_popcount(mask));
    g.pending &= ~mask;
}
void compact() {
    std::erase_if(g_groups, [](const Group& g) { return g.pending == 0; });
}
void flush_all(VkCommandBuffer cb, Cause cause) {
    if (g_groups.empty()) return;
    if (g_rendering) { g_open_pending++; return; }  // cannot happen: a pass's begin flushes all but its own
    for (Group& g : g_groups) resolve(cb, g, g.pending, cause);
    g_groups.clear();
    report();
}
// Start of a pass's set-up: flush every pending slot this pass cannot take over.
void at_begin(VkCommandBuffer cb, const Targets& t) {
    if (g_groups.empty()) return;
    GpuImage* mine[9]{};
    for (uint32_t k = 0; k < t.color_count && k < 8; k++) mine[k] = t.colors[k] ? image_of(t.colors[k]) : nullptr;
    mine[8] = t.depth ? image_of(t.depth) : nullptr;
    for (Group& g : g_groups) {
        uint32_t flush = 0;
        for (uint32_t k = 0; k < 10; k++) {
            if (!(g.pending >> k & 1)) continue;
            GpuImage* img = g.images[k < 8 ? k : 8];
            bool kept = false;
            if (img && img->msaa_valid) {
                if (k < 8) { for (uint32_t j = 0; j < t.color_count && j < 8; j++) kept |= mine[j] == img; }
                else if (mine[8] == img) kept = true;
                else if (keep153() && !clears_pending_image(img)) kept = true, g_kept153++;
            }
            if (!kept) flush |= 1u << k;
        }
        if (flush) resolve(cb, g, flush, BeginOther);
    }
    compact();
}
// After prepare_msaa_pass: the slots of this pass's images are dropped (multisample: it resolves them itself later)
// or flushed (single-sample: it loads the resolved image), before vkCmdBeginRendering.
void after_prepare(VkCommandBuffer cb, bool multisample, GpuImage* const (&images)[9], uint32_t color_count,
                   const VkRenderingAttachmentInfo* colors, GpuImage* dimg) {
    for (Group& g : g_groups) {
        uint32_t flush = 0;
        for (uint32_t k = 0; k < 10; k++) {
            if (!(g.pending >> k & 1)) continue;
            GpuImage* img = g.images[k < 8 ? k : 8];
            bool mine153 = false;
            if (k < 8) { for (uint32_t j = 0; j < color_count && j < 8; j++) mine153 |= images[j] == img; }
            else mine153 = dimg == img;
            if (!mine153 && keep153()) continue;  // cmd153: another image's pending depth/stencil stays pending
            bool taken = false;
            if (multisample) {
                if (k < 8) { for (uint32_t j = 0; j < color_count && j < 8; j++) taken |= images[j] == img && colors[j].imageView; }
                else taken = dimg == img;
            }
            if (taken) g.pending &= ~(1u << k), g_dropped++;
            else flush |= 1u << k;
        }
        if (flush) resolve(cb, g, flush, multisample ? BeginOther : BeginSingle);
    }
    compact();
    report();
}
}  // namespace lazy148
void begin_rendering(VkCommandBuffer cb, const Targets& t, bool allow_msaa = true) {
    if (lazy148::on()) lazy148::at_begin(cb, t);
    if (clears_pending_other(t)) clears_flush(cb);  // other targets' clears first, in order
    g_pipeline_attachments.invalidate();
    // An image never written (layout UNDEFINED) has nothing to load: loadOp CLEAR to zero, not a LOAD of whatever
    // the memory held (G3, docs/nvn/DEBUG.md #50); a pending clear still wins (clears_apply). Env: bisecting.
    static const bool load_undefined = std::getenv("NX_UNDEFINED_LOAD") != nullptr;
    uint32_t fresh = 0;  // bit c colour c, bit 8 depth: begun cleared to 0 (depth 0, stencil 0)
    VkRenderingAttachmentInfo colors[8]{};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    const auto native_targets = native_resources::prepare_targets(t);
    VkExtent2D extent = native_targets.complete ? native_targets.extent : VkExtent2D{UINT32_MAX, UINT32_MAX};
    VkFormat formats[8]{};  // the views' (render pass objects)
    for (uint32_t k = 0; k < t.color_count; k++) {
        colors[k].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        GpuImage* img = native_targets.complete ? native_targets.attachments[k].resource.get()
                                                   : (t.colors[k] ? image_of(t.colors[k]) : nullptr);
        g_pass_images[k] = img;
        if (!img) continue;
        keep_msaa_samples(*img, g_msaa_requested);
        if (img->layout == VK_IMAGE_LAYOUT_UNDEFINED && !load_undefined) fresh |= 1u << k;
        transition(cb, *img, VK_IMAGE_LAYOUT_GENERAL);  // rendered images stay GENERAL (sampled while rendered)
        img->rendered = true;
        uint32_t level = native_targets.complete ? native_targets.attachments[k].level
                                                 : std::min<uint32_t>(t.views.level[k], img->levels - 1);
        formats[k] = native_targets.complete ? native_targets.attachments[k].format
                                            : view_format(*img, t.views.format[k]);
        const uint32_t layer = native_targets.complete ? native_targets.attachments[k].layer : t.views.layer[k];
        colors[k].imageView = attachment_view(*img, level, layer, formats[k]);
        // NX_TRACE_CUBES=<first frame>: writes into cube images from that frame on. With 400 from the start, the log
        // ran out at the first 3D scene (frame ~171) and never reached a course load.
        static const char* trace_cubes = std::getenv("NX_TRACE_CUBES");
        static const uint64_t cube_from = trace_cubes ? std::strtoull(trace_cubes, nullptr, 10) : 0;
        static int cube_logs = 0;
        if (trace_cubes && (img->view_type == VK_IMAGE_VIEW_TYPE_CUBE || img->view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) &&
            g_frame >= cube_from && cube_logs++ < 2000)
            log("cubes: render into %llx level %u layer %u (%ux%u, %u levels, %u layers) view %s",
                (unsigned long long)t.colors[k], level, t.views.layer[k], img->width, img->height, img->levels,
                img->layers, colors[k].imageView == img->view ? "WHOLE IMAGE (fallback)" : "ok");
        colors[k].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        colors[k].loadOp = fresh >> k & 1 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[k].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (!native_targets.complete)
            extent = {std::min(extent.width, std::max(1u, img->width >> level)),
                      std::min(extent.height, std::max(1u, img->height >> level))};
    }
    GpuImage* dimg = native_targets.complete ? native_targets.attachments[8].resource.get()
                                              : (t.depth ? image_of(t.depth) : nullptr);
    g_pass_images[8] = dimg;
    dlss_scene_targets(g_pass_images,t.color_count);
    if (dimg && dimg->aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
        keep_msaa_samples(*dimg, g_msaa_requested);
        if (dimg->layout == VK_IMAGE_LAYOUT_UNDEFINED && !load_undefined) fresh |= 1u << 8;
        transition(cb, *dimg, VK_IMAGE_LAYOUT_GENERAL);
        dimg->rendered = true;
        uint32_t level = native_targets.complete ? native_targets.attachments[8].level
                                                 : std::min<uint32_t>(t.views.level[8], dimg->levels - 1);
        const uint32_t layer = native_targets.complete ? native_targets.attachments[8].layer : t.views.layer[8];
        depth.imageView = native_targets.complete ? attachment_view(*dimg, level, layer, native_targets.attachments[8].format)
                                                 : attachment_view(*dimg, level, layer);
        depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        depth.loadOp = fresh >> 8 & 1 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.clearValue.depthStencil = {1.0f, 0};  // far (a fresh depth buffer; the course passes test LESS)
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (!native_targets.complete)
            extent = {std::min(extent.width, std::max(1u, dimg->width >> level)),
                      std::min(extent.height, std::max(1u, dimg->height >> level))};
    } else {
        dimg = nullptr;
    }
    if (extent.width == UINT32_MAX) extent = {1, 1};
    VkRenderingAttachmentInfo stencil = depth;  // same view, its own loadOp
    uint32_t cleared = clears_apply(t, colors, depth, stencil);  // pending clears of these targets: loadOp CLEAR
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = t.color_count;
    ri.pColorAttachments = colors;
    if (dimg) {
        if (dimg->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ri.pDepthAttachment = &depth;
        if (dimg->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ri.pStencilAttachment = &stencil;
    }
    g_render_pass = render_pass_objects();
    const bool try_msaa = g_msaa_requested >= 2 && !g_render_pass && allow_msaa;
    g_pass_samples = try_msaa
                         ? prepare_msaa_pass(cb, g_pass_images, g_msaa_requested, t.color_count, formats,
                                             extent, colors, depth, stencil)
                         : VK_SAMPLE_COUNT_1_BIT;
    if (try_msaa) g_bound = Bound{};  // color expansion may have bound its own pipeline
    for (uint32_t k = 0; k < t.color_count; ++k)
        if (g_pass_images[k]) g_pass_images[k]->msaa_valid = g_pass_samples != VK_SAMPLE_COUNT_1_BIT;
    if (dimg && g_pass_samples != VK_SAMPLE_COUNT_1_BIT) dimg->msaa_valid = true;
    if (lazy148::on()) {
        const bool multisample = g_pass_samples != VK_SAMPLE_COUNT_1_BIT;
        lazy148::after_prepare(cb, multisample, g_pass_images, t.color_count, colors, dimg);
        lazy148::g_current_on = false;
        if (multisample) {
            lazy148::Group& g = lazy148::g_current;
            g = lazy148::Group{};
            g.color_count = t.color_count;
            g.extent = extent;
            for (uint32_t k = 0; k < t.color_count && k < 8; k++) {
                g.colors[k] = colors[k];
                if (colors[k].imageView && colors[k].resolveMode != VK_RESOLVE_MODE_NONE && g_pass_images[k] && g_pass_images[k]->msaa) {
                    g.images[k] = g_pass_images[k], g.sidecar[k] = colors[k].imageView, g.pending |= 1u << k;
                    colors[k].resolveMode = VK_RESOLVE_MODE_NONE, colors[k].resolveImageView = VK_NULL_HANDLE;
                }
            }
            if (dimg && dimg->msaa && depth.resolveMode != VK_RESOLVE_MODE_NONE) {
                g.images[8] = dimg, g.sidecar[8] = depth.imageView, g.depth = depth, g.stencil = stencil;
                if (dimg->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) g.pending |= 1u << 8;
                if (dimg->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) g.pending |= 1u << 9;
                depth.resolveMode = VK_RESOLVE_MODE_NONE, depth.resolveImageView = VK_NULL_HANDLE;
                stencil.resolveMode = VK_RESOLVE_MODE_NONE, stencil.resolveImageView = VK_NULL_HANDLE;
            }
            lazy148::g_current_on = g.pending != 0;
        }
    }
    // A multisample pass writes its sidecars as well as the guest resolve targets.
    g_attachment_only = g_pass_samples == VK_SAMPLE_COUNT_1_BIT && !g_render_pass && scoped_pass_barriers() &&
                        !narrow_pass_barrier();
    if (g_render_pass)
        begin_render_pass(cb, t.color_count, formats, colors, dimg ? dimg->format : VK_FORMAT_UNDEFINED, depth, stencil,
                          extent);
    else
        p_vkCmdBeginRendering(cb, &ri);
    g_rendering = true;
    g_pass_written = cleared | fresh;  // as a clear inside the pass: sampling it needs it stored first
    trace_pass("begin: cleared, fresh", t, cleared, fresh);
    g_pass = t;
    g_pass_extent = extent;
}

bool multisample_feedback(const Targets& t, const std::vector<const ShaderInfo*>& shaders) {
    if (g_msaa_requested < 2) return false;
    for (const ShaderInfo* shader : shaders)
        for (const DescriptorBinding& binding : shader->refl.bindings)
            if (binding.set == 3) return true;
    for (const ResolvedTexture& tex : g_textures.resolved) {
        if (!tex.texture) continue;
        const GpuImage* src = image_of(tex.texture);
        if (!src) continue;
        if (t.depth && src == image_of(t.depth)) return true;
        for (uint32_t k = 0; k < t.color_count; ++k)
            if (t.colors[k] && src == image_of(t.colors[k])) return true;
    }
    return false;
}

uint64_t g_dump_draws = 0, g_dump_request = 0;

// DispatchCompute: the bound program's compute stage over (x, y, z) groups, outside render passes, then a barrier
// so what it wrote is visible to what follows.
void dispatch(VkCommandBuffer cb, const Cmd& c) {
    static const bool off = std::getenv("NX_NO_COMPUTE") != nullptr;  // debug: skip compute dispatches
    if (off) return;
    const ShaderInfo* cs = g_state.compute_shader;
    if (!cs) return;
    const auto groups = engine_vk::dispatch_present(c) ? engine_vk::decode_dispatch(c) :
        VkDispatchIndirectCommand{uint32_t(c.i[0]), uint32_t(c.i[1]), uint32_t(c.i[2])};
    uint32_t x = groups.x, y = groups.y, z = groups.z;
    const uint8_t* args = c.n == 1 ? guest_bytes(c.a[0]) : nullptr;  // indirect: {x, y, z}
    if (c.n == 1 && !args) return;
    // Written by an earlier dispatch of this submit: only the GPU knows them (vkCmdDispatchIndirect, below).
    bool gpu_args = args && !written_separate() && written_overlaps(args, 12);
    if (args && !gpu_args) std::memcpy(&x, args, 4), std::memcpy(&y, args + 4, 4), std::memcpy(&z, args + 8, 4);
    if (!gpu_args && (!x || !y || !z)) return;
    trace_compute_once(cs, x, y, z);  // 0, 0, 0 for GPU arguments
    trace_nan_dispatch(cs);
    written_pose_dispatch_begin();
    const uint32_t pose_qa = catherine::visual::pose_qa_options(catherine::visual::pose_replay_frame());
    if (!args && !g_rendering &&
        ((!(pose_qa & catherine::visual::PoseQaNoCpuMorph) && [&]{ perf22::Scope ps(perf22::DispMorph); const bool r = try_cpu_morph(*cs, x, y, z); if (r) perf22::seq('m'); return r; }()) ||
         (!(pose_qa & catherine::visual::PoseQaNoCpuSkin) && [&]{ perf22::Scope ps(perf22::DispSkin); const bool r = try_cpu_skin(*cs, x, y, z); if (r) perf22::seq('s'); return r; }()))) {
        written_pose_dispatch_complete(*cs, x, false);
        draw_end_rendering(cb, 2);
        ++g_stats.dispatches;
        return;
    }
    perf22::seq('c');
    std::vector<const ShaderInfo*> stages{cs};
    const ProgramLayout& layout = program_layout(stages);
    VkPipeline pipeline = compute_pipeline(cs, layout);
    if (!pipeline) return;
    draw_end_rendering(cb, 2);
    if (lazy148::on()) lazy148::flush_all(cb, lazy148::Dispatch);
    gather_textures(stages);
    build_sets(cb, stages, layout);
    p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    bind_sets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout);
    if (gpu_profile_on()) gpu_profile_mark(cb, "compute " + cs->hash);
    if (!gpu_args || !written_indirect(cb, args)) p_vkCmdDispatch(cb, x, y, z);
    written_pose_dispatch_complete(*cs, x, gpu_args);
    if (gpu_profile_on()) gpu_profile_mark(cb, "outside passes (clears, uploads, copies)");
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0,
                           nullptr, 0, nullptr);
    g_stats.dispatches++;
}

void vertex_read_barrier(VkCommandBuffer cb, const ShaderInfo* vs) {
    if (!g_vertex_pending || !vs) return;
    bool reads = false;
    for (size_t k = 0; k < g_textures.bindings.size() && !reads; k++)
        if (g_textures.bindings[k].first == vs && k < g_textures.resolved.size() && g_textures.resolved[k].texture)
            if (GpuImage* img = image_of(g_textures.resolved[k].texture))
                reads = img->rendered && img->rendered_at > g_vertex_visible_at;
    if (!reads) return;
    draw_end_rendering(cb, 1);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &mb, 0,
                           nullptr, 0, nullptr);
    g_vertex_pending = false, g_vertex_barriers++, g_vertex_visible_at = g_pass_serial;
}

PipelineKey compatibility_pipeline_key(const ShaderInfo* vs,const ShaderInfo* fs,uint32_t mode,const Targets& t,const PipelineAttachmentKey& attachments) {
    ++native_pipeline::compatibility_key_builds;
    PipelineKey key;
    std::memset(&key, 0, sizeof key);  // the key is compared byte for byte
    key.vs = vs, key.fs = fs;
    key.topology = mode;
    key.attrib_count = uint32_t(std::min<size_t>(g_state.attribs.size(), 16));
    std::copy_n(g_state.attribs.begin(), key.attrib_count, key.attribs);
    key.stream_count = uint32_t(std::min<size_t>(g_state.streams.size(), 16));
    std::copy_n(g_state.streams.begin(), key.stream_count, key.streams);
    std::copy_n(g_state.blend, 8, key.blend);
    key.color = g_state.color, key.mask = g_state.mask, key.depth = g_state.depth, key.polygon = g_state.polygon;
    key.color_count = t.color_count, key.depth_clamp = g_state.depth_clamp;
    key.alpha_to_coverage = g_state.alpha_to_coverage;
    key.samples = g_pass_samples;
    attachments.apply(key);
    return key;
}

#include "fast89.inc"
#include "batch91.inc"

bool draw(VkCommandBuffer cb, const Targets& t, uint32_t mode, bool indexed, const Cmd& c) {
    perf22::Scope ps_draw(perf22::Draw);
    perf22::seq('D');
    const uint64_t ps_pre = perf22::start();
    g_stats.total++;
    if (g_state.rasterizer_discard || !g_state.render_enable) return false;  // nothing reaches the targets
    const auto* shaders = &g_state.graphics_shaders;
    if (shaders->empty()) {
        g_stats.no_program++;
        return false;
    }
    const ShaderInfo* vs = nullptr;
    const ShaderInfo* fs = nullptr;
    for (const ShaderInfo* s : *shaders) {
        if (s->stage == ShaderStage::Vertex) vs = s;
        if (s->stage == ShaderStage::Fragment) fs = s;
    }
    if (!vs) {
        g_stats.no_vertex++;
        return false;
    }
    if(dlss_ui_needed(vs->hash.c_str())){
        draw_end_rendering(cb,2);const auto targets=native_resources::prepare_targets(t);
        dlss_capture_hudless(cb,targets.complete?targets.attachments[0].resource.get():(t.colors[0]?image_of(t.colors[0]):nullptr));
    }
    perf22::add(perf22::PreHead, ps_pre);
    std::vector<const ShaderInfo*> swapped;  // NX_SWAP_FS / NX_SKIP_FS (nvn_draw_trace.cpp)
    const uint64_t ps_dbg = perf22::start();
    const bool dbg_ok = debug_shader_overrides(shaders, fs, swapped);
    perf22::add(perf22::PreDebug, ps_dbg);
    if (!dbg_ok) return false;
    const uint64_t ps_lay = perf22::start();
    const ProgramLayout& layout = program_layout(*shaders);
    perf22::add(perf22::PreLayout, ps_lay);
    const uint64_t ps_blk = perf22::start();
    const bool batch91_block = batch91::on() && batch91::active() && batch91::block_shader(vs);
    perf22::add(perf22::PreBlock, ps_blk);
    fast89::Template* fast89_t;
    { perf22::Scope ps(perf22::F89Match); fast89_t = batch91_block ? fast89::match(cb, t, mode, indexed, c, *shaders, vs, fs, layout) : nullptr; }
    if (fast89_t && batch91::enqueue(cb, *fast89_t)) return true;
    const uint64_t ps_pend = perf22::start();
    if (batch91::any_pending()) {
        // This draw is recorded now: batches must go first if it is order-sensitive, or if it is about to
        // recapture the template slot that holds pending matrices.
        if (!batch91::order_free_state(g_state) ||
            (batch91_block && !batch91::g_buckets[fast89::choose97(fast89::key97(vs, fs, &layout, mode, indexed, c))].pending.empty()))
            batch91::flush(cb);
    }

    perf22::add(perf22::PreFlush, ps_pend);
    const uint64_t ps_snap = perf22::start();
    // NX_SNAP_FS_BEFORE=<hash>[@<first draw>]: snapshot colour target 0 before draws with that fragment shader
    // (what a feedback pass like a tonemap reads), 8 at most.
    {
        static const std::string before = std::getenv("NX_SNAP_FS_BEFORE") ? std::getenv("NX_SNAP_FS_BEFORE") : "";
        static const uint64_t from = before.find('@') != std::string::npos
                                         ? std::strtoull(before.c_str() + before.find('@') + 1, nullptr, 10) : 0;
        static int taken = 0;
        bool match = (fs && before.compare(0, 16, fs->hash) == 0) || before.compare(0, 16, vs->hash) == 0;
        if (!before.empty() && match && g_stats.total >= from && taken < 8 &&
            t.color_count && t.colors[0])
            if (GpuImage* img = image_of(t.colors[0])) {
                taken++;
                draw_end_rendering(cb, 6);
                debug_snapshot(cb, *img, 0xB0000000ull + g_stats.total);
            }
    }
    // Textures first: an upload or a layout change cannot happen inside the render pass.
    perf22::add(perf22::PreSnap, ps_snap);
    perf22::add(perf22::DrawPre, ps_pre);
    uint32_t gathered;
    { perf22::Scope ps(perf22::DrawGather); gathered = gather_textures(*shaders); }
    const uint64_t ps_mid1 = perf22::start();
    if (lazy148::on() && !lazy148::g_groups.empty()) {
        bool hit153 = false;
        for (const ShaderInfo* sh : *shaders) {
            for (const DescriptorBinding& b : sh->refl.bindings) if (b.set == 3) { hit153 = true; break; }
            if (hit153) break;
        }
        for (size_t r = 0; !hit153 && r < g_textures.resolved.size(); r++) {
            if (!g_textures.resolved[r].texture) continue;
            GpuImage* img = image_of(g_textures.resolved[r].texture);
            if (!img) continue;
            for (const lazy148::Group& g : lazy148::g_groups) {
                for (uint32_t k = 0; k < 10 && !hit153; k++)
                    hit153 = (g.pending >> k & 1) && g.images[k < 8 ? k : 8] == img;
                if (hit153) break;
            }
        }
        if (hit153) {
            if (g_rendering) { draw_end_rendering(cb, 1); ++lazy148::g_draw_breaks153; }
            lazy148::flush_all(cb, lazy148::Sampled);
        }
    }
    // A pass that neither tests nor writes depth but keeps its depth image bound while sampling that same image (the
    // world map's depth-to-colour linearization, fs 0aa56d1b): on Metal the image is then attached and read in one
    // encoder, which is undefined, and the copy came out 0 everywhere; the composite then took the player for
    // occluded (a flat silhouette, issue #50; DEBUG-2.md #67). The depth is unused: the pass runs without it.
    // NX_KEEP_SAMPLED_DEPTH=1: as before.
    Targets detached;
    const Targets& tt = [&]() -> const Targets& {
        static const bool keep = std::getenv("NX_KEEP_SAMPLED_DEPTH") != nullptr;
        const auto d = native_draw_depth::read(g_state.depth);
        if (keep || !t.depth || d.test || d.write || d.stencil) return t;
        GpuImage* dimg = image_of(t.depth);
        if (!dimg) return t;
        for (const ResolvedTexture& r : g_textures.resolved)
            if (r.texture && image_of(r.texture) == dimg) {
                static uint64_t n = 0;
                if (n++ == 0) log("nvn: a pass samples its own unused depth image: drawn without it (#50)");
                detached = t, detached.depth = 0;
                return detached;
            }
        return t;
    }();
    vertex_read_barrier(cb, vs);  // G4
    bool need_break = gathered & kNeedBreak;
    const bool feedback = multisample_feedback(tt, *shaders);
    need_break |= feedback && g_pass_samples != VK_SAMPLE_COUNT_1_BIT;
    if (gathered & kSampledPending) clears_flush(cb);  // cleared (and stored) before it is sampled
    static const bool bytes = std::getenv("NX_TARGETS_MEMCMP") != nullptr;  // bisecting
    bool targets_changed = !g_rendering || (bytes ? std::memcmp(&g_pass, &tt, sizeof tt) != 0 : !targets_same(g_pass, tt));
    if (g_rendering && (need_break || targets_changed)) {
        (targets_changed ? g_stats.breaks_targets : g_stats.breaks_textures)++;
        draw_end_rendering(cb, 1);
    }

    perf22::add(perf22::DrawMid1, ps_mid1);
    { perf22::Scope ps(perf22::DrawSets); build_sets(cb, *shaders, layout); }
    // Vertex and index data before the pass begins: a range a dispatch of this submit wrote may need a GPU copy
    // (nvn_written.cpp), which cannot be recorded inside a pass.
    DrawInputs in;
    bool inputs_ok;
    { perf22::Scope ps(perf22::DrawInputs); inputs_ok = draw_inputs_shared(cb, vs, indexed, c, in); }
    const uint64_t ps_mid2 = perf22::start();
    if (!inputs_ok) {
        g_stats.no_indices++;
        return false;
    }
    check_vertex_ranges(vs, fs, indexed, c, in);  // NX_CHECK_VERTS (nvn_written.cpp)
    // Vulkan has no quad topology. Both array and indexed quads must expand;
    // passing four indices to TRIANGLE_LIST silently drops the second half (boss eye glow).
    const bool quad_draw = mode == 7;
    uint32_t quad_indices = 0;
    if (quad_draw) {
        const uint32_t quads = uint32_t(std::max(0, c.i[indexed ? 0 : 1])) / 4;
        const Upload source_indices = in.index;
        const VkIndexType source_type = in.index_type;
        if (indexed && !source_indices.ptr) return false;
        if (!quads) return false;
        quad_indices = quads * 6;
        in.index = upload_alloc(uint64_t(quad_indices) * sizeof(uint32_t), alignof(uint32_t));
        in.index_type = VK_INDEX_TYPE_UINT32;
        auto* index = reinterpret_cast<uint32_t*>(in.index.ptr);
        for (uint32_t quad = 0; quad < quads; ++quad, index += 6) {
            const uint32_t v = quad * 4;
            uint32_t corners[4] = {v, v + 1, v + 2, v + 3};
            if (indexed) {
                const uint32_t width = source_type == VK_INDEX_TYPE_UINT16 ? 2 :
                                       source_type == VK_INDEX_TYPE_UINT8_EXT ? 1 : 4;
                for (uint32_t j = 0; j < 4; ++j) {
                    corners[j] = 0;
                    std::memcpy(&corners[j], source_indices.ptr + uint64_t(v + j) * width, width);
                }
            }
            index[0] = corners[0];
            index[1] = corners[1];
            index[2] = corners[2];
            index[3] = corners[0];
            index[4] = corners[2];
            index[5] = corners[3];
        }
    }
    if (!g_rendering) {
        if (gpu_profile_on()) {
            GpuImage* img = t.color_count && t.colors[0] ? image_of(t.colors[0]) : nullptr;
            char label[128];
            std::snprintf(label, sizeof label, "%ux%u L%u fs %s", img ? img->width : 0, img ? img->height : 0,
                          t.views.level[0], fs ? fs->hash.c_str() : "-");
            gpu_profile_mark(cb, label);
        }
        begin_rendering(cb, tt, !feedback);
    }
    fit_screen_gradient(in, c, mode, indexed, *vs, fs, g_pass_extent);

    perf22::add(perf22::DrawMid2, ps_mid2);
    // Pipeline.
    const uint64_t ps_pipe = perf22::start();
    const auto attachments=pipeline_attachments(t,tt);
    const bool native_ready=native_pipeline::ready(c);
    const native_pipeline::Definition* definition=nullptr;
    std::optional<native_pipeline::Definition> prepared;
    std::optional<PipelineKey> key;
    if(!native_ready||native_pipeline::comparing())key.emplace(compatibility_pipeline_key(vs,fs,mode,t,attachments));
    VkPipeline pipeline;
    if(native_ready) {
        ++native_pipeline::ordinary_requests;
        if(native_pipeline::memo_mode()){
            const auto selected=graphics_pipeline_current(vs,fs,c,layout,attachments.colors.data(),attachments.depth,t.color_count,attachments.ufloat,g_pass_samples,key?&*key:nullptr);
            pipeline=selected.pipeline;definition=selected.definition;
        }else{
            prepared.emplace(native_pipeline::current(vs,fs,c));
            std::copy(attachments.colors.begin(),attachments.colors.end(),prepared->color_formats);
            prepared->depth_format=attachments.depth;prepared->ufloat_colors=attachments.ufloat;
            prepared->color_count=t.color_count;prepared->samples=g_pass_samples;
            definition=&*prepared;
            if(key)native_pipeline::validate(*definition,*key,layout);
            pipeline=graphics_pipeline_definition(*definition,layout);
        }
        if(key)++native_pipeline::compared_requests;
    } else {
        ++native_pipeline::compatibility_requests;
        pipeline=producer_fixed::mode()
            ?graphics_pipeline_native_fixed(*key,layout,producer_pipeline::prepare(*key,c),producer_fixed::prepare(*key))
            :producer_pipeline::mode()?graphics_pipeline_native(*key,layout,producer_pipeline::prepare(*key,c)):graphics_pipeline(*key,layout);
    }
    native_pipeline::report();
    perf22::add(perf22::DrawPipeline, ps_pipe);
    perf22::Scope ps_bind(perf22::DrawBind);
    if (!pipeline) {
        g_stats.no_pipeline++;
        return false;
    }

    if (g_bound.cb != cb || draw_slow(kSlowState)) g_bound = Bound{}, g_bound.cb = cb;
    if (g_bound.pipeline != pipeline) {
        p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        g_bound.pipeline = pipeline, g_state_set++;
    } else {
        g_state_kept++;
    }
    { perf22::Scope ps(perf22::BdSets); bind_sets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout); }
    const uint64_t ps_vx = perf22::start();

    // The vertex buffers resolved above, plus the zero stream (binding 15).
    for (uint32_t s = 0; s < 15; s++)
        if (in.used >> s & 1) bind_vertex(cb, s, in.vertex[s]);
    bind_vertex(cb, 15, zero_stream());

    perf22::add(perf22::BdVertex, ps_vx);
    const uint64_t ps_rs = perf22::start();
    // Values were converted and owned on the game producer. Pass extents stay late-bound.
    const auto native_raster = producer_raster::prepare(g_state,g_pass_extent);
    const VkViewport vp = native_raster.viewport;
    const VkRect2D sc = native_raster.scissor;
    set_vulkan_dynamic(cb,vp,sc,native_raster.dynamic);

    perf22::add(perf22::BdRaster, ps_rs);
    const uint64_t ps_cmd = perf22::start();
    trace_skin(indexed, c, vs, fs);
    if (indexed || quad_draw) {
        uint32_t count = quad_draw ? quad_indices : uint32_t(c.i[0]);
        int32_t vertex_offset = quad_draw && !indexed ? c.i[0] : c.i[1];
        if (g_bound.has_index && g_bound.index.buffer == in.index.buffer && g_bound.index.offset == in.index.offset &&
            g_bound.index_type == in.index_type) {
            g_state_kept++;
        } else {
            p_vkCmdBindIndexBuffer(cb, in.index.buffer, in.index.offset, in.index_type);
            g_bound.has_index = true, g_bound.index = in.index, g_bound.index_type = in.index_type, g_state_set++;
        }
        bool q = zero_draws_on() && zero_draws_begin(cb, vs->hash + " " + (fs ? fs->hash : std::string("-")));
        if (!quad_draw && engine_vk::present(c) && engine_vk::decode(c).index_type == in.index_type) {
            const auto native = engine_vk::decode(c);
            const auto& args = native.args;
            p_vkCmdDrawIndexed(cb, args.indexCount, args.instanceCount, args.firstIndex,
                              args.vertexOffset, args.firstInstance);
        } else {
            p_vkCmdDrawIndexed(cb, count, uint32_t(std::max(1, c.i[3])), 0, vertex_offset, uint32_t(c.i[2]));
        }
        if (q) zero_draws_end(cb);
    } else {
        bool q = zero_draws_on() && zero_draws_begin(cb, vs->hash + " " + (fs ? fs->hash : std::string("-")));
        if (engine_vk::arrays_present(c)) {
            const auto args = engine_vk::decode_arrays(c);
            p_vkCmdDraw(cb, args.vertexCount, args.instanceCount, args.firstVertex, args.firstInstance);
        } else {
            p_vkCmdDraw(cb, uint32_t(c.i[1]), uint32_t(std::max(1, c.i[3])), uint32_t(c.i[0]), uint32_t(c.i[2]));
        }
        if (q) zero_draws_end(cb);
    }
    perf22::add(perf22::BdCmd, ps_cmd);
    perf22::Scope ps_post(perf22::BdPost);
    g_stats.done++;
    static unsigned msaa_reported = 0;
    if (g_pass_samples != VK_SAMPLE_COUNT_1_BIT && !(msaa_reported & g_pass_samples)) {
        msaa_reported |= g_pass_samples;
        log("nvn/vk: %ux MSAA resolving %ux%u geometry into guest targets", unsigned(g_pass_samples),
            g_pass_extent.width, g_pass_extent.height);
    }
    if (g_attachment_only && (!shader_scope_cached(vs) || !shader_scope_cached(fs)))
        g_attachment_only = false;
    g_pass_written |= ((1u << t.color_count) - 1) | (native_draw_depth::writes(g_state.depth) ? 1u << 8 : 0);
    // NX_SNAP_FS=<hash>,<hash>[@<first draw>]: also snapshot after draws with those fragment shaders (8 at most, or
    // NX_SNAP_FS_MAX).
    static const std::string snap_fs = std::getenv("NX_SNAP_FS") ? std::getenv("NX_SNAP_FS") : "";
    static const uint64_t snap_from = snap_fs.find('@') != std::string::npos
                                          ? std::strtoull(snap_fs.c_str() + snap_fs.find('@') + 1, nullptr, 10) : 0;
    static int snap_count = 0;
    static const int snap_max = std::getenv("NX_SNAP_FS_MAX") ? std::atoi(std::getenv("NX_SNAP_FS_MAX")) : 8;
    bool snap_by_fs = !snap_fs.empty() && g_stats.done >= snap_from && snap_count < snap_max &&
                      ((fs && snap_fs.find(fs->hash) != std::string::npos) ||
                       snap_fs.find(vs->hash) != std::string::npos) &&
                      ++snap_count;  // the vertex shader's hash matches too
    if ((snap_by_fs || snap_follow(t, vs, fs, g_textures.handles) || debug_snap_wanted(g_stats.done)) && t.color_count && t.colors[0])
        if (GpuImage* snap = image_of(t.colors[0])) draw_end_rendering(cb, 6), debug_snapshot(cb, *snap, g_stats.done);
    // NX_DUMP_ON_TARGET=<hex NVNtexture*>: every NX_DUMP_FRAMES-th draw into it asks for NX_DUMP_TEXTURES to be read
    // back right after this submit (before later frames change them).
    static const char* dump_env = std::getenv("NX_DUMP_ON_TARGET");
    static const uint64_t dump_target = dump_env ? std::strtoull(dump_env, nullptr, 16) : 0;
    if (dump_target && t.colors[0] == dump_target) g_dump_request = ++g_dump_draws;
    // NX_DUMP_ON_FS=<fs hash>[,...]: a draw with one of these fragment shaders dumps its target 0 and what it samples.
    static const std::string dump_fs = [] { const char* e = std::getenv("NX_DUMP_ON_FS"); return std::string(e ? e : ""); }();
    static const uint64_t dump_from = [] { const char* e = std::getenv("NX_DUMP_FROM"); return e ? std::strtoull(e, nullptr, 10) : 0; }();
    if (!dump_fs.empty() && fs && g_frame >= dump_from && dump_fs.find(fs->hash) != std::string::npos && g_dump_draws < 400 &&
        t.colors[0] && image_of(t.colors[0]) && (std::getenv("NX_DUMP_ANY") || image_of(t.colors[0])->view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
                                                           image_of(t.colors[0])->view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)) {
        debug_dump_add(t.colors[0]);
        for (const ResolvedTexture& r : g_textures.resolved) debug_dump_add(r.texture);
        g_dump_request = ++g_dump_draws;
    }
    if (batch91_block && !fast89_t) {
        perf22::Scope ps(perf22::F89Capture);
        fast89::capture(cb, t, mode, indexed, c, *shaders, vs, fs, layout, pipeline, in, gathered, quad_draw);
        batch91::after_capture(vs, fs, layout, key?&*key:nullptr,definition,mode,indexed,c);
    }
    trace_draw(t, mode, indexed, c, vs, fs, g_textures.bindings, g_textures.handles);
    return true;
}

}  // namespace

void draw_end_submit() {  // after the submit's fence: compute results to guest memory, debug snapshots
    debug_snapshots_write();
    for (const WriteBack& w : draw_take_write_backs()) {
        trace_nan_write_back(w.from, w.to, w.size);
        apply_write_back(w);
    }
}

std::vector<WriteBack> draw_take_write_backs() {  // an asynchronous submit copies them when it is retired
    std::vector<WriteBack> v = written_take();  // one per written range (disjoint), then NX_COMPUTE_SEPARATE's
    v.insert(v.end(), separate_write_backs().begin(), separate_write_backs().end());
    separate_write_backs().clear();
    return v;
}

void draw_begin_submit() {
    fast89::invalidate();
    batch91::submit_tick();
    if (batch91::any_pending()) {  // cannot happen (every pass end flushes); never carried into a new submit
        static uint64_t dropped = 0;
        log("batch91: %llu pending batches dropped at a submit (unexpected)", (unsigned long long)++dropped);
        for (uint16_t s : batch91::g_order) batch91::g_buckets[s].pending.clear();
        batch91::g_order.clear();
    }
    {
        static uint64_t submits = 0;
        if (batch91::on() && ++submits % 600 == 0)
            log("batch91 adaptive: %s, %llu switches", batch91::active() ? "active" : "idle", (unsigned long long)batch91::g_adapt.switches),
            log("batch91: %llu block draws batched into %llu instanced draws (%llu flushes), %llu captures, %llu ineligible",
                (unsigned long long)batch91::g_batched, (unsigned long long)batch91::g_instanced_draws,
                (unsigned long long)batch91::g_flushes, (unsigned long long)fast89::g_captures,
                (unsigned long long)batch91::g_ineligible);
        if (batch91::on() && submits % 600 == 0 && fast89::admission_enabled())
            log("Template admission: considered %llu skipped %llu eligible %llu collisions %llu captures %llu batched instances %llu instanced draws %llu",
                (unsigned long long)fast89::g_admission_considered,(unsigned long long)fast89::g_admission_skipped,
                (unsigned long long)fast89::g_admission_eligible,(unsigned long long)fast89::g_admission_collisions,
                (unsigned long long)fast89::g_captures,(unsigned long long)batch91::g_batched,(unsigned long long)batch91::g_instanced_draws);
        if (batch91::on() && submits % 600 == 0)
            log("batch91 why: after_capture %llu (template refused %llu, shape %llu, order-sensitive %llu, matrix size %llu, no variant %llu); "
                "capture refused: rendering %llu textures %llu layout %llu storage %llu values %llu uniforms %llu vertex %llu index %llu",
                (unsigned long long)batch91::g_why[0], (unsigned long long)batch91::g_why[1], (unsigned long long)batch91::g_why[2],
                (unsigned long long)batch91::g_why[3], (unsigned long long)batch91::g_why[4], (unsigned long long)batch91::g_why[5],
                (unsigned long long)fast89::g_cap_why[0], (unsigned long long)fast89::g_cap_why[1], (unsigned long long)fast89::g_cap_why[2],
                (unsigned long long)fast89::g_cap_why[3], (unsigned long long)fast89::g_cap_why[4], (unsigned long long)fast89::g_cap_why[5],
                (unsigned long long)fast89::g_cap_why[6], (unsigned long long)fast89::g_cap_why[7]);
    }
    g_msaa_requested = plat::anti_aliasing();
    if (g_msaa_requested >= 2 && !(msaa_sample_mask() & unsigned(g_msaa_requested))) g_msaa_requested = 0;
    g_pipeline_attachments.invalidate();
    g_rendering = false;
    g_pass_samples = VK_SAMPLE_COUNT_1_BIT;
    g_vertex_pending = false, g_vertex_visible_at = g_pass_serial;  // each command buffer begins with a full barrier
    g_bound = Bound{};  // a new command buffer
    submit_lists_reset();
    sets_begin_submit();
    clears_begin_submit();
    g_submit_ns = plat::monotonic_ns();
    dedup_reset();
    written_reset();
    pipeline_begin_submit();
}

uint64_t recorded_draws();

void draw_log_stats() {
    if (scoped_pass_barriers())
        log("nvn passes: %llu attachment-scoped barriers, %llu global fallbacks",
            (unsigned long long)g_attachment_barriers, (unsigned long long)g_attachment_fallbacks);
    if (g_vertex_barriers) log("nvn passes: %llu vertex-read barriers (G4)", (unsigned long long)g_vertex_barriers);
    dedup_log();
    render_pass_log();
    written_log();
    sampled_ready_log();
    sets_log();
    if (g_state_set + g_state_kept)
        log("nvn: draw state: %llu binds and sets recorded, %llu already in place", (unsigned long long)g_state_set,
            (unsigned long long)g_state_kept);
    g_state_set = g_state_kept = 0;
    clears_log();
    log("nvn passes ended: %llu submit end, %llu draw, %llu dispatch, %llu clear of other targets, %llu clear larger than "
        "the pass, %llu copy, %llu snapshot, %llu SetRenderTargets", (unsigned long long)g_pass_ends[0],
        (unsigned long long)g_pass_ends[1], (unsigned long long)g_pass_ends[2], (unsigned long long)g_pass_ends[3],
        (unsigned long long)g_pass_ends[4], (unsigned long long)g_pass_ends[5], (unsigned long long)g_pass_ends[6],
        (unsigned long long)g_pass_ends[7]);
    Stats& t = g_stats;
    log("nvn draws: %llu recorded, %llu replayed:", (unsigned long long)recorded_draws(), (unsigned long long)t.total);
    log("nvn draws: %llu total, %llu done, %llu no program, %llu no vertex shader, %llu no pipeline, %llu no indices",
        (unsigned long long)t.total, (unsigned long long)t.done, (unsigned long long)t.no_program,
        (unsigned long long)t.no_vertex, (unsigned long long)t.no_pipeline, (unsigned long long)t.no_indices);
    log("nvn passes: %llu ended by a draw's texture uploads or layout changes, %llu by new targets, %llu dispatches",
        (unsigned long long)t.breaks_textures, (unsigned long long)t.breaks_targets, (unsigned long long)t.dispatches);
}

void pass_end_only(VkCommandBuffer cb, int reason) {
    if (g_rendering) batch91::flush(cb);
    g_pipeline_attachments.invalidate();
    if (!g_rendering) {
        if (lazy148::on() && (reason == 0 || (reason == 5 && !lazy148::copy159()) || (!lazy148::clear155() && (reason == 3 || reason == 4))))
            lazy148::flush_all(cb, reason == 0 ? lazy148::SubmitEnd : lazy148::ClearCopy);
        return;
    }
    g_pass_ends[reason & 7]++, trace_pass("end: reason", g_pass, uint32_t(reason), 0);
    if (g_render_pass)
        p_vkCmdEndRenderPass(cb);
    else
        p_vkCmdEndRendering(cb);
    g_rendering = false;
    if (g_pass_samples == VK_SAMPLE_COUNT_1_BIT && (g_pass_written & (1u << 8)) && g_pass_images[8])
        g_pass_images[8]->msaa_valid = false;
    g_pass_samples = VK_SAMPLE_COUNT_1_BIT;
    // What the pass wrote is read by what follows (sampled, copied, loaded by the next pass), and what it sampled may
    // be rendered over next. The images stay GENERAL, so transition() records no barrier. Metal (MoltenVK) and the
    // Intel driver ordered passes anyway; NVIDIA overlaps them, and later passes read half-written tiles: the course
    // HUD in blocks (DEBUG-2 #71). NX_NO_PASS_BARRIER=1: none, as before (bisecting).
    // Narrow (PHASE4.md G4; NX_PASS_BARRIER_NARROW=1): the later stages only, so the next pass's vertex work and
    // binning may start while this one finishes. A draw whose vertex shader samples a rendered image gets a barrier
    // of its own first (vertex_read_barrier). By default ALL_COMMANDS, as before.
    static const bool no_barrier = std::getenv("NX_NO_PASS_BARRIER") != nullptr;
    bool scoped = false;
    if (!no_barrier && g_attachment_only) {
        VkImageMemoryBarrier attachments[9]{};
        const uint32_t count = pass_attachment_barriers(g_pass, g_pass_images, attachments);
        if (count) {
            // Keep execution ordering for images sampled by the old pass.
            // Only memory availability is scoped to the attachments.
            p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                   0, 0, nullptr, 0, nullptr, count, attachments);
            ++g_attachment_barriers;
            scoped = true;
        }
    }
    if (!no_barrier && !scoped) {
        if (scoped_pass_barriers()) ++g_attachment_fallbacks;
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        VkPipelineStageFlags dst = narrow_pass_barrier()
                                       ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                       : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
        g_vertex_pending |= narrow_pass_barrier();
    }
    g_pass_serial++;
    for (uint32_t k = 0; k < 9; k++)  // this pass's targets only (the other slots may name freed images)
        if ((k < g_pass.color_count || (k == 8 && g_pass.depth)) && g_pass_images[k])
            g_pass_images[k]->rendered_at = g_pass_serial;
    if (lazy148::g_current_on) {
        lazy148::g_groups.push_back(lazy148::g_current);
        lazy148::g_deferred += uint32_t(__builtin_popcount(lazy148::g_current.pending));
        lazy148::g_current_on = false;
    }
    if (lazy148::on() && (reason == 0 || (reason == 5 && !lazy148::copy159()) || (!lazy148::clear155() && (reason == 3 || reason == 4))))
        lazy148::flush_all(cb, reason == 0 ? lazy148::SubmitEnd : lazy148::ClearCopy);
    if(dlss_depth_snapshot_pending()&&lazy148::on())lazy148::flush_all(cb,lazy148::ClearCopy);
    dlss_scene_capture(cb);
    gpu_profile_mark(cb, "outside passes (clears, uploads, copies)");
}
void draw_end_rendering(VkCommandBuffer cb, int reason) {  // and the pending clears (nvn_clears.cpp)
    pass_end_only(cb, reason);
    clears_flush(cb);
}
bool pass_open() { return g_rendering; }
bool pass_open_on(const Targets& t) { return g_rendering && targets_same(g_pass, t); }
VkExtent2D pass_extent() { return g_pass_extent; }
VkSampleCountFlagBits pass_samples() { return g_pass_samples; }
void pass_begin(VkCommandBuffer cb, const Targets& t) { begin_rendering(cb, t); }
// After a clear inside the pass (nvn_clears.cpp). A masked one is a fullscreen draw with its own pipeline, viewport
// and scissor (nvn_mask_clear.cpp): the next draw sets everything again.
void pass_mark_written(uint32_t bits) { g_pass_written |= bits, g_bound = Bound{}; fast89::invalidate(); }

bool replay::pass_wrote(const GpuImage* img) {
    if (!g_rendering || !img) return false;
    uint32_t hit = g_pass.depth && g_pass_images[8] == img ? 1u << 8 : 0;
    for (uint32_t c = 0; c < g_pass.color_count; c++)
        if (g_pass.colors[c] && g_pass_images[c] == img) hit |= 1u << c;
    return (hit & g_pass_written) != 0;
}

uint64_t submit_start_ns() { return g_submit_ns; }

// Entry points of the draws and dispatches for draw_command (nvn_draw_state.cpp).
bool draw_call(VkCommandBuffer cb, const Targets& t, uint32_t mode, bool indexed, const Cmd& c) {
    return draw(cb, t, mode, indexed, c);
}
void dispatch_call(VkCommandBuffer cb, const Cmd& c) { perf22::Scope ps(perf22::Dispatch); batch91::flush(cb); fast89::invalidate(); dispatch(cb, c); }
void fast89_flush_invalidate_clear155(VkCommandBuffer cb, const Targets& st, const Cmd& c) {
    batch91::flush(cb);
    fast89::invalidate();
    if (!lazy148::on() || lazy148::g_groups.empty()) return;
    if (!lazy148::clear155()) {
        if (!g_rendering) lazy148::flush_all(cb, lazy148::ClearCopy);
        return;
    }
    GpuImage* img = nullptr;
    if (c.op == Cmd::ClearColor) img = c.n < st.color_count && c.n < 8 && st.colors[c.n] ? image_of(st.colors[c.n]) : nullptr;
    else img = st.depth ? image_of(st.depth) : nullptr;
    if (!img) return;
    bool hit = false;
    for (const lazy148::Group& g : lazy148::g_groups)
        for (uint32_t k = 0; k < 10 && !hit; k++) hit = (g.pending >> k & 1) && g.images[k < 8 ? k : 8] == img;
    if (!hit) return;
    // cmd157: the slots this clear overwrites whole are dropped, not resolved.
    static const bool drop157 = [] { const char* e = std::getenv("CATHERINE_PARALLEL157"); return !(e && e[0] == '0'); }() &&
                                !std::getenv("NX_CLEARS_SCISSOR") && !std::getenv("NX_CLEARS_NO_MASK");
    uint32_t covered = 0;  // bit 0 colour, bit 8 depth, bit 9 stencil (slot kinds)
    if (drop157) {
        if (c.op == Cmd::ClearColor) {
            if (img->aspect == VK_IMAGE_ASPECT_COLOR_BIT && (c.mask & 0xF) == 0xF) covered |= 1u;
        } else if (img->aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
            if ((c.mask & 1) && (img->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)) covered |= 1u << 8;
            if ((c.mask >> 8 & 0xFF) && (img->aspect & VK_IMAGE_ASPECT_STENCIL_BIT)) covered |= 1u << 9;
        }
    }
    uint32_t left = 0;
    for (lazy148::Group& g : lazy148::g_groups)
        for (uint32_t k = 0; k < 10; k++) {
            if (!((g.pending >> k & 1) && g.images[k < 8 ? k : 8] == img)) continue;
            const uint32_t kind = k < 8 ? 1u : 1u << k;
            if (covered & kind) g.pending &= ~(1u << k), ++lazy148::g_clear_dropped157;
            else left++;
        }
    if (left && g_rendering) draw_end_rendering(cb, 3);  // the clear would end this pass (its targets are not the clear's)
    for (lazy148::Group& g : lazy148::g_groups) {
        uint32_t mask = 0;
        for (uint32_t k = 0; k < 10; k++)
            if ((g.pending >> k & 1) && g.images[k < 8 ? k : 8] == img) mask |= 1u << k;
        if (mask) lazy148::resolve(cb, g, mask, lazy148::ClearCopy);
    }
    lazy148::compact();
}
void fast89_flush_invalidate_copy159(VkCommandBuffer cb, const Cmd& c) {
    batch91::flush(cb);
    fast89::invalidate();
    if (!lazy148::on() || lazy148::g_groups.empty()) return;
    if (!lazy148::copy159()) {
        if (!g_rendering) lazy148::flush_all(cb, lazy148::ClearCopy);
        return;
    }
    GpuImage* imgs[2] = {};
    if (c.op == Cmd::CopyTextureToTexture) imgs[0] = c.a[0] ? image_of(c.a[0]) : nullptr, imgs[1] = c.a[1] ? image_of(c.a[1]) : nullptr;
    else if (c.op == Cmd::CopyBufferToTexture) imgs[0] = c.a[1] ? image_of(c.a[1]) : nullptr;
    else imgs[0] = c.a[0] ? image_of(c.a[0]) : nullptr;  // ClearTexture
    auto touched = [&](const GpuImage* img) { return img && (img == imgs[0] || img == imgs[1]); };
    bool hit = false;
    for (const lazy148::Group& g : lazy148::g_groups)
        for (uint32_t k = 0; k < 10 && !hit; k++) hit = (g.pending >> k & 1) && touched(g.images[k < 8 ? k : 8]);
    if (!hit) return;
    if (g_rendering) draw_end_rendering(cb, 5);  // the copy ends the pass anyway
    for (lazy148::Group& g : lazy148::g_groups) {
        uint32_t mask = 0;
        for (uint32_t k = 0; k < 10; k++)
            if ((g.pending >> k & 1) && touched(g.images[k < 8 ? k : 8])) mask |= 1u << k;
        if (mask) lazy148::resolve(cb, g, mask, lazy148::ClearCopy);
    }
    lazy148::compact();
}
void fast89_flush_invalidate(VkCommandBuffer cb) {
    batch91::flush(cb);
    fast89::invalidate();
    if (lazy148::on() && !g_rendering) lazy148::flush_all(cb, lazy148::ClearCopy);
}

uint64_t draw_take_dump_request() {
    uint64_t r = g_dump_request;
    g_dump_request = 0;
    return r;
}

}  // namespace nx::nvn
