// draw89: draw templates (included in nvn_draw.cpp's anonymous namespace, after set_dynamic).
//
// A draw fully translated by draw() is kept as a template: its descriptor values, vertex/index uploads, pipeline,
// and a snapshot of everything its translation read. A later draw in the SAME render pass whose inputs are identical
// except for the vertex stage's bank-3 uniform (the per-object model matrix) replays the template: that one binding
// is computed by exactly the code build_sets runs for it (uniform_info89, including the model-matrix interpolation),
// and everything else is reused. What is compared before reuse:
//   the pass (same command buffer, same pass serial, rendering), the targets, the program/shaders/layout, the draw
//   command's parameters, every State field the translation reads (render state, vertex buffer ranges, attributes,
//   streams, textures, images, pools, storage ranges, dynamic state), the bytes of every other uniform binding, and
//   the bytes of the vertex streams and indices; none of those ranges may overlap a GPU-written range now.
// Templates are dropped at each submit, each ring wrap (memo87's epoch), each dispatch, clear, copy, target change
// (nvn_replay.cpp) and in-pass clear (pass_mark_written). Draws with storage buffers or images, quad lists, the UI
// gradient fix, or an open debug/trace option never become templates.
// CATHERINE_PARALLEL89=0: off. =2: verify (the normal path runs; the template's result is compared with it).

}  // namespace (nvn_draw.cpp's)
}  // namespace nx::nvn
namespace nx::nvn::replay {
VkDescriptorBufferInfo fast89_uniform_info(VkCommandBuffer cb, const ShaderInfo* s, const DescriptorBinding& b, int st);
bool fast89_values(std::vector<DescriptorValue>& out);
void fast92_sets(VkDescriptorSet* out);
namespace memo87 { extern std::atomic<uint64_t> g_epoch; }
}  // namespace nx::nvn::replay
namespace nx::nvn {
namespace {

namespace fast89 {

// nvn_sets.cpp's api_stage, identical.
int stage89(ShaderStage s) { return s == ShaderStage::Fragment ? 1 : s == ShaderStage::Compute ? 5 : 0; }

int mode_setting() {
    static const int m = [] {
        const char* e = std::getenv("CATHERINE_PARALLEL89");
        if (e && e[0] == '0') return 0;
        for (const char* name : {"NX_DRAW_SLOW", "NX_NO_DEDUP", "NX_SWAP_FS", "NX_SKIP_FS", "NX_SNAP_FS", "NX_SNAP_FS_BEFORE",
                                 "NX_TRACE_DRAWS", "NX_TRACE_CMDS", "NX_TRACE_PASSES", "NX_TRACE_SKIN", "NX_FRAME_SIG",
                                 "NX_SNAP_FOLLOW", "NX_DUMP_ON_TARGET", "NX_DUMP_ON_FS", "NX_CHECK_VERTS", "NX_GPU_PROFILE",
                                 "NX_TRACE_CUBE_DRAWS", "NX_TARGETS_MEMCMP"})
            if (std::getenv(name)) return 0;
        return e && e[0] == '2' ? 2 : 1;
    }();
    return m;
}

uint64_t g_epoch = 1;  // dispatches, clears, copies, target changes, submits
uint64_t g_hits = 0, g_misses = 0, g_captures = 0, g_verify_bad = 0, g_verify_ok = 0;
uint64_t g_cap_why[8] = {};  // capture refusals: not rendering, textures pending, quad/layout, storage/images, no compact values, uniform bytes, vertex bytes, index bytes
uint64_t g_cyc_draw_all = 0, g_cyc_dispatch_all = 0, g_draw_calls = 0;
uint64_t g_kind_ticks[3] = {}, g_kind_calls[3] = {};
// 0: a tower block mesh (the census's four signatures), 1: drawn from a CPU-skinned vertex stream, 2: anything else.
int draw_kind(bool indexed, const Cmd& c) {
    if (g_state.attribs.empty()) return 2;
    const auto& a = g_state.attribs[0];
    if (a.stream >= 16 || a.stream >= g_state.streams.size()) return 2;
    const auto& r = g_state.vertex_buffers[a.stream];
    const uint32_t stride = g_state.streams[a.stream].stride;
    if (indexed && ((r.size == 3420 && stride == 36 && c.i[0] == 420) || (r.size == 720 && stride == 36 && c.i[0] == 30) ||
                    (r.size == 816 && stride == 24 && c.i[0] == 144) || (r.size == 480 && stride == 24 && c.i[0] == 30)))
        return 0;
    const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
    return p && a.offset <= r.size && r.size - a.offset >= 12 && written_skin_output(p + a.offset, 12) ? 1 : 2;
}
uint64_t g_cyc_match_hit = 0, g_cyc_match_miss = 0, g_cyc_replay = 0, g_cyc_normal = 0, g_cyc_capture = 0;
void invalidate() { ++g_epoch; }

struct StateSnap {  // the State fields the translation reads, besides uniforms and the vectors
    uint64_t program;
    Range storage[2][16], vertex_buffers[16];
    uint64_t textures[2][32], images[2][16], texture_pool, sampler_pool;
    uint8_t tail[sizeof(State) - offsetof(State, blend)];
};
void snap_state(StateSnap& s) {
    const State& g = g_state;
    s.program = g.program;
    std::memcpy(s.storage, g.storage, sizeof s.storage);
    std::memcpy(s.vertex_buffers, g.vertex_buffers, sizeof s.vertex_buffers);
    std::memcpy(s.textures, g.textures, sizeof s.textures);
    std::memcpy(s.images, g.images, sizeof s.images);
    s.texture_pool = g.texture_pool, s.sampler_pool = g.sampler_pool;
    std::memcpy(s.tail, reinterpret_cast<const uint8_t*>(&g) + offsetof(State, blend), sizeof s.tail);
}

struct Bytes {  // a guest range and the bytes it held
    uint64_t address = 0, size = 0;
    std::vector<uint8_t> bytes;
};
bool same_bytes_now(const Bytes& b) {
    if (!b.size) return true;
    const uint8_t* p = guest_bytes(b.address);
    return p && std::memcmp(p, b.bytes.data(), b.size) == 0 && !written_overlaps(p, b.size);
}
uint64_t g_vertex_why96[4];  // cmd96: vertex refusals (null, over 64 KB, written) and early vertex refusals
uint64_t g_capture_calls96 = 0;
bool on96() {
    static const bool on = [] { const char* e = std::getenv("CATHERINE_PARALLEL96"); return !(e && e[0] == '0'); }();
    return on;
}
int mode104();
// cmd109: vertex/index ranges are kept without bytes when match() skips them, and taken without the 64 KB cap
// when verifying.
bool skip109(int bit) { const int m = mode104(); return (m & bit) && !(m & 8); }
bool take_ranged109(Bytes& out, uint64_t address, uint64_t size, int bit) {
    out.address = address, out.size = size, out.bytes.clear();
    if (!size) return true;
    const uint8_t* p = guest_bytes(address);
    if (!p || written_overlaps(p, size)) return false;
    if (!skip109(bit)) out.bytes.assign(p, p + size);
    return true;
}
bool take_bytes(Bytes& out, uint64_t address, uint64_t size) {
    out.address = address, out.size = size;
    if (!size) return true;
    const uint8_t* p = guest_bytes(address);
    if (!p || size > (64u << 10) || written_overlaps(p, size)) return false;
    out.bytes.assign(p, p + size);
    return true;
}

struct Template {
    bool valid = false;
    uint64_t key97 = 0;  // cmd97: the full key hash
    uint64_t epoch = 0, ring = 0, pass = 0;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    Targets t{};
    uint32_t mode = 0;
    bool indexed = false;
    Cmd c{};
    const ProgramLayout* layout = nullptr;
    std::vector<const ShaderInfo*> shaders;
    std::vector<VertexAttrib> attribs;
    std::vector<VertexStream> streams;
    StateSnap state;
    uint32_t tex_mask127[2] = {0, 0}, img_mask127[2] = {0, 0};  // cmd127: bound slots per stage (vertex, fragment)
    bool full127 = true;                                         // compare every slot (unknown bindings)
    std::vector<Bytes> uniforms;      // every uniform binding of sets 0/1 except the matrix, in build_sets order
    Range uniform_ranges[2][18];      // the bound ranges (address may move; sizes must match)
    int matrix_value = -1;            // the matrix binding's index in values (-1: none)
    const ShaderInfo* matrix_shader = nullptr;
    replay::materials::Recipe matrix_recipe;
    std::vector<DescriptorValue> values;
    bool legacy = false;                   // descriptor sets (no descriptor-buffer layout): sets[] instead of values
    VkDescriptorSet sets[4] = {};
    DrawInputs in;
    std::vector<Bytes> vertex;        // each used stream's guest range
    Bytes index;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
Template g_templates[8192];  // 8192 (was 256: ~100 live templates collided ~20 pairs, thrashing per run)

// The slot: the key draws must share, plus a fingerprint of what tells block types apart (fragment textures, vertex
// buffer addresses, stencil reference), so interleaved types keep templates of their own. match() still compares all.
size_t slot_of(const ShaderInfo* vs, const ShaderInfo* fs, const ProgramLayout* layout, uint32_t mode, bool indexed, const Cmd& c) {
    uint64_t h = reinterpret_cast<uintptr_t>(vs) * 0x9e3779b97f4a7c15ull ^ reinterpret_cast<uintptr_t>(fs) * 0xc2b2ae3d27d4eb4full ^
                 reinterpret_cast<uintptr_t>(layout) * 0x165667b19e3779f9ull ^ (uint64_t(mode) << 1 | indexed) ^ uint64_t(uint32_t(c.i[0])) << 20 ^ c.a[0];
    const State& g = g_state;
    for (int k = 0; k < 8; k++) h = (h ^ g.textures[1][k]) * 0x100000001b3ull;
    for (int k = 0; k < 4; k++) h = (h ^ g.vertex_buffers[k].address) * 0x100000001b3ull;
    h = (h ^ (uint64_t(g.stencil_ref[0]) | uint64_t(g.stencil_ref[1]) << 8)) * 0x100000001b3ull;
    h ^= h >> 29;
    return size_t(h * 0xbf58476d1ce4e5b9ull >> 51);
}
// cmd97: the same hash, all 64 bits (slot_of = key97 >> 51), and the 4-way group around it.
uint64_t key97(const ShaderInfo* vs, const ShaderInfo* fs, const ProgramLayout* layout, uint32_t mode, bool indexed, const Cmd& c) {
    uint64_t h = reinterpret_cast<uintptr_t>(vs) * 0x9e3779b97f4a7c15ull ^ reinterpret_cast<uintptr_t>(fs) * 0xc2b2ae3d27d4eb4full ^
                 reinterpret_cast<uintptr_t>(layout) * 0x165667b19e3779f9ull ^ (uint64_t(mode) << 1 | indexed) ^ uint64_t(uint32_t(c.i[0])) << 20 ^ c.a[0];
    const State& g = g_state;
    for (int k = 0; k < 8; k++) h = (h ^ g.textures[1][k]) * 0x100000001b3ull;
    for (int k = 0; k < 4; k++) h = (h ^ g.vertex_buffers[k].address) * 0x100000001b3ull;
    h = (h ^ (uint64_t(g.stencil_ref[0]) | uint64_t(g.stencil_ref[1]) << 8)) * 0x100000001b3ull;
    h ^= h >> 29;
    return h * 0xbf58476d1ce4e5b9ull;
}
size_t g_slot97 = SIZE_MAX;
uint64_t g_evictions97 = 0;
bool live97(const Template& T);
size_t find97(uint64_t key) {
    const size_t base = size_t(key >> 51);
    for (size_t k = 0; k < 4; k++)
        if (g_templates[base ^ k].valid && g_templates[base ^ k].key97 == key) return base ^ k;
    return SIZE_MAX;
}
size_t choose97(uint64_t key) {
    const size_t base = size_t(key >> 51);
    for (size_t k = 0; k < 4; k++)
        if (g_templates[base ^ k].key97 == key) return base ^ k;
    for (size_t k = 0; k < 4; k++)
        if (!live97(g_templates[base ^ k])) return base ^ k;
    return base;
}
bool same_cmd(const Cmd& a, const Cmd& b) {
    return a.op == b.op && a.n == b.n && a.mask == b.mask && std::memcmp(a.i, b.i, sizeof a.i) == 0 && a.a[0] == b.a[0];
}

// The set 0/1 uniform bindings in build_sets' order, with the matrix binding marked.
template <class F>
void for_buffer_bindings(const std::vector<const ShaderInfo*>& shaders,const ProgramLayout& layout,F&& f) {
    int index=0;
    if(!replay::materials::mode()) {
        for(const auto* s:shaders)for(const auto& b:s->refl.bindings) {
            if(b.set>=2)continue;
            const auto recipe=replay::materials::legacy_recipe(s,b);f(recipe,index++);
        }
    } else {
        const auto& material=replay::materials::plan(shaders,layout);
        replay::materials::verify_template(material,shaders);
        for(const auto& op:material.buffers)f(*op.recipe,index++);
    }
}
// Admission only controls whether a fully rendered draw is worth capturing.
// It never supplies draw values or permits template reuse. match() remains the
// exact admission authority for every later optimized draw.
bool admission_enabled(){static const bool enabled=[] {const char* e=std::getenv("CATHERINE_VULKAN_TEMPLATE_ADMISSION");return e&&std::atoi(e)!=0;}();return enabled;}
struct AdmissionCounter{uint64_t key=0,epoch=0,pass=0,ring=0;VkCommandBuffer cb=VK_NULL_HANDLE;uint8_t observations=0;};
AdmissionCounter g_admission[8192];
uint64_t g_admission_considered=0,g_admission_skipped=0,g_admission_eligible=0,g_admission_collisions=0;
bool admit_capture(VkCommandBuffer cb,uint64_t key){
    ++g_admission_considered;
    auto& entry=g_admission[size_t(key>>51)];
    const uint64_t ring=replay::memo87::g_epoch.load(std::memory_order_relaxed);
    const bool same_scope=entry.observations&&entry.epoch==g_epoch&&entry.pass==g_pass_serial&&entry.ring==ring&&entry.cb==cb;
    if(!same_scope||entry.key!=key){
        if(same_scope)++g_admission_collisions;
        entry={key,g_epoch,g_pass_serial,ring,cb,1};++g_admission_skipped;return false;
    }
    entry.observations=2;++g_admission_eligible;return true;
}

// After a normal draw that recorded: keep it as the template of its key, if it qualifies.
// cmd97: a template that could still match in this pass.
bool live97(const Template& T) { return T.valid && T.epoch == g_epoch && T.pass == g_pass_serial; }

void capture(VkCommandBuffer cb, const Targets& t, uint32_t mode, bool indexed, const Cmd& c,
             const std::vector<const ShaderInfo*>& shaders, const ShaderInfo* vs, const ShaderInfo* fs,
             const ProgramLayout& layout, VkPipeline pipeline, const DrawInputs& in, uint32_t gathered, bool quad_arrays) {
    g_slot97 = SIZE_MAX;
    if ((g_capture_calls96 & 0x3ffff) == 0x3ffff)
        log("cmd97: %llu live templates evicted by another key", (unsigned long long)g_evictions97);
    if ((++g_capture_calls96 & 0x3ffff) == 0)
        log("cmd96: %llu captures tried; vertex refusals: null %llu, over 64 KB %llu, written %llu; early vertex refusals %llu",
            (unsigned long long)g_capture_calls96, (unsigned long long)g_vertex_why96[0], (unsigned long long)g_vertex_why96[1],
            (unsigned long long)g_vertex_why96[2], (unsigned long long)g_vertex_why96[3]);
    if (!mode_setting() || !g_rendering) { ++g_cap_why[0]; return; }
    if (gathered) { ++g_cap_why[1]; return; }
    if (quad_arrays || layout.dynamic) { ++g_cap_why[2]; return; }
    if (vs->hash == "52421ce0a854d776") return;  // fit_screen_gradient's shader: its inputs are rewritten
    uint64_t key=0;
    const bool use_admission=admission_enabled();
    if(use_admission){key=key97(vs,fs,&layout,mode,indexed,c);if(!admit_capture(cb,key))return;}
    const auto material_metadata=replay::materials::template_metadata(shaders,layout);
    if(material_metadata.has_images||material_metadata.has_storage) {++g_cap_why[3];return;}
    if(!use_admission)key=key97(vs,fs,&layout,mode,indexed,c);
    g_slot97 = choose97(key);
    Template& T = g_templates[g_slot97];
    if (T.valid && T.key97 != key && live97(T)) ++g_evictions97;
    T.valid = false;
    T.key97 = key;
    // cmd96: the vertex and index refusals of the takes below, checked before the descriptor values and the
    // uniform bytes are copied (big meshes drawn with block shaders failed ~150x/frame after copying the rest).
    if (on96()) {
        auto refused96 = [](uint64_t address, uint64_t size) {
            if (!size) return false;
            const uint8_t* p = guest_bytes(address);
            return !p || written_overlaps(p, size);  // cmd109: no size cap (vertex/index are not compared per draw)
        };
        for (uint32_t s = 0; s < 15; s++)
            if ((in.used >> s & 1) && refused96(g_state.vertex_buffers[s].address, g_state.vertex_buffers[s].size)) {
                ++g_cap_why[6], ++g_vertex_why96[3];
                return;
            }
        if (indexed && refused96(c.a[0], uint64_t(uint32_t(c.i[0])) * index_size(c.mask))) { ++g_cap_why[7]; return; }
    }
    T.legacy = !layout.bytes[0];
    if (T.legacy) {
        T.values.clear();
        replay::fast92_sets(T.sets);
    } else if (!replay::fast89_values(T.values)) { ++g_cap_why[4]; return; }
    T.uniforms.clear(), T.matrix_value = -1, T.matrix_shader = nullptr, T.matrix_recipe = {};
    bool ok = true;
    for_buffer_bindings(shaders, layout, [&](const replay::materials::Recipe& recipe, int index) {
        if (!ok || !recipe.uniform) return;
        if (recipe.model_transform) {
            if (T.matrix_value >= 0) ok = false;  // one per draw
            T.matrix_value = index, T.matrix_shader = recipe.shader, T.matrix_recipe = recipe;
            return;
        }
        const Range r = replay::materials::uniform_range(recipe);
        Bytes bytes;
        const uint64_t size = r.address ? std::min<uint64_t>(r.size, 0x10000) : 0;
        if (!take_bytes(bytes, r.address, r.address && guest_bytes(r.address) ? size : 0)) ok = false;
        T.uniforms.push_back(std::move(bytes));
    });
    if (!ok || (!T.legacy && T.matrix_value >= int(T.values.size()))) { ++g_cap_why[5]; return; }
    T.vertex.clear();
    for (uint32_t s = 0; s < 15 && ok; s++)
        if (in.used >> s & 1) {
            const Range& r = g_state.vertex_buffers[s];
            Bytes bytes;
            if (!take_ranged109(bytes, r.address, r.size, 2)) ok = false;
            T.vertex.push_back(std::move(bytes));
        }
    if (!ok) { ++g_cap_why[6]; return; }
    if (indexed) {
        if (!take_ranged109(T.index, c.a[0], uint64_t(uint32_t(c.i[0])) * index_size(c.mask), 4)) { ++g_cap_why[7]; return; }
    } else {
        T.index = Bytes{};
    }
    T.epoch = g_epoch, T.ring = replay::memo87::g_epoch.load(std::memory_order_relaxed), T.pass = g_pass_serial;
    T.cb = cb, T.t = t, T.mode = mode, T.indexed = indexed, T.c = c, T.layout = &layout, T.shaders = shaders;
    T.attribs = g_state.attribs, T.streams = g_state.streams;
    snap_state(T.state);
    std::memcpy(T.tex_mask127,material_metadata.texture_mask,sizeof T.tex_mask127);
    std::memcpy(T.img_mask127,material_metadata.image_mask,sizeof T.img_mask127);
    T.full127=material_metadata.full_slots;
    std::memcpy(T.uniform_ranges, g_state.uniforms, sizeof T.uniform_ranges);
    T.in = in, T.pipeline = pipeline;
    T.valid = true;
    ++g_captures;
}

uint64_t g_wrong104[3], g_matches104 = 0;  // per kind: uniform, vertex, index
int mode104() {  // bits: 1 skip uniform bytes, 2 skip vertex bytes, 4 skip index bytes, 8 verify (compare all, count)
    static const int m = [] { const char* e = std::getenv("CATHERINE_PARALLEL104"); return e ? std::atoi(e) : 6; }();
    return m;
}
// The template a draw may replay, or null.
Template* match(VkCommandBuffer cb, const Targets& t, uint32_t mode, bool indexed, const Cmd& c,
                const std::vector<const ShaderInfo*>& shaders, const ShaderInfo* vs, const ShaderInfo* fs,
                const ProgramLayout& layout) {
    const size_t at97 = find97(key97(vs, fs, &layout, mode, indexed, c));
    if (at97 == SIZE_MAX) return nullptr;
    Template& T = g_templates[at97];
    if (!T.valid || T.epoch != g_epoch || T.ring != replay::memo87::g_epoch.load(std::memory_order_relaxed) ||
        !g_rendering || T.pass != g_pass_serial || T.cb != cb || T.layout != &layout || T.mode != mode ||
        T.indexed != indexed || !same_cmd(T.c, c) || std::memcmp(&T.t, &t, sizeof t) != 0 || T.shaders != shaders ||
        T.attribs.size() != g_state.attribs.size() || T.streams.size() != g_state.streams.size())
        return nullptr;
    if (!T.attribs.empty() && std::memcmp(T.attribs.data(), g_state.attribs.data(), T.attribs.size() * sizeof(VertexAttrib)))
        return nullptr;
    if (!T.streams.empty() && std::memcmp(T.streams.data(), g_state.streams.data(), T.streams.size() * sizeof(VertexStream)))
        return nullptr;
    if (T.full127) {
        StateSnap now;
        snap_state(now);
        if (std::memcmp(&now, &T.state, sizeof now) != 0) return nullptr;
    } else {  // cmd127: only what this template's draw consumes
        const State& g = g_state;
        if (g.program != T.state.program || g.texture_pool != T.state.texture_pool || g.sampler_pool != T.state.sampler_pool)
            return nullptr;
        if (std::memcmp(reinterpret_cast<const uint8_t*>(&g) + offsetof(State, blend), T.state.tail, sizeof T.state.tail) != 0)
            return nullptr;
        for (uint32_t s127 = 0; s127 < 15; s127++)
            if ((T.in.used >> s127 & 1) && std::memcmp(&g.vertex_buffers[s127], &T.state.vertex_buffers[s127], sizeof(Range)) != 0)
                return nullptr;
        for (int st = 0; st < 2; st++) {
            for (uint32_t m = T.tex_mask127[st]; m; m &= m - 1)
                if (g.textures[st][__builtin_ctz(m)] != T.state.textures[st][__builtin_ctz(m)]) return nullptr;
            for (uint32_t m = T.img_mask127[st]; m; m &= m - 1)
                if (g.images[st][__builtin_ctz(m)] != T.state.images[st][__builtin_ctz(m)]) return nullptr;
        }
    }
    for (int st = 0; st < 2; st++)
        for (int k = 0; k < 18; k++)
            if (g_state.uniforms[st][k].size != T.uniform_ranges[st][k].size ||
                !g_state.uniforms[st][k].address != !T.uniform_ranges[st][k].address)
                return nullptr;
    const int m104 = mode104();
    const bool verify104 = m104 & 8;
    size_t u = 0;
    bool ok = true;
    bool bad104[3] = {false, false, false};
    if (verify104 || !(m104 & 1)) for_buffer_bindings(shaders, layout, [&](const replay::materials::Recipe& recipe, int) {
        if (!recipe.uniform || recipe.model_transform) return;
        const Range r = replay::materials::uniform_range(recipe);
        Bytes& want = T.uniforms[u++];
        if (want.size) {
            const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
            if (!(p && std::memcmp(p, want.bytes.data(), want.size) == 0 && !written_overlaps(p, want.size))) bad104[0] = true;
        } else {
            if (!(!r.address || !guest_bytes(r.address) || !std::min<uint64_t>(r.size, 0x10000))) bad104[0] = true;
        }
    });
    if (verify104 || !(m104 & 2)) {
        size_t v = 0;
        for (uint32_t s = 0; s < 15; s++)
            if (T.in.used >> s & 1)
                if (!same_bytes_now(T.vertex[v++])) bad104[1] = true;
    }
    if ((verify104 || !(m104 & 4)) && indexed && !same_bytes_now(T.index)) bad104[2] = true;
    if (verify104) {
        ++g_matches104;
        for (int k = 0; k < 3; k++) g_wrong104[k] += bad104[k];
        if ((g_matches104 & 0xfffff) == 0)
            log("cmd104 verify: %llu matches; bytes changed within a template life: uniform %llu, vertex %llu, index %llu",
                (unsigned long long)g_matches104, (unsigned long long)g_wrong104[0], (unsigned long long)g_wrong104[1],
                (unsigned long long)g_wrong104[2]);
    }
    for (int k = 0; k < 3; k++)
        if (bad104[k] && (verify104 || !(m104 >> k & 1))) return nullptr;
    return &T;
}

// Verify mode: the normal path ran for a draw the template matched; compare what it produced with the template.
uint64_t g_verify_moved = 0;
void verify(const Template& T, VkPipeline pipeline, const DrawInputs& in) {
    thread_local std::vector<DescriptorValue> now;
    bool bad = pipeline != T.pipeline || in.used != T.in.used || in.index_type != T.in.index_type;
    bool moved = false;
    for (uint32_t s = 0; s < 15 && !bad; s++)
        if (in.used >> s & 1) moved |= in.vertex[s].buffer != T.in.vertex[s].buffer || in.vertex[s].offset != T.in.vertex[s].offset;
    if (T.indexed) moved |= in.index.buffer != T.in.index.buffer || in.index.offset != T.in.index.offset;
    if (!replay::fast89_values(now) || now.size() != T.values.size()) bad = true;
    for (size_t k = 0; k < now.size() && !bad; k++) {
        const DescriptorValue &a = now[k], &b = T.values[k];
        if (a.type != b.type || a.binding != b.binding || a.set != b.set || a.uses_buffer != b.uses_buffer) { bad = true; break; }
        if (int(k) == T.matrix_value) continue;  // computed per draw in both
        if (a.uses_buffer) {
            if (a.buffer.range != b.buffer.range) bad = true;
            else moved |= a.buffer.buffer != b.buffer.buffer || a.buffer.offset != b.buffer.offset;
        } else if (std::memcmp(&a.image, &b.image, sizeof a.image) != 0) {
            bad = true;
        }
    }
    if (bad) ++g_verify_bad;
    else if (moved) ++g_verify_moved;
    else ++g_verify_ok;
}

}  // namespace fast89

// The template's replay: the tail of draw() with the template's results (and the matrix binding computed now).
bool fast89_replay(VkCommandBuffer cb, const Targets& t, uint32_t mode, bool indexed, const Cmd& c,
                   const ShaderInfo* vs, const ShaderInfo* fs, const ProgramLayout& layout, fast89::Template& T) {
    thread_local std::vector<DescriptorValue> values;
    values = T.values;
    if (T.matrix_value >= 0)
        values[size_t(T.matrix_value)].buffer =
            replay::materials::uniform_info(cb, T.matrix_recipe);
    if (g_bound.cb != cb) g_bound = Bound{}, g_bound.cb = cb;
    if (g_bound.pipeline != T.pipeline) {
        p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, T.pipeline);
        g_bound.pipeline = T.pipeline, g_state_set++;
    } else {
        g_state_kept++;
    }
    bind_descriptor_values(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, values);
    for (uint32_t s = 0; s < 15; s++)
        if (T.in.used >> s & 1) bind_vertex(cb, s, T.in.vertex[s]);
    bind_vertex(cb, 15, zero_stream());
    VkViewport vp{0, 0, float(g_pass_extent.width), float(g_pass_extent.height), 0, 1};
    if (g_state.has_viewport)
        vp = {float(g_state.viewport[0]), float(g_state.viewport[1]), float(g_state.viewport[2]),
              float(g_state.viewport[3]), g_state.depth_near, g_state.depth_far};
    vp.y += vp.height, vp.height = -vp.height;
    VkRect2D sc{{0, 0}, g_pass_extent};
    if (g_state.has_scissor)
        sc = {{std::max(0, g_state.scissor[0]), std::max(0, g_state.scissor[1])},
              {uint32_t(std::max(0, g_state.scissor[2])), uint32_t(std::max(0, g_state.scissor[3]))}};
    set_dynamic(cb, vp, sc);
    if (indexed) {
        uint32_t count = uint32_t(c.i[0]);
        int32_t vertex_offset = c.i[1];
        if (g_bound.has_index && g_bound.index.buffer == T.in.index.buffer && g_bound.index.offset == T.in.index.offset &&
            g_bound.index_type == T.in.index_type) {
            g_state_kept++;
        } else {
            p_vkCmdBindIndexBuffer(cb, T.in.index.buffer, T.in.index.offset, T.in.index_type);
            g_bound.has_index = true, g_bound.index = T.in.index, g_bound.index_type = T.in.index_type, g_state_set++;
        }
        p_vkCmdDrawIndexed(cb, count, uint32_t(std::max(1, c.i[3])), 0, vertex_offset, uint32_t(c.i[2]));
    } else {
        p_vkCmdDraw(cb, uint32_t(c.i[1]), uint32_t(std::max(1, c.i[3])), uint32_t(c.i[0]), uint32_t(c.i[2]));
    }
    g_stats.done++;
    if (g_attachment_only && (!shader_scope_cached(vs) || !shader_scope_cached(fs))) g_attachment_only = false;
    g_pass_written |= ((1u << t.color_count) - 1) | (native_draw_depth::writes(g_state.depth) ? 1u << 8 : 0);
    ++fast89::g_hits;
    return true;
}
