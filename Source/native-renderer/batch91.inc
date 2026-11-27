// batch91: tower block draws batched into instanced draws (included in nvn_draw.cpp after fast89.inc).
//
// A block draw whose translation matches a captured template (fast89::match: same pass, targets, program, state,
// textures, vertex/index bytes and uniform bytes, all but the bank-3 model matrix) is not recorded: its matrix is
// computed now, in the original order, by exactly build_sets' code (fast91_matrix_bytes, the interpolation included)
// and appended to the template's bucket. At a flush each bucket is one vkCmdDrawIndexed with N instances: the
// template's pipeline built with the instancing variant of its vertex shader (inst91: vp_c3 reads indexed by
// gl_InstanceIndex*4+k; otherwise the identical module) and the N matrices bound back to back. Buckets flush in the
// order their first draw arrived and instances keep arrival order, so each block's body still precedes its face.
// Only opaque draws of the six block vertex shaders (exact SPIR-V) batch: depth tested LESS/LEQUAL, no blending, and
// stencil at most keep/replace with depth writes under LESS; moving such draws later in the pass changes nothing but
// exact-depth ties. Flushes: before any other draw that blends, does not depth-test or uses stencil otherwise; before
// clears, copies and dispatches; at every pass end; at 1024 matrices; before a bucket's template slot is recaptured.
// CATHERINE_PARALLEL91=0: off.

}  // namespace (nvn_draw.cpp's)
}  // namespace nx::nvn
namespace nx::nvn::replay {
bool fast91_matrix_bytes(const ShaderInfo* s, const DescriptorBinding& b, int st, uint8_t* out);
}
namespace nx::nvn {
namespace {
namespace batch91 {

#include "inst91_sources.inc"  // kInst91: the six original modules (exact) and their instancing variants

bool on() {
    static const bool v = [] {
        const char* e = std::getenv("CATHERINE_PARALLEL91");
        return !(e && e[0] == '0') && fast89::mode_setting() != 0;  // also off under any debug/trace option
    }();
    return v;
}

// perf 2026-11-22: called for every draw. Same answer as comparing vs->hash with each kInst91 hash string,
// but a 16-character hash is first checked against the entries' first 8 bytes (an exact pre-filter).
bool block_shader(const ShaderInfo* vs) {
    static const std::array<uint64_t, std::size(kInst91)> prefixes = [] {
        std::array<uint64_t, std::size(kInst91)> p{};
        for (size_t k = 0; k < std::size(kInst91); ++k) {
            char head[8] = {};
            std::memcpy(head, kInst91[k].hash, std::min<size_t>(8, std::strlen(kInst91[k].hash)));
            std::memcpy(&p[k], head, 8);
        }
        return p;
    }();
    const std::string& h = vs->hash;
    if (h.size() >= 8) {
        uint64_t head;
        std::memcpy(&head, h.data(), 8);
        for (size_t k = 0; k < prefixes.size(); ++k)
            if (prefixes[k] == head && h == kInst91[k].hash) return true;
        return false;
    }
    for (const Inst91& m : kInst91)
        if (h == m.hash) return true;
    return false;
}

// perf 2026-11-22: adaptive batching. A scene that batched nothing during a 60-submit probe (the Day 5 bar: every
// capture refused, ~5% of the replay thread spent on the attempt) skips the template path for 1200 submits, then
// probes again. The tower boss batches every frame and stays on. Batching only regroups draws (exact), and the
// state changes only between submits, where pending batches and templates are already gone.
// CATHERINE_BATCH91_ADAPTIVE=0: always on, as before.
struct Adaptive { bool active = true; uint64_t left = 60, batched_at = 0, switches = 0; };
Adaptive g_adapt;
bool adaptive_on() {
    static const bool v = [] { const char* e = std::getenv("CATHERINE_BATCH91_ADAPTIVE"); return !(e && e[0] == '0'); }();
    return v;
}
bool active() { return !adaptive_on() || g_adapt.active; }

// The instancing variant of a block vertex shader (same hash, reflection and everything else), or null.
const ShaderInfo* variant_of(const ShaderInfo* vs) {
    static std::map<const ShaderInfo*, std::unique_ptr<ShaderInfo>> made;
    if (auto it = made.find(vs); it != made.end()) return it->second.get();
    std::unique_ptr<ShaderInfo> v;
    for (const Inst91& m : kInst91)
        if (vs->hash == m.hash && vs->spirv.size() == m.source_words &&
            std::memcmp(vs->spirv.data(), m.source, m.source_words * 4) == 0) {
            v = std::make_unique<ShaderInfo>(*vs);
            v->spirv.assign(m.variant, m.variant + m.variant_words);
            break;
        }
    const ShaderInfo* r = v.get();
    made.emplace(vs, std::move(v));
    return r;
}

bool order_free_state(const State& s) {
    const DepthStencilInfo& d = s.depth;
    if (!d.depth_test || (d.depth_func != 2 && d.depth_func != 4)) return false;  // NVN: 2 LESS, 4 LEQUAL
    for (int k = 0; k < 8; k++)
        if (s.color.blend_enable[k]) return false;
    if (s.alpha_to_coverage) return false;
    if (d.stencil_test) {
        if (!d.depth_write || d.depth_func != 2) return false;  // the nearest fragment must decide the stencil value
        for (int f = 0; f < 2; f++)
            if (d.stencil_fail[f] != 1 || d.depth_fail[f] != 1 || (d.depth_pass[f] != 1 && d.depth_pass[f] != 3))
                return false;  // NVN: 1 KEEP, 3 REPLACE
    }
    return true;
}


struct VulkanIndexedPacket {
    VulkanRasterState raster{};
    VkViewport viewport{};
    VkRect2D scissor{};
    bool viewport_uses_extent = false;
    bool scissor_uses_extent = false;
    uint32_t index_count = 0;
    int32_t vertex_offset = 0;
    uint32_t written_attachments = 0;
};

int packet_mode() {
    static const int mode = [] {
        const char* value = std::getenv("CATHERINE_VULKAN_TOWER_PACKET");
        return value ? std::atoi(value) : 1;
    }();
    return mode;
}

VulkanIndexedPacket prepare_vulkan_packet(const State& s, const Targets& t, const Cmd& draw) {
    VulkanIndexedPacket p{};
    const auto native = producer_raster::prepare(s,g_pass_extent);
    p.raster = native.dynamic;
    p.viewport_uses_extent = native.viewport_uses_extent;
    p.scissor_uses_extent = native.scissor_uses_extent;
    p.viewport = native.viewport;
    p.scissor = native.scissor;
    if (engine_vk::present(draw)) {
        const auto native = engine_vk::decode(draw);
        p.index_count = native.args.indexCount;
        p.vertex_offset = native.args.vertexOffset;
    } else {
        p.index_count = uint32_t(draw.i[0]);
        p.vertex_offset = draw.i[1];
    }
    p.written_attachments = ((1u << t.color_count) - 1) |
                            (native.writes_depth_stencil ? 1u << 8 : 0);
    return p;
}

struct Bucket {
    VulkanIndexedPacket packet;
    bool batchable = false;
    uint64_t captured = 0;                 // fast89::g_captures when its template was captured
    VkPipeline pipeline = VK_NULL_HANDLE;  // the template's pipeline with the instancing vertex shader
    std::vector<std::array<uint8_t, 64>> pending;
};
Bucket g_buckets[8192];  // one per fast89 template slot
std::vector<uint16_t> g_order;  // buckets holding matrices, in order of their first one
uint64_t g_batched = 0, g_instanced_draws = 0, g_flushes = 0, g_ineligible = 0;
uint64_t g_why[8] = {};  // after_capture: calls, template refused, draw shape, order-sensitive state, matrix size, no variant

bool any_pending() { return !g_order.empty(); }

void submit_tick() {
    if (!adaptive_on() || !on()) return;
    if (g_adapt.left > 1) { --g_adapt.left; return; }
    if (g_adapt.active && g_batched == g_adapt.batched_at) {
        g_adapt.active = false, g_adapt.left = 1200, ++g_adapt.switches;
    } else {
        if (!g_adapt.active) ++g_adapt.switches;
        g_adapt.active = true, g_adapt.left = 60, g_adapt.batched_at = g_batched;
    }
}

// After fast89::capture: mark the slot batchable (or not) and build its instancing pipeline.
void after_capture(const ShaderInfo* vs, const ShaderInfo* fs, const ProgramLayout& layout, const PipelineKey* key,const native_pipeline::Definition* definition,
                   uint32_t mode, bool indexed, const Cmd& c) {
    const size_t slot = fast89::g_slot97;  // cmd97: the slot capture() used
    if (slot == SIZE_MAX) return;          // capture() returned before choosing one
    fast89::Template& T = fast89::g_templates[slot];
    Bucket& B = g_buckets[slot];
    B.batchable = false;
    ++g_why[0];
    if (!T.valid || (T.legacy && !T.sets[0])) { ++g_why[1]; return; }
#if defined(__linux__) && !defined(__ANDROID__)
    // Linux keeps ordinary draws for this layout. The instancing descriptor-copy
    // path faults in the NVIDIA driver after cutscene playback on the RTX 5090.
    if (T.legacy) return;
#endif
    if (T.matrix_value < 0 || !indexed || c.i[2] != 0 || c.i[3] > 1) { ++g_why[2]; return; }
    if (!order_free_state(g_state)) { ++g_why[3]; return; }
    if (T.uniform_ranges[0][T.matrix_recipe.slot].size != 64) { ++g_why[4]; return; }
    const ShaderInfo* variant = variant_of(vs);
    if (!variant) { ++g_why[5]; return; }
    if (packet_mode()) B.packet = prepare_vulkan_packet(g_state, T.t, T.c);
    if(definition) {
        auto native=*definition;native.vs=variant;
        if(key){PipelineKey oracle=*key;oracle.vs=variant;native_pipeline::validate(native,oracle,layout);++native_pipeline::compared_requests;}
        ++native_pipeline::tower_requests;
        B.pipeline=graphics_pipeline_definition(native,layout);
        native_pipeline::report();
    } else {
        PipelineKey k = *key;
    k.vs = variant;
    B.pipeline = producer_fixed::mode()
        ? graphics_pipeline_native_fixed(k,layout,producer_pipeline::prepare(k,c),producer_fixed::prepare(k))
        : producer_pipeline::mode()
            ? graphics_pipeline_native(k,layout,producer_pipeline::prepare(k,c))
            : graphics_pipeline(k,layout);
    }
    if (!B.pipeline) return;
    B.captured = fast89::g_captures;
    B.batchable = true;
}

void flush(VkCommandBuffer cb);

// A block draw that matched template T: its matrix goes to the bucket instead of a draw. False: draw it normally.
bool enqueue(VkCommandBuffer cb, fast89::Template& T) {
    perf22::Scope timing(perf22::BatchEnqueue);
    const size_t slot = size_t(&T - fast89::g_templates);
    Bucket& B = g_buckets[slot];
    if (!B.batchable || B.captured == 0) return false;
    std::array<uint8_t, 64> m;
    {perf22::Scope timing_matrix(perf22::BatchMatrix);if (!replay::materials::matrix_bytes(T.matrix_recipe, m.data())) return false;}
    if (B.pending.empty()) g_order.push_back(uint16_t(slot));
    B.pending.push_back(m);
    ++g_batched;
    if (B.pending.size() >= 1024) flush(cb);
    return true;
}

// The State fields set_dynamic and the pass bookkeeping read, as the template had them.
void restore_template_state(const fast89::Template& T, State& saved) {
    saved = g_state;
    std::memcpy(reinterpret_cast<uint8_t*>(&g_state) + offsetof(State, blend), T.state.tail, sizeof T.state.tail);
}

// Legacy (descriptor set) layouts: a new set 0 holding the template's set 0 with the matrix binding widened to the
// N matrices (every other binding copied from the template's set), bound with the template's sets 1-3 the way
// bind_sets binds them (runs of present sets; these layouts have no dynamic offsets).
void bind_legacy(VkCommandBuffer cb, const fast89::Template& T, const VkDescriptorBufferInfo& matrices) {
    descriptor_bytes_legacy_bind(cb);
    VkDescriptorSet sets[4] = {T.sets[0], T.sets[1], T.sets[2], T.sets[3]};
    const VkDescriptorSet s0 = frame_descriptor_set(T.layout->sets[0]);
    const uint32_t matrix = T.matrix_recipe.destination.binding;
    thread_local std::vector<VkCopyDescriptorSet> copies;
    copies.clear();
    uint64_t seen[4] = {};  // set 0 bindings already copied (< 256)
    if(!replay::materials::mode()) {
    for (const ShaderInfo* s : T.shaders)
        for (const DescriptorBinding& b : s->refl.bindings) {
            if (b.set != 0 || b.binding == matrix || b.binding >= 256 || (seen[b.binding >> 6] >> (b.binding & 63) & 1)) continue;
            seen[b.binding >> 6] |= 1ull << (b.binding & 63);
            VkCopyDescriptorSet cp{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
            cp.srcSet = T.sets[0], cp.srcBinding = b.binding, cp.dstSet = s0, cp.dstBinding = b.binding;
            cp.descriptorCount = std::max(1u, b.count);
            copies.push_back(cp);
        }
    } else {
    for(const auto& b:replay::materials::plan(T.shaders,*T.layout).set0_copies) {
            if (b.set != 0 || b.binding == matrix || b.binding >= 256 || (seen[b.binding >> 6] >> (b.binding & 63) & 1)) continue;
            seen[b.binding >> 6] |= 1ull << (b.binding & 63);
            VkCopyDescriptorSet cp{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
            cp.srcSet = T.sets[0], cp.srcBinding = b.binding, cp.dstSet = s0, cp.dstBinding = b.binding;
            cp.descriptorCount = std::max(1u, b.count);
            copies.push_back(cp);
        }
    }
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = s0, w.dstBinding = matrix, w.descriptorCount = 1, w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &matrices;
    p_vkUpdateDescriptorSets(vk_device(), 1, &w, uint32_t(copies.size()), copies.data());
    sets[0] = s0;
    for (uint32_t k = 0; k < 4;) {
        if (!sets[k]) { k++; continue; }
        uint32_t n = 1;
        while (k + n < 4 && sets[k + n]) n++;
        p_vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, T.layout->layout, k, n, sets + k, 0, nullptr);
        k += n;
    }
}

void flush(VkCommandBuffer cb) {
    if (g_order.empty()) return;
    ++g_flushes;
    thread_local std::vector<DescriptorValue> values;
    const int mode = packet_mode();
    State saved;
    for (uint16_t slot : g_order) {
        fast89::Template& T = fast89::g_templates[slot];
        Bucket& B = g_buckets[slot];
        const uint32_t n = uint32_t(B.pending.size());
        if (!n) continue;
        const Upload u = upload_alloc(uint64_t(n) * 64, 256);
        std::memcpy(u.ptr, B.pending.data(), size_t(n) * 64);
        const VkDescriptorBufferInfo matrices{u.buffer, u.offset, uint64_t(n) * 64};
        if (mode != 1) restore_template_state(T, saved);
        if (g_bound.cb != cb) g_bound = Bound{}, g_bound.cb = cb;
        if (g_bound.pipeline != B.pipeline) {
            p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, B.pipeline);
            g_bound.pipeline = B.pipeline, g_state_set++;
        }
        if (T.legacy) {
            if (!T.sets[0]) { if (mode != 1) g_state = saved; B.pending.clear(); continue; }  // cannot happen: the matrix is in set 0
            bind_legacy(cb, T, matrices);
        } else {
            values = T.values;
            values[size_t(T.matrix_value)].buffer = matrices;
            bind_descriptor_values(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, *T.layout, values);
        }
        for (uint32_t s = 0; s < 15; s++)
            if (T.in.used >> s & 1) bind_vertex(cb, s, T.in.vertex[s]);
        bind_vertex(cb, 15, zero_stream());

        VkViewport vp{};
        VkRect2D sc{};
        if (mode != 1) {
            vp = {0, 0, float(g_pass_extent.width), float(g_pass_extent.height), 0, 1};
            if (g_state.has_viewport)
                vp = {float(g_state.viewport[0]), float(g_state.viewport[1]), float(g_state.viewport[2]),
                      float(g_state.viewport[3]), g_state.depth_near, g_state.depth_far};
            vp.y += vp.height;
            vp.height = -vp.height;
            sc = {{0, 0}, g_pass_extent};
            if (g_state.has_scissor)
                sc = {{std::max(0, g_state.scissor[0]), std::max(0, g_state.scissor[1])},
                      {uint32_t(std::max(0, g_state.scissor[2])), uint32_t(std::max(0, g_state.scissor[3]))}};
        }
        if (mode) {
            const VulkanIndexedPacket& p = B.packet;
            const VkViewport native_vp = p.viewport_uses_extent
                ? VkViewport{0, float(g_pass_extent.height), float(g_pass_extent.width), -float(g_pass_extent.height), 0, 1}
                : p.viewport;
            const VkRect2D native_sc = p.scissor_uses_extent ? VkRect2D{{0, 0}, g_pass_extent} : p.scissor;
            if (mode == 2) {
                const auto legacy_raster = vulkan_raster_state(g_state);
                const uint32_t written = ((1u << T.t.color_count) - 1) |
                    (native_draw_depth::writes(g_state.depth) ? 1u << 8 : 0);
                if (!same_bytes(vp, native_vp) || !same_bytes(sc, native_sc) ||
                    std::memcmp(p.raster.raster, legacy_raster.raster, sizeof p.raster.raster) ||
                    std::memcmp(p.raster.blend, legacy_raster.blend, sizeof p.raster.blend) ||
                    p.index_count != uint32_t(T.c.i[0]) || p.vertex_offset != T.c.i[1] ||
                    written != p.written_attachments)
                    fatal("Vulkan tower packet differs from compatibility state at slot %u", unsigned(slot));
                static uint64_t checked = 0;
                if (++checked == 1 || checked % 10000 == 0)
                    log("Vulkan tower packet verified: %llu batches", (unsigned long long)checked);
            }
            set_vulkan_dynamic(cb, native_vp, native_sc, p.raster);
        } else {
            set_dynamic(cb, vp, sc);
        }
        if (!(g_bound.has_index && g_bound.index.buffer == T.in.index.buffer && g_bound.index.offset == T.in.index.offset &&
              g_bound.index_type == T.in.index_type)) {
            p_vkCmdBindIndexBuffer(cb, T.in.index.buffer, T.in.index.offset, T.in.index_type);
            g_bound.has_index = true, g_bound.index = T.in.index, g_bound.index_type = T.in.index_type, g_state_set++;
        }
        if (mode) {
            p_vkCmdDrawIndexed(cb, B.packet.index_count, n, 0, B.packet.vertex_offset, 0);
            g_pass_written |= B.packet.written_attachments;
        } else {
            p_vkCmdDrawIndexed(cb, uint32_t(T.c.i[0]), n, 0, T.c.i[1], 0);
            g_pass_written |= ((1u << T.t.color_count) - 1) |
                              (native_draw_depth::writes(g_state.depth) ? 1u << 8 : 0);
        }
        g_stats.done += n;
        if (mode != 1) g_state = saved;
        B.pending.clear();
        ++g_instanced_draws;
    }
    g_order.clear();
}

}  // namespace batch91


