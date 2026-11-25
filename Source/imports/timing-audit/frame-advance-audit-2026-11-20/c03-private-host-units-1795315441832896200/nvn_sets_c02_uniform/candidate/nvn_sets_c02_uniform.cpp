#include "C:/catherine/recomp/Source/imports/timing-audit/frame-advance-audit-2026-11-20/safety-root-candidate-1795300767913069600/candidate/chain_rotation_profiles.h"
#include "C:/catherine/recomp/Source/imports/timing-audit/frame-advance-audit-2026-11-20/safety-root-candidate-1795300767913069600/candidate/rigid_motion_trace.h"
// The descriptor sets of a draw or dispatch (split from nvn_draw.cpp): the textures its bindings name, resolved once
// per draw (gather_textures), the sets built from the bound state (build_sets) and bound (bind_sets), and the draw's
// shared inputs. The replay's CPU shortcuts of docs/nvn/PERF-PLAN.md live here and in nvn_draw.cpp; NX_DRAW_SLOW
// turns them off (draw_slow, nvn_draw_state.h). Everything here runs under the GPU lock, one draw at a time.
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "nvn_descriptor_bytes.h"
#include "nvn_draw_state.h"
#include "engine_vk_indexed.h"
#include "native_material_plans.h"
#include "native_image_contract.h"
#include "native_texture_bindings.h"
#include "native_pool_publisher.h"
#include "watched-geometry.h"
#include "vincent_pose_upload.h"
#include "rigid_late_sample_interpolation.h"
#include "spike_pose_upload.h"
#include "campaign_block_pose_upload.h"
#include "block20_pose_upload.h"
#include "binding_memo87.h"
#include "model_tracker81.h"
#include "puzzle_memo81.h"

#include "c02_uniform_provenance.h"
#include "c03_command_packet.h"
#include "root9_uniform_packet.h"
#include "root9_translation.h"
#include "root12_translation.h"
#include "root15_family.h"

namespace nx::nvn::replay {

using namespace nx::vk;

namespace {
bool& generic_c3_active88() { static bool active=false;return active; }
#include "puzzle_transform88.inc"
}  // namespace

DrawTextures g_textures;

bool draw_slow(uint32_t what) {
    static const uint32_t mask = [] {
        const char* e = std::getenv("NX_DRAW_SLOW");
        if (!e) return 0u;
        uint32_t m = 0;
        for (; *e; e++) {  // only letters of "mehsu": those; anything else (1): all
            const char* letters = "mehsu";
            const char* at = std::strchr(letters, *e);
            if (!at) return ~0u;
            m |= 1u << (at - letters);
        }
        return m ? m : ~0u;
    }();
    return (mask & what) != 0;
}

uint32_t handle_texture(uint64_t handle, uint32_t* sampler) {
    if (sampler) *sampler = uint32_t(handle >> 20) & 0xFFF;
    return uint32_t(handle & 0xFFFFF);
}

namespace {

int api_stage(ShaderStage s) { return s == ShaderStage::Fragment ? 1 : s == ShaderStage::Compute ? 5 : 0; }

// The texture and sampler a handle names in the bound pools, looked up (texture_for without its cache).
// `has_sampler`: the sampler pool had the slot (`sampler` is left alone otherwise).
void lookup_handle(uint64_t handle, uint64_t& texture, SamplerInfo& sampler, ViewInfo* view,
                   bool* has_sampler = nullptr) {
    uint32_t sid = 0;
    uint32_t tid = handle_texture(handle, &sid);
    texture = 0;
    if (view) *view = {};
    // The pools' maps are read under the objects lock, and at the replay's mark (nvn_incarnation.cpp): the game's
    // recording thread registers the next frame's slots meanwhile (98% of the registrations, DEBUG.md #56).
    auto* tp = get<DescriptorPool>(g_state.texture_pool);
    auto* sp = get<DescriptorPool>(g_state.sampler_pool);
    PoolEntry e;
    {
        std::shared_lock lk(objects_lock());
        // A slot empty at the mark was registered after the lists were recorded but before the submit: the current
        // entry (NVN reads descriptors when the GPU runs; DEBUG.md #64). NX_MARK_EMPTY_OLD=1: the empty entry.
        if (tp && (!pool_entry_at(g_state.texture_pool, tid, t_replay_mark, e) || (!e.has && !mark_empty_old()))) {
            e = {};
            if (auto it = tp->textures.find(tid); it != tp->textures.end()) e.has = true, e.texture = it->second;
            if (auto it = tp->snapshots.find(tid); it != tp->snapshots.end()) e.snapshot = it->second;
            if (auto it = tp->views.find(tid); it != tp->views.end()) e.has_view = true, e.view = it->second;
        }
        SamplerSlot ss;
        // A slot empty at the mark was registered after the lists were recorded but before the submit (NVN reads
        // descriptors when the GPU runs): the current entry is the one.
        if (sp && sampler_entry_at(g_state.sampler_pool, sid, t_replay_mark, ss) && ss.has) {
            sampler = ss.info;
            if (has_sampler) *has_sampler = true;
        } else if (sp) {
            if (auto it = sp->samplers.find(sid); it != sp->samplers.end()) {
                sampler = it->second;
                if (has_sampler) *has_sampler = true;
            }
        }
    }
    if (tp) {
        texture = e.texture;
        // The slot's texture gone or re-initialized as another kind: the copy made at registration (nvn_objects.cpp).
        // NX_SNAPSHOT_SHAPE=1 also uses the copy when only the memory or size changed (the pool's descriptor copy):
        // it broke the course's foreground flowers (dynamic-resolution targets re-initialized every frame are meant
        // to be followed) and did not light the Goombas (docs/nvn/DEBUG.md #45).
        static const bool kind_only = std::getenv("NX_SNAPSHOT_SHAPE") == nullptr;
        // Re-initialized over other memory: the struct now holds another texture, while the pool's descriptor still
        // names the registered memory. Wonder registers its 64x36 aerial-haze texture in a new slot every frame and
        // reuses the struct for a dynamic-resolution target of the same format before the replay: 6.5% of course
        // frames sampled that target instead, and the background lost its haze for a frame (DEBUG-2 #92). A target
        // re-initialized in place (same memory, new size) is still followed (#45). NX_SNAPSHOT_MEMORY=0: as before.
        static const bool memory_rule = [] {
            const char* e = std::getenv("NX_SNAPSHOT_MEMORY");
            return !(e && e[0] == '0');
        }();
        if (texture && e.snapshot) {
            Texture* now = get<Texture>(texture);
            Texture* then = get<Texture>(e.snapshot);
            bool moved = now && then && !kind_only &&
                         (now->memory != then->memory || now->width != then->width || now->height != then->height ||
                          now->depth != then->depth || now->levels != then->levels);
            moved = moved || (now && then && memory_rule && now->memory != then->memory);
            if (then && (!now || moved || now->target != then->target || now->format != then->format)) texture = e.snapshot;
        }
        if (view && e.has_view) *view = e.view;
    }
}

void lookup_handle_published(uint64_t handle,uint64_t& texture,SamplerInfo& sampler,ViewInfo* view,bool* has_sampler,native_pools::SamplerDescription& plan) {
    if(!native_pools::mode())return lookup_handle(handle,texture,sampler,view,has_sampler);
    uint32_t sid = 0;
    uint32_t tid = handle_texture(handle, &sid);
    texture = 0;
    if (view) *view = {};
    // The pools' maps are read under the objects lock, and at the replay's mark (nvn_incarnation.cpp): the game's
    // recording thread registers the next frame's slots meanwhile (98% of the registrations, DEBUG.md #56).
    auto* tp = get<DescriptorPool>(g_state.texture_pool);
    auto* sp = get<DescriptorPool>(g_state.sampler_pool);
    PoolEntry e;
    {
        std::shared_lock lk(objects_lock());
        PoolEntry published;native_pools::SamplerDescription owned;
        const bool admitted=native_pools::select(g_state.texture_pool,tp,tid,g_state.sampler_pool,sp,sid,t_replay_mark,published,owned);
        if(!admitted||native_pools::mode()==2) {
            bool old_has=false;SamplerInfo old_sampler=sampler;
        // A slot empty at the mark was registered after the lists were recorded but before the submit: the current
        // entry (NVN reads descriptors when the GPU runs; DEBUG.md #64). NX_MARK_EMPTY_OLD=1: the empty entry.
        if (tp && (!pool_entry_at(g_state.texture_pool, tid, t_replay_mark, e) || (!e.has && !mark_empty_old()))) {
            e = {};
            if (auto it = tp->textures.find(tid); it != tp->textures.end()) e.has = true, e.texture = it->second;
            if (auto it = tp->snapshots.find(tid); it != tp->snapshots.end()) e.snapshot = it->second;
            if (auto it = tp->views.find(tid); it != tp->views.end()) e.has_view = true, e.view = it->second;
        }
        SamplerSlot ss;
        // A slot empty at the mark was registered after the lists were recorded but before the submit (NVN reads
        // descriptors when the GPU runs): the current entry is the one.
        if (sp && sampler_entry_at(g_state.sampler_pool, sid, t_replay_mark, ss) && ss.has) {
            old_sampler = ss.info;
            old_has = true;
        } else if (sp) {
            if (auto it = sp->samplers.find(sid); it != sp->samplers.end()) {
                old_sampler = it->second;
                old_has = true;
            }
        }
            if(admitted)native_pools::verify_selection(published,e,owned,old_has,old_sampler);
            else {sampler=old_sampler;if(has_sampler)*has_sampler=old_has;}
        }
        if(admitted) {e=published;plan=std::move(owned);if(plan)sampler=plan->source;if(has_sampler)*has_sampler=bool(plan);}

    }
    if (tp) {
        texture = e.texture;
        // The slot's texture gone or re-initialized as another kind: the copy made at registration (nvn_objects.cpp).
        // NX_SNAPSHOT_SHAPE=1 also uses the copy when only the memory or size changed (the pool's descriptor copy):
        // it broke the course's foreground flowers (dynamic-resolution targets re-initialized every frame are meant
        // to be followed) and did not light the Goombas (docs/nvn/DEBUG.md #45).
        static const bool kind_only = std::getenv("NX_SNAPSHOT_SHAPE") == nullptr;
        // Re-initialized over other memory: the struct now holds another texture, while the pool's descriptor still
        // names the registered memory. Wonder registers its 64x36 aerial-haze texture in a new slot every frame and
        // reuses the struct for a dynamic-resolution target of the same format before the replay: 6.5% of course
        // frames sampled that target instead, and the background lost its haze for a frame (DEBUG-2 #92). A target
        // re-initialized in place (same memory, new size) is still followed (#45). NX_SNAPSHOT_MEMORY=0: as before.
        static const bool memory_rule = [] {
            const char* e = std::getenv("NX_SNAPSHOT_MEMORY");
            return !(e && e[0] == '0');
        }();
        if (texture && e.snapshot) {
            Texture* now = get<Texture>(texture);
            Texture* then = get<Texture>(e.snapshot);
            bool moved = now && then && !kind_only &&
                         (now->memory != then->memory || now->width != then->width || now->height != then->height ||
                          now->depth != then->depth || now->levels != then->levels);
            moved = moved || (now && then && memory_rule && now->memory != then->memory);
            if (then && (!now || moved || now->target != then->target || now->format != then->format)) texture = e.snapshot;
        }
        if (view && e.has_view) *view = e.view;
    }
}

}  // namespace

// NX_FRAME_SIG's watched draws: how a handle's texture slot resolves (the mark's entry against the current one).
std::string explain_texture_slot(uint64_t handle) {
    uint32_t sid = 0, tid = handle_texture(handle, &sid);
    auto* tp = get<DescriptorPool>(g_state.texture_pool);
    if (!tp) return "no pool";
    std::shared_lock lk(objects_lock());
    PoolEntry e;
    bool at_mark = pool_entry_at(g_state.texture_pool, tid, t_replay_mark, e);
    auto cur = tp->textures.find(tid);
    auto cs = tp->snapshots.find(tid);
    auto desc = [](uint64_t key) {
        auto o = objects().find(key);
        auto* t = o == objects().end() ? nullptr : dynamic_cast<Texture*>(o->second.get());
        char b[96];
        std::snprintf(b, sizeof b, "%llx(%ux%u f%x m%llx)", (unsigned long long)(key & 0xFFFFF), t ? t->width : 0,
                      t ? t->height : 0, t ? t->format : 0, (unsigned long long)(t ? t->memory : 0));
        return std::string(b);
    };
    char b[160];
    std::snprintf(b, sizeof b, "{id %x mark %llu now %llu: ", tid, (unsigned long long)t_replay_mark,
                  (unsigned long long)current_epoch());
    std::string s = b;
    if (at_mark) s += "at mark " + (e.has ? desc(e.texture) : std::string("empty")) + " snap " + desc(e.snapshot) + "; ";
    s += "current " + (cur != tp->textures.end() ? desc(cur->second) : std::string("none")) + " snap " +
         (cs != tp->snapshots.end() ? desc(cs->second) : std::string("none")) + "}";
    return s;
}

namespace {

// What a handle resolves to, once per submit (docs/nvn/PERF-PLAN.md step 4). The replay's mark (nvn_incarnation.cpp,
// DEBUG.md #55-#57) makes (texture pool, sampler pool, handle) -> (texture, its object, view) constant within a
// submit: every lookup answers as of the mark, and what the mark resolves to is freed only by drain_objects, between
// replays. Sampler slots have no such history; one registered again during a replay keeps its first value here. The
// image's state is not cached (sampled_ready and sampled_view run for every draw). Open addressing, no removal;
// emptied by bumping the generation.
struct HandleSlot {
    uint64_t generation = 0;  // g_handle_generation when filled
    uint64_t handle = 0, texture_pool = 0, sampler_pool = 0;  // the key (handle: its 32 bits)
    uint64_t texture = 0;       // as lookup_handle gives it
    Texture* object = nullptr;  // get<Texture>(texture)
    bool has_sampler = false;
    SamplerInfo sampler;
    native_pools::SamplerDescription published_sampler;
    ViewInfo view;
    VkSampler vk_sampler[2] = {};  // sampler_for(sampler, shadow), once a binding asked
};
constexpr uint32_t kHandleSlots = 2048;  // a course frame names a few hundred handles
HandleSlot g_handle_slots[kHandleSlots];
uint64_t g_handle_generation = 1, g_handle_mark = 0;
uint32_t g_handle_used = 0;
uint64_t g_handle_hits = 0, g_handle_misses = 0, g_sets_skipped = 0;

void handles_reset() { g_handle_generation++, g_handle_used = 0; }

// The slot of `handle` in the bound pools, filled on first use; null without a replay mark (NX_TEXTURE_RACE_OLD: a
// lookup then answers as of now) or with NX_DRAW_SLOW. Valid until the next call.
HandleSlot* handle_slot(uint64_t handle) {
    uint64_t mark = t_replay_mark;
    if (!mark || draw_slow(kSlowHandles)) return nullptr;
    if (mark != g_handle_mark || g_handle_used > kHandleSlots / 2) handles_reset(), g_handle_mark = mark;
    handle &= 0xFFFFFFFFull;  // 20 bits of texture id, 12 of sampler id
    uint64_t tp = g_state.texture_pool, sp = g_state.sampler_pool;
    uint64_t h = (handle + 1) * 0x9E3779B97F4A7C15ull ^ tp * 0xC2B2AE3D27D4EB4Full ^ sp * 0x165667B19E3779F9ull;
    for (uint32_t k = uint32_t(h >> 40);; k++) {
        HandleSlot& s = g_handle_slots[k & (kHandleSlots - 1)];
        if (s.generation == g_handle_generation) {
            if (s.handle != handle || s.texture_pool != tp || s.sampler_pool != sp) continue;
            g_handle_hits++;
            if(native_pools::mode()==2&&s.published_sampler) {
                if(std::memcmp(&s.sampler,&s.published_sampler->source,sizeof s.sampler))fatal("Published sampler first-observation cache changed");
                for(unsigned depth=0;depth!=2;++depth)if(s.vk_sampler[depth])verify_published_sampler(*s.published_sampler,depth!=0);
                ++native_pools::census.compared;
            }
            return &s;
        }
        s = HandleSlot{};
        s.generation = g_handle_generation, s.handle = handle, s.texture_pool = tp, s.sampler_pool = sp;
        lookup_handle_published(handle,s.texture,s.sampler,&s.view,&s.has_sampler,s.published_sampler);
        s.object = s.texture ? get<Texture>(s.texture) : nullptr;
        g_handle_used++, g_handle_misses++;
        return &s;
    }
}

// Host data every draw binds (the support buffer, c[0]) is the same for most draws of a submit, and was built and
// copied for each stage of each draw (~11 KB per draw). DEBUG.md #30's rule stands: dedup shares bytes by address, so
// only bytes that do not change during the submit may go through it, and the stack copies these were built in did.
// Here each distinct content gets a block of its own for the submit, found by what it is made from, and dedup keeps
// one upload of it. The blocks are reused from sets_begin_submit on, where dedup starts over too.
struct SupportBlock {
    float width = 0, height = 0;  // the key: support_buffer_set_viewport's arguments
    SupportBuffer sb;
};
struct ConstantsBlock {
    int hw = 0;         // the key: the Maxwell stage (vertex 0, fragment 4) and the stage's storage bindings,
    Range storage[16];  // all driver_constants reads outside compute
    std::array<uint8_t, 0x1000> bytes;
};
constexpr size_t kBlocks = 32;  // more distinct ones in a submit: copied per draw, as before
std::vector<std::unique_ptr<SupportBlock>> g_support;
std::vector<std::unique_ptr<ConstantsBlock>> g_constants;
size_t g_support_used = 0, g_constants_used = 0;

const SupportBuffer* support_block(float width, float height) {
    for (size_t k = 0; k < g_support_used; k++)
        if (std::memcmp(&g_support[k]->width, &width, 4) == 0 && std::memcmp(&g_support[k]->height, &height, 4) == 0)
            return &g_support[k]->sb;
    if (g_support_used == kBlocks) return nullptr;
    if (g_support_used == g_support.size()) g_support.push_back(std::make_unique<SupportBlock>());
    SupportBlock& b = *g_support[g_support_used++];
    b.width = width, b.height = height;
    b.sb = support_buffer_defaults();
    support_buffer_set_viewport(b.sb, width, height);
    return &b.sb;
}

const uint8_t* constants_block(ShaderStage stage, int st) {
    if (stage == ShaderStage::Compute) return nullptr;  // its table also lists the uniform blocks; a few per frame
    int hw = stage == ShaderStage::Fragment ? 4 : 0;
    const Range* key = g_state.storage[st];
    for (size_t k = 0; k < g_constants_used; k++)
        if (g_constants[k]->hw == hw && std::memcmp(g_constants[k]->storage, key, sizeof g_constants[k]->storage) == 0)
            return g_constants[k]->bytes.data();
    if (g_constants_used == kBlocks) return nullptr;
    if (g_constants_used == g_constants.size()) g_constants.push_back(std::make_unique<ConstantsBlock>());
    ConstantsBlock& b = *g_constants[g_constants_used++];
    b.hw = hw;
    std::memcpy(b.storage, key, sizeof b.storage);
    b.bytes = driver_constants(stage, st);
    return b.bytes.data();
}

// The buffer a descriptor points to: `bytes` copied into the upload buffer (at least 16 bytes, zero padded).
// Storage buffers written by compute shaders go back to guest memory after the submit (the GPU's writes are what the
// game and later work read; WriteBack in nvn_vk.h). One copy per written range, shared by the later work of the
// submit (nvn_written.cpp); g_write_backs: NX_COMPUTE_SEPARATE=1, a copy per binding.
std::vector<WriteBack> g_write_backs;
bool all_write_backs() {  // NX_WRITE_BACK_ALL=1: every compute storage binding, written or not (bisecting)
    static const bool on = std::getenv("NX_WRITE_BACK_ALL") != nullptr;
    return on;
}

// guest: the bytes are guest memory (a copy is shared by later draws of the submit); host scratch data (the support
// buffer, c[0]: on the stack, at the same address for every draw) is copied every time (docs/nvn/DEBUG.md #30).
VkDescriptorBufferInfo buffer_of(VkCommandBuffer cb, const void* bytes, uint64_t size, uint64_t min_size = 16,
                                 bool write_back = false, bool guest = true) {
    uint64_t n = std::max<uint64_t>((size + 15) & ~15ull, min_size);
    const auto* p = static_cast<const uint8_t*>(bytes);
    if (!write_back && guest) {
        Upload u = read_upload(cb, p, size, n, 256);
        return {u.buffer, u.offset, n};
    }
    if (write_back && p && size && !written_separate()) {
        Upload u = written_bind(cb, p, size, n);
        return {u.buffer, u.offset, n};
    }
    Upload u = upload_alloc(n, 256);
    std::memset(u.ptr, 0, n);
    if (bytes && size) std::memcpy(u.ptr, bytes, size);
    // Only what a compute shader may have written goes back. Host scratch data came here too and was "written back"
    // after the submit to the stack address it had been built at (the support buffer and c[0] of every stage of every
    // draw, into a dead frame; with NX_ASYNC_GPU from whichever thread retired the submit). NX_DRAW_SLOW keeps that.
    if (bytes && size && (write_back || draw_slow(kSlowUploads)))
        g_write_backs.push_back({u.ptr, const_cast<uint8_t*>(static_cast<const uint8_t*>(bytes)), size});
    return {u.buffer, u.offset, n};
}

// The sets being built: scratch kept from draw to draw (three vectors, one of them ~130 KB, were allocated per draw).
struct SetWrite {  // one write of a set, as the set cache compares it
    uint32_t binding = 0, type = 0;
    uint64_t a = 0, b = 0, c = 0;  // buffer, offset, range; or view, sampler, layout
};
struct Descriptors {
    VkDescriptorSet sets[4] = {};
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<uint8_t> set_index;  // the set of each write (its dstSet is filled when the set is known)
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<DescriptorValue> values;
    bool compact = false;
    std::vector<SetWrite> key;  // scratch for the set cache
    uint64_t dynamic = 0;              // set 0 bindings given a dynamic offset (ProgramLayout::dynamic)
    uint32_t dynamic_offset[64] = {};  // their ring offsets, by binding
};
Descriptors g_sets;

// The set cache (docs/nvn/android-perf/PLAN.md R3): a set with the same layout and the same writes as one made earlier
// in this submit is that set. Every draw allocated and wrote up to four sets (~1,500 allocations and ~5,000 writes per
// course frame), most of it driver time on Adreno; texture sets repeat across the draws of a material, and buffer sets
// whenever dedup gave the same ring offsets. Sets come from the submit's pool and are reset with it, so entries live
// one submit (sets_begin_submit). The writes are compared in full, not by hash only. NX_SET_CACHE=0: off.
struct CachedSet {
    uint64_t generation = 0, hash = 0;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    uint32_t at = 0, n = 0;  // its writes in g_set_arena
    VkDescriptorSet set = VK_NULL_HANDLE;
};
constexpr uint32_t kSetSlots = 8192;
CachedSet g_set_cache[kSetSlots];
std::vector<SetWrite> g_set_arena;
uint64_t g_set_generation = 1, g_set_hits = 0, g_set_misses = 0;
uint32_t g_set_used = 0;

bool set_cache_on() {
    static const bool on = [] {
        const char* e = std::getenv("NX_SET_CACHE");
        return !(e && e[0] == '0') && !draw_slow(kSlowEmptySets);
    }();
    return on;
}

template <typename H>
uint64_t handle_bits(H h) {
    uint64_t v = 0;
    std::memcpy(&v, &h, sizeof h < sizeof v ? sizeof h : sizeof v);
    return v;
}

// d.sets from the cache or newly made, the writes of the cached ones dropped and the rest pointed at their new sets.
void resolve_sets(const ProgramLayout& layout) {
    Descriptors& d = g_sets;
    for (uint32_t k = 0; k < 4; k++) {
        d.key.clear();
        uint64_t h = handle_bits(layout.sets[k]) * 0x9E3779B97F4A7C15ull;
        for (size_t i = 0; i < d.writes.size(); i++) {
            if (d.set_index[i] != k) continue;
            const VkWriteDescriptorSet& w = d.writes[i];
            SetWrite e;
            e.binding = w.dstBinding, e.type = uint32_t(w.descriptorType);
            if (w.pBufferInfo)
                e.a = handle_bits(w.pBufferInfo->buffer), e.b = w.pBufferInfo->offset, e.c = w.pBufferInfo->range;
            else if (w.pImageInfo)
                e.a = handle_bits(w.pImageInfo->imageView), e.b = handle_bits(w.pImageInfo->sampler),
                e.c = uint64_t(w.pImageInfo->imageLayout);
            for (uint64_t v : {uint64_t(e.binding) << 32 | e.type, e.a, e.b, e.c})
                h = (h ^ v) * 0x100000001B3ull, h ^= h >> 31;
            d.key.push_back(e);
        }
        d.sets[k] = VK_NULL_HANDLE;
        if (d.key.empty()) continue;
        CachedSet* slot = nullptr;
        for (uint32_t probe = uint32_t(h >> 32);; probe++) {
            CachedSet& c = g_set_cache[probe & (kSetSlots - 1)];
            if (c.generation != g_set_generation) {
                slot = &c;
                break;
            }
            if (c.hash == h && c.layout == layout.sets[k] && c.n == d.key.size() &&
                std::memcmp(&g_set_arena[c.at], d.key.data(), d.key.size() * sizeof(SetWrite)) == 0) {
                d.sets[k] = c.set, g_set_hits++;
                break;
            }
        }
        if (d.sets[k]) continue;
        d.sets[k] = descriptor_set(layout.sets[k]), g_set_misses++;
        for (size_t i = 0; i < d.writes.size(); i++)
            if (d.set_index[i] == k) d.writes[i].dstSet = d.sets[k];
        if (!d.sets[k] || g_set_used >= kSetSlots / 2) continue;
        *slot = {g_set_generation, h, layout.sets[k], uint32_t(g_set_arena.size()), uint32_t(d.key.size()), d.sets[k]};
        g_set_arena.insert(g_set_arena.end(), d.key.begin(), d.key.end());
        g_set_used++;
    }
    size_t kept = 0;  // the writes of new sets only
    for (size_t i = 0; i < d.writes.size(); i++)
        if (d.writes[i].dstSet) d.writes[kept++] = d.writes[i];
    d.writes.resize(kept);
}

#include "nvn_uniform_extents.inc"

}  // namespace

std::vector<WriteBack>& separate_write_backs() { return g_write_backs; }

void texture_for(uint64_t handle, uint64_t& texture, SamplerInfo& sampler, ViewInfo* view) {
    HandleSlot* s = handle_slot(handle);
    if (!s) return lookup_handle(handle, texture, sampler, view);
    texture = s->texture;
    if (s->has_sampler) sampler = s->sampler;
    if (view) *view = s->view;
}

// Descriptor sets for a program's stages from the bound state: buffers (sets 0/1), textures (set 2, g_textures as
// gather_textures left them), storage images (set 3). Records uploads and layout changes, so it runs outside render
// passes when textures or images need them. A set is allocated when its first binding is written: the sets no stage
// declares anything in (usually two of the four) are neither allocated nor bound.
// sets89: the uniform-block case of build_sets, verbatim (only `info = ...; break;` became a return).
// cmd98: model_geometry_identity memoized per vertex pointer (equal bytes and stride -> the same hash).
inline uint64_t trace_geometry98(uint64_t geometry,const uint8_t* vertices,size_t available,uint32_t stride){
    static const bool dump=std::getenv("CATHERINE_DUMP_CHAIN_GEOMETRY")!=nullptr;
    if(dump&&(geometry==0x5d85a105c9d1820full||geometry==0x74b7033ab5e79527ull||geometry==0xb423bd2d0d6de2e9ull||geometry==0xdde09f70b0bd2816ull)){
        static std::unordered_map<uint64_t,bool> seen;
        if(!seen[geometry]){seen[geometry]=true;char path[256];std::snprintf(path,sizeof path,"logs/chain-geometry-%llx.bin",(unsigned long long)geometry);
            if(FILE* file=std::fopen(path,"wb")){std::fwrite(vertices,1,available,file);std::fclose(file);}
            log("CHAIN_GEOMETRY geometry=%llx bytes=%llu stride=%u",(unsigned long long)geometry,(unsigned long long)available,stride);
            const auto& uv=g_state.vertex_buffers[1];if(const uint8_t* source=guest_bytes(uv.address)){
                std::snprintf(path,sizeof path,"logs/chain-uv-%llx.bin",(unsigned long long)geometry);
                if(FILE* file=std::fopen(path,"wb")){std::fwrite(source,1,uv.size,file);std::fclose(file);}
                log("CHAIN_UV geometry=%llx bytes=%llu stride=%u",(unsigned long long)geometry,(unsigned long long)uv.size,g_state.streams[1].stride);
            }
            for(unsigned i=0;i<g_state.attribs.size();++i){const auto& a=g_state.attribs[i];log("CHAIN_ATTRIBUTE location=%u stream=%u offset=%u format=%x",i,a.stream,a.offset,a.format);}
        }
    }
    return geometry;
}
inline uint64_t geometry_identity98(const uint8_t* vertices, size_t available, uint32_t stride) {
    static const bool on = [] { const char* e = std::getenv("CATHERINE_PARALLEL98"); return !(e && e[0] == '0'); }();
    if (!on) return trace_geometry98(catherine::visual::model_geometry_identity(vertices, available, stride),vertices,available,stride);
    struct Entry { const uint8_t* p = nullptr; uint32_t n = 0, stride = 0; uint8_t bytes[64]; uint64_t hash = 0; };
    static Entry table[256];
    const uint32_t bytes = uint32_t(std::min<size_t>(available, 64));
    Entry& e = table[(reinterpret_cast<uintptr_t>(vertices) >> 4 ^ reinterpret_cast<uintptr_t>(vertices) >> 12) & 255];
    if (e.p == vertices && e.n == bytes && e.stride == stride && std::memcmp(e.bytes, vertices, bytes) == 0) return trace_geometry98(e.hash,vertices,available,stride);
    e.p = vertices, e.n = bytes, e.stride = stride;
    std::memcpy(e.bytes, vertices, bytes);
    e.hash = catherine::visual::model_geometry_identity(e.bytes, bytes, stride);
    return trace_geometry98(e.hash,vertices,available,stride);
}

namespace {
#include "inactive_c3.inc"
}

static const uint8_t* root9_rigid_c3(const uint8_t* bytes,uint64_t size,
    const uint8_t* vertices,uint64_t available,uint32_t stride,const ShaderInfo& shader,
    int stage,int slot,uint64_t address,bool& handled){
    handled=false;const uint64_t clock=puzzle_render_clock();auto& history=catherine::visual::root9_presentation();
    if(!(clock&0x80000000u)){history.clear();catherine::visual::root12_presentation().clear();return bytes;}
    if(!bytes||size!=64||stage!=0||slot!=0||shader.stage!=ShaderStage::Vertex||
       !vertices)return bytes;
    const auto* packet=nx::nvn::root9_uniform_owner(address,bytes,size);if(!packet)return bytes;
    if(written_overlaps(bytes,size)||!std::binary_search(catherine::visual::world_model_shaders.begin(),catherine::visual::world_model_shaders.end(),std::string_view(shader.hash)))return bytes;
    if(packet->background.identity){
        const auto& background=packet->background;
        const uint32_t produced=uint32_t(background.clock>>32),now=uint32_t(clock>>32);
        if(!(background.clock&0x80000000u)||now<produced||now-produced>2)return bytes;
        float raw[16];std::memcpy(raw,bytes,64);
        for(float value:raw)if(!std::isfinite(value))return bytes;
        if(std::fabs(raw[12])>1e-5f||std::fabs(raw[13])>1e-5f||std::fabs(raw[14])>1e-5f||std::fabs(raw[15]-1)>1e-5f)return bytes;
        static catherine::visual::RigidLateSampleInterpolator backgrounds;
        handled=true;
        const auto* result=backgrounds.render(background.identity,bytes,64,puzzle_render_frame(),clock,true);
        if(shader.hash=="e16573779a2ed545"||shader.hash=="5abe931033fb4344")catherine::visual::trace_rigid_motion(1,puzzle_render_frame(),clock,geometry_identity98(vertices,available,stride),background.identity,background.mesh,bytes,result);
        static const bool trace_background=std::getenv("CATHERINE_TRACE_BACKGROUND_INSTANCE")!=nullptr;
        if(trace_background&&geometry_identity98(vertices,available,stride)==0xbbd44ba55009c6cbull){
            static unsigned remaining=12000;
            if(remaining){--remaining;float rendered[16];std::memcpy(rendered,result,64);
                float error=0;for(unsigned k=0;k<16;++k)error=std::max(error,std::fabs(raw[k]-rendered[k]));
                log("BACKGROUND_INSTANCE frame=%llu identity=%llu mesh=%llx error=%.9g basis=%.9g,%.9g,%.9g",
                    (unsigned long long)puzzle_render_frame(),(unsigned long long)background.identity,
                    (unsigned long long)background.mesh,error,raw[0],raw[1],raw[2]);
            }
        }
        // Exact stationary results can retain the native uniform cache path.
        return std::memcmp(result,bytes,64)==0?bytes:result;
    }
    const auto& owner=packet->owner;uint32_t produced=uint32_t(owner.clock>>32),now=uint32_t(clock>>32);
    if(!(owner.clock&0x80000000u)||now<produced||now-produced>2)return bytes;
    catherine::visual::Block20Owner identity{owner.block,owner.generation,owner.model,owner.parent,owner.lifetime};
    const std::array<float,3> root{owner.world[12],owner.world[13],owner.world[14]};
    const uint64_t geometry=geometry_identity98(vertices,available,stride);
    if(catherine::root15::family(owner)==12)
        return catherine::visual::root12_presentation().rigid(true,identity,root,bytes,size,geometry,vertices,available,stride,puzzle_render_frame(),clock,&handled);
    return history.rigid(true,identity,root,bytes,size,geometry,vertices,available,stride,puzzle_render_frame(),clock,&handled);
}

VkDescriptorBufferInfo uniform_info89(VkCommandBuffer cb, const ShaderInfo* s, const DescriptorBinding& b, int st,
                                      const UniformExtents* static_sizes) {
                    const Range& r = b.nvn.index >= 0 && b.nvn.index < 18 ? g_state.uniforms[st][b.nvn.index] : Range{};
                    const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
                    const uint8_t* native_bytes=p;
                    // Reads past the bound size return 0 on Maxwell and garbage on Metal (a UI shader read c3[10]
                    // of a 0x90-byte block and wrote Inf): the bound data goes in a zero-padded 4 KB at least.
                    // Banks the shader indexes with a computed value (clamped to 64 KB, nvn_spirv_patch.cpp): all
                    // 64 KB, so every read past the data returns 0 as on Maxwell.
                    bool indexed = b.set == 0 && b.binding < 64 && (dynamic_uniform_bindings(s) >> b.binding & 1);
                    const bool uniform = b.set == 0 && VkDescriptorType(b.type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    const uint32_t exact = static_sizes && uniform && b.binding < static_sizes->size()
                                               ? (*static_sizes)[b.binding] : 0;
                    const uint64_t size = p ? std::min<uint64_t>(r.size, 0x10000) : 0;
                    const uint64_t legacy_padding = indexed ? 0x10000 : 0x1000;
                    uint64_t padding = !indexed && exact ? std::min(0x1000u, std::bit_ceil(exact)) : legacy_padding;
                    // A tighter padded range might bind directly into a larger written shadow. Preserve the
                    // previous shadow/composition decision if the shader can read past this bound range.
                    if (size && exact > size && padding < legacy_padding && written_overlaps(p, size))
                        padding = legacy_padding;
                    if(b.nvn.bank==3 && s->stage==ShaderStage::Vertex){
                        const auto stream=g_state.attribs[0].stream;
                        if(stream<16){
                            const auto& mesh=g_state.vertex_buffers[stream];
                            const auto* vertices=mesh.address?guest_bytes(mesh.address):nullptr;
                            const uint64_t offset=g_state.attribs[0].offset;
                            if(vertices){
                                // World-space skin output already contains the interpolated
                                // pose. Local morph output still needs its head-parent delta.
                                const bool skinned=offset<=mesh.size && mesh.size-offset>=12 &&
                                    written_skin_output(vertices+offset,12);
                                if(!skinned){bool handled9=false;if(offset==0)p=root9_rigid_c3(p,size,vertices,mesh.size,g_state.streams[stream].stride,*s,st,b.nvn.index,r.address,handled9);if(!handled9){if(c02_uniform_provenance::matches(st,b.nvn.index,r.address,size)&&!written_overlaps(p,size))c02_uniform_provenance::bypass();else if(c03_uniform_matches(st,b.nvn.index,r.address,p,size)&&!written_overlaps(p,size)){}else p=inactive_c3::transform(p,size,vertices,mesh.size,g_state.streams[stream].stride,*s);}}
#ifdef CATHERINE_POSE_DIAGNOSTICS
                                else if(size==64 && (puzzle_render_clock()&0x80000000u)){
                                    static uint64_t last=0, total=0;
                                    ++total;
                                    const auto frame=puzzle_render_frame();
                                    if(frame>last+120){
                                        last=frame;
                                        const auto geometry=geometry_identity98(vertices,mesh.size,g_state.streams[stream].stride);
                                        log("HeadSkin frame=%llu geometry=%llx c3_unchanged=1 total=%llu",(unsigned long long)frame,(unsigned long long)geometry,(unsigned long long)total);
                                    }
                                }
#endif
                            }
                        }
                    }
                    static const bool dedup = std::getenv("NX_NO_DEDUP") == nullptr;
                    return static_sizes && uniform && !size && dedup ? zero_uniform(padding)
                                                                     : buffer_of(cb, p, size, padding, false, p==native_bytes);
}

// sets91: uniform_info89 up to its upload (verbatim), returning the 64 bytes it would upload.
bool fast91_matrix_bytes(const ShaderInfo* s, const DescriptorBinding& b, int st, uint8_t* out) {
    const UniformExtents* static_sizes = shader_uniform_extents(s);
                    const Range& r = b.nvn.index >= 0 && b.nvn.index < 18 ? g_state.uniforms[st][b.nvn.index] : Range{};
                    const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
                    const uint8_t* native_bytes=p;
                    // Reads past the bound size return 0 on Maxwell and garbage on Metal (a UI shader read c3[10]
                    // of a 0x90-byte block and wrote Inf): the bound data goes in a zero-padded 4 KB at least.
                    // Banks the shader indexes with a computed value (clamped to 64 KB, nvn_spirv_patch.cpp): all
                    // 64 KB, so every read past the data returns 0 as on Maxwell.
                    bool indexed = b.set == 0 && b.binding < 64 && (dynamic_uniform_bindings(s) >> b.binding & 1);
                    const bool uniform = b.set == 0 && VkDescriptorType(b.type) == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    const uint32_t exact = static_sizes && uniform && b.binding < static_sizes->size()
                                               ? (*static_sizes)[b.binding] : 0;
                    const uint64_t size = p ? std::min<uint64_t>(r.size, 0x10000) : 0;
                    const uint64_t legacy_padding = indexed ? 0x10000 : 0x1000;
                    uint64_t padding = !indexed && exact ? std::min(0x1000u, std::bit_ceil(exact)) : legacy_padding;
                    // A tighter padded range might bind directly into a larger written shadow. Preserve the
                    // previous shadow/composition decision if the shader can read past this bound range.
                    if (size && exact > size && padding < legacy_padding && written_overlaps(p, size))
                        padding = legacy_padding;
                    if(b.nvn.bank==3 && s->stage==ShaderStage::Vertex){
                        const auto stream=g_state.attribs[0].stream;
                        if(stream<16){
                            const auto& mesh=g_state.vertex_buffers[stream];
                            const auto* vertices=mesh.address?guest_bytes(mesh.address):nullptr;
                            const uint64_t offset=g_state.attribs[0].offset;
                            if(vertices){
                                // World-space skin output already contains the interpolated
                                // pose. Local morph output still needs its head-parent delta.
                                const bool skinned=offset<=mesh.size && mesh.size-offset>=12 &&
                                    written_skin_output(vertices+offset,12);
                                if(!skinned){bool handled9=false;if(offset==0)p=root9_rigid_c3(p,size,vertices,mesh.size,g_state.streams[stream].stride,*s,st,b.nvn.index,r.address,handled9);if(!handled9){if(c02_uniform_provenance::matches(st,b.nvn.index,r.address,size)&&!written_overlaps(p,size))c02_uniform_provenance::bypass();else if(c03_uniform_matches(st,b.nvn.index,r.address,p,size)&&!written_overlaps(p,size)){}else p=inactive_c3::transform(p,size,vertices,mesh.size,g_state.streams[stream].stride,*s);}}
#ifdef CATHERINE_POSE_DIAGNOSTICS
                                else if(size==64 && (puzzle_render_clock()&0x80000000u)){
                                    static uint64_t last=0, total=0;
                                    ++total;
                                    const auto frame=puzzle_render_frame();
                                    if(frame>last+120){
                                        last=frame;
                                        const auto geometry=geometry_identity98(vertices,mesh.size,g_state.streams[stream].stride);
                                        log("HeadSkin frame=%llu geometry=%llx c3_unchanged=1 total=%llu",(unsigned long long)frame,(unsigned long long)geometry,(unsigned long long)total);
                                    }
                                }
#endif
                            }
                        }
                    }
    (void)padding;
    if (!p || size != 64) return false;
    std::memcpy(out, p, 64);
    return true;
}

// batch91 (legacy layouts): the descriptor sets the last bind_sets bound.
void fast92_sets(VkDescriptorSet* out) { for (int k = 0; k < 4; k++) out[k] = g_sets.sets[k]; }

// draw89's entry points.
VkDescriptorBufferInfo fast89_uniform_info(VkCommandBuffer cb, const ShaderInfo* s, const DescriptorBinding& b, int st) {
    return uniform_info89(cb, s, b, st, shader_uniform_extents(s));
}
bool fast89_values(std::vector<DescriptorValue>& out) {
    if (!g_sets.compact) return false;
    out = g_sets.values;
    return true;
}

#include "native_material_core.inc"

namespace materials {
struct UniformData {const uint8_t* bytes=nullptr;const uint8_t* native=nullptr;uint64_t size=0,padding=0;bool zero=false;};
UniformData uniform_data(const Recipe& recipe) {
    const ShaderInfo* s=recipe.shader;
    const bool static_sizes=static_extent_enabled();
                    const Range r = uniform_range(recipe);
                    const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
                    const uint8_t* native_bytes=p;
                    // Reads past the bound size return 0 on Maxwell and garbage on Metal (a UI shader read c3[10]
                    // of a 0x90-byte block and wrote Inf): the bound data goes in a zero-padded 4 KB at least.
                    // Banks the shader indexes with a computed value (clamped to 64 KB, nvn_spirv_patch.cpp): all
                    // 64 KB, so every read past the data returns 0 as on Maxwell.
                    bool indexed = recipe.computed_index;
                    const bool uniform = recipe.uniform_descriptor;
                    const uint32_t exact = static_sizes && uniform ? recipe.exact_extent : 0;
                    const uint64_t size = p ? std::min<uint64_t>(r.size, 0x10000) : 0;
                    const uint64_t legacy_padding = indexed ? 0x10000 : 0x1000;
                    uint64_t padding = !indexed && exact ? std::min(0x1000u, std::bit_ceil(exact)) : legacy_padding;
                    // A tighter padded range might bind directly into a larger written shadow. Preserve the
                    // previous shadow/composition decision if the shader can read past this bound range.
                    if (size && exact > size && padding < legacy_padding && written_overlaps(p, size))
                        padding = legacy_padding;
                    if(mode()==2) {
                        const auto& b=*recipe.legacy;
                        const Range old=b.nvn.index>=0&&b.nvn.index<18?g_state.uniforms[api_stage(s->stage)][b.nvn.index]:Range{};
                        const auto* old_p=old.address?guest_bytes(old.address):nullptr;
                        const uint64_t old_size=old_p?std::min<uint64_t>(old.size,0x10000):0;
                        const bool old_index=b.set==0&&b.binding<64&&(dynamic_uniform_bindings(s)>>b.binding&1);
                        const bool old_uniform=b.set==0&&VkDescriptorType(b.type)==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        const auto* old_sizes=shader_uniform_extents(s);
                        const uint32_t old_exact=old_sizes&&old_uniform&&b.binding<old_sizes->size()?(*old_sizes)[b.binding]:0;
                        const uint64_t old_legacy_padding=old_index?0x10000:0x1000;
                        uint64_t old_padding=!old_index&&old_exact?std::min(0x1000u,std::bit_ceil(old_exact)):old_legacy_padding;
                        if(old_size&&old_exact>old_size&&old_padding<old_legacy_padding&&written_overlaps(old_p,old_size))old_padding=old_legacy_padding;
                        if(p!=old_p||size!=old_size||padding!=old_padding||uniform!=old_uniform||
                           recipe.model_transform!=(b.nvn.bank==3&&s->stage==ShaderStage::Vertex)||
                           (size&&std::memcmp(p,old_p,size)))fatal("Native uniform source/bytes/padding differ from legacy");
                        ++census.uniform_compared;
                    }
                    if(recipe.model_transform){
                        const auto stream=g_state.attribs[0].stream;
                        if(stream<16){
                            const auto& mesh=g_state.vertex_buffers[stream];
                            const auto* vertices=mesh.address?guest_bytes(mesh.address):nullptr;
                            const uint64_t offset=g_state.attribs[0].offset;
                            if(vertices){
                                // World-space skin output already contains the interpolated
                                // pose. Local morph output still needs its head-parent delta.
                                const bool skinned=offset<=mesh.size && mesh.size-offset>=12 &&
                                    written_skin_output(vertices+offset,12);
                                if(!skinned){bool handled9=false;if(offset==0)p=root9_rigid_c3(p,size,vertices,mesh.size,g_state.streams[stream].stride,*s,recipe.stage,recipe.slot,r.address,handled9);if(!handled9){if(c02_uniform_provenance::matches(recipe.stage,recipe.slot,r.address,size)&&!written_overlaps(p,size))c02_uniform_provenance::bypass();else if(c03_uniform_matches(recipe.stage,recipe.slot,r.address,p,size)&&!written_overlaps(p,size)){}else p=inactive_c3::transform(p,size,vertices,mesh.size,g_state.streams[stream].stride,*s);}}
#ifdef CATHERINE_POSE_DIAGNOSTICS
                                else if(size==64 && (puzzle_render_clock()&0x80000000u)){
                                    static uint64_t last=0, total=0;
                                    ++total;
                                    const auto frame=puzzle_render_frame();
                                    if(frame>last+120){
                                        last=frame;
                                        const auto geometry=geometry_identity98(vertices,mesh.size,g_state.streams[stream].stride);
                                        log("HeadSkin frame=%llu geometry=%llx c3_unchanged=1 total=%llu",(unsigned long long)frame,(unsigned long long)geometry,(unsigned long long)total);
                                    }
                                }
#endif
                            }
                        }
                    }

    static const bool dedup=std::getenv("NX_NO_DEDUP")==nullptr;
    return {p,native_bytes,size,padding,static_sizes&&uniform&&!size&&dedup};
}
VkDescriptorBufferInfo uniform_info(VkCommandBuffer cb,const Recipe& r) {
    if(!mode())return uniform_info89(cb,r.shader,*r.legacy,r.stage,shader_uniform_extents(r.shader));
    const auto data=uniform_data(r);
    return data.zero?zero_uniform(data.padding):buffer_of(cb,data.bytes,data.size,data.padding,false,data.bytes==data.native);
}
bool matrix_bytes(const Recipe& r,uint8_t* out) {
    if(!mode())return fast91_matrix_bytes(r.shader,*r.legacy,r.stage,out);
    const auto data=uniform_data(r);if(!data.bytes||data.size!=64)return false;
    std::memcpy(out,data.bytes,64);return true;
}
} // namespace materials

#include "native_image_material_core.inc"

void build_sets(VkCommandBuffer cb, const std::vector<const ShaderInfo*>& shaders, const ProgramLayout& layout) {
    Descriptors& d = g_sets;
    if (draw_slow(kSlowScratch))
        d = Descriptors{};
    else
        d.writes.clear(), d.set_index.clear(), d.buffers.clear(), d.images.clear();
    d.dynamic = 0;
    static const bool compact_values = [] {
        const char* setting = std::getenv("NX_COMPACT_DESCRIPTOR_VALUES");
        return setting && setting[0] == '1';
    }();
    d.compact = compact_values && layout.bytes[0] && !layout.dynamic;
    if (d.compact) d.values.clear();
    const bool every_set = draw_slow(kSlowEmptySets), shared = !draw_slow(kSlowUploads);
    const bool cached = layout.bytes[0] || set_cache_on();
    for (int k = 0; k < 4; k++) d.sets[k] = every_set ? descriptor_set(layout.sets[k]) : VK_NULL_HANDLE;
    auto set_of = [&](uint32_t k) {  // with the set cache, made (or found) in bind_sets
        d.set_index.push_back(uint8_t(k));
        if (cached) return VkDescriptorSet(VK_NULL_HANDLE);
        if (!d.sets[k]) d.sets[k] = descriptor_set(layout.sets[k]);
        return d.sets[k];
    };
    auto& writes = d.writes;
    auto& buffers = d.buffers;
    auto& images = d.images;
    if (!d.compact) {
        buffers.reserve(1024), images.reserve(1024), writes.reserve(2048);  // pointers into them are kept
    }
    float vw = g_state.has_viewport ? float(g_state.viewport[2]) : 1280.0f;
    float vh = g_state.has_viewport ? float(g_state.viewport[3]) : 720.0f;
    SupportBuffer sb;  // built when a binding gets a copy of its own
    bool sb_built = false;
    auto support = [&]() -> VkDescriptorBufferInfo {
        const SupportBuffer* block = shared ? support_block(vw, vh) : nullptr;
        if (block) {
            Upload u = dedup_upload(reinterpret_cast<const uint8_t*>(block), sizeof sb, sizeof sb, 256);
            return {u.buffer, u.offset, sizeof sb};
        }
        if (!sb_built) {
            sb = support_buffer_defaults();
            support_buffer_set_viewport(sb, vw, vh);
            sb_built = true;
        }
        return buffer_of(cb, &sb, sizeof sb, 16, false, false);
    };
    // A storage binding's bytes, and whether this compute stage writes them.
    auto storage = [](const ShaderInfo* s, const DescriptorBinding& b, int st, const uint8_t*& p, uint64_t& size) {
        const Range& r = b.nvn.index >= 0 && b.nvn.index < 16 ? g_state.storage[st][b.nvn.index] : Range{};
        p = r.address ? guest_bytes(r.address) : nullptr;
        size = p ? std::min<uint64_t>(r.size, 16ull << 20) : 0;
        return s->stage == ShaderStage::Compute &&
               (all_write_backs() || (b.binding < 64 && written_storage_bindings(s) >> b.binding & 1));
    };

    const materials::LinkedPlan* selected_material=materials::mode()?&materials::plan(shaders,layout):nullptr;
    if(!selected_material||!selected_material->supported) {
        if(selected_material&&++materials::census.fallback_arrays==1)
            log("Native material compatibility fallback: buffer descriptor array/runtime count (legacy set 0/1 execution)");
    // The written ranges first (nvn_written.cpp): two bindings of one dispatch that meet get one copy, and bindings
    // that read what the dispatch writes see it, as on the GPU.
    for (const ShaderInfo* s : shaders)
        for (const DescriptorBinding& b : s->refl.bindings) {
            const uint8_t* p;
            uint64_t size;
            if (b.set < 2 && b.nvn.source == NvnSource::StorageBuffer && storage(s, b, api_stage(s->stage), p, size) &&
                p && size && !written_separate())
                written_bind(cb, p, size, std::max<uint64_t>((size + 15) & ~15ull, 0x10000));
        }
    for (const ShaderInfo* s : shaders) {
        const UniformExtents* static_sizes = shader_uniform_extents(s);
        int st = api_stage(s->stage);
        for (const DescriptorBinding& b : s->refl.bindings) {
            if (b.set >= 2) continue;
            VkDescriptorBufferInfo info{};
            if (memo87::lookup(s, b, st, vw, vh, info)) goto memo87_have;
            switch (b.nvn.source) {
                case NvnSource::SupportBuffer: info = support(); break;
                case NvnSource::ConstantData: info = buffer_of(cb, s->constants.data(), s->constants.size()); break;
                case NvnSource::UniformBlock:
                case NvnSource::UniformAsStorage: {
                    info = uniform_info89(cb, s, b, st, static_sizes);
                    break;
                }
                case NvnSource::StorageBuffer: {
                    const uint8_t* p;
                    uint64_t size;
                    bool written = storage(s, b, st, p, size);
                    const uint8_t* native_bytes=p;
                    if (!written && s->stage != ShaderStage::Compute) written_graphics_storage(p, size);
                    if(!written && s->stage==ShaderStage::Compute && b.nvn.index==0 && vincent_skin_shader(*s)) {
                        p=vincent_pose_palette(p,size,g_state.storage[5][1].address,g_state.storage[5][0].address);
                        p=spike_gpu_palette(p,size,*s);
                        p=campaign_block_gpu_palette(p,size,*s);
                        p=block20_gpu_palette(p,size,*s);
                    }
                    // At least 64 KB: Metal does not bound-check, and a shader writing past a small or missing
                    // buffer overwrote the next data in the upload buffer (garbage everywhere after a compute).
                    info = buffer_of(cb, p, size, 0x10000, written, p==native_bytes);
                    break;
                }
                case NvnSource::DriverConstants: {
                    if (const uint8_t* block = shared ? constants_block(s->stage, st) : nullptr) {
                        Upload u = dedup_upload(block, 0x1000, 0x1000, 256);
                        info = {u.buffer, u.offset, 0x1000};
                        break;
                    }
                    auto c0 = driver_constants(s->stage, st);
                    info = buffer_of(cb, c0.data(), c0.size(), 16, false, false);
                    break;
                }
                default: info = buffer_of(cb, nullptr, 0x1000, 16, false, false); break;
            }
            memo87::store(s, b, st, vw, vh, info);
        memo87_have:
            VkDescriptorType type = VkDescriptorType(b.type);
            if (b.set == 0 && b.binding < 64 && layout.dynamic >> b.binding & 1) {  // the offset goes with the bind
                d.dynamic |= 1ull << b.binding, d.dynamic_offset[b.binding] = uint32_t(info.offset);
                info.offset = 0, type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            }
            if (d.compact) {
                d.values.emplace_back(type, b.binding, uint8_t(b.set), info);
                continue;
            }
            buffers.push_back(info);
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set_of(b.set);
            w.dstBinding = b.binding;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pBufferInfo = &buffers.back();
            writes.push_back(w);
        }
    }

    } else {
        const auto& material=*selected_material;
        materials::Execution execution;
        execution.vw=vw;execution.vh=vh;execution.shared=shared;
        for(const auto* recipe:material.storage) {
            if(materials::mode()==2)materials::verify_recipe(*recipe);
            const uint8_t* p;uint64_t size;
            if(materials::storage_source(*recipe,p,size)&&p&&size&&!written_separate())
                written_bind(cb,p,size,std::max<uint64_t>((size+15)&~15ull,0x10000));
        }
        for(const auto& op:material.buffers) {
            const auto& recipe=*op.recipe;const auto& dst=op.destination;
            if(materials::mode()==2) {
                materials::verify_recipe(recipe);
                const auto& b=*recipe.legacy;
                const bool dynamic=b.set==0&&b.binding<64&&(layout.dynamic>>b.binding&1);
                if(dst.set!=b.set||dst.binding!=b.binding||dst.dynamic!=dynamic||
                   dst.type!=(dynamic?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VkDescriptorType(b.type)))
                    fatal("Native material linked Vulkan destination differs from legacy");
            }
            VkDescriptorBufferInfo info{};
            if(!materials::memo_lookup(recipe,vw,vh,info)) {
                info=recipe.resolve(cb,recipe,execution);
                materials::memo_store(recipe,vw,vh,info);
            }
            ++materials::census.writes;
            if(dst.dynamic) {d.dynamic|=1ull<<dst.binding;d.dynamic_offset[dst.binding]=uint32_t(info.offset);info.offset=0;}
            if(d.compact) {d.values.emplace_back(dst.type,dst.binding,uint8_t(dst.set),info);continue;}
            buffers.push_back(info);
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet=set_of(dst.set);w.dstBinding=dst.binding;w.descriptorCount=1;w.descriptorType=dst.type;
            w.pBufferInfo=&buffers.back();writes.push_back(w);
        }
        if(materials::census.writes==material.buffers.size()||materials::census.writes/1000000!=(materials::census.writes-material.buffers.size())/1000000)
            log("Native materials: %llu shader plans, %llu linked plans, %llu writes, %llu memo hits, %llu recipe comparisons, %llu memo comparisons, %llu uniform comparisons, %llu tower comparisons",
                (unsigned long long)materials::census.compiled,(unsigned long long)materials::census.linked,
                (unsigned long long)materials::census.writes,(unsigned long long)materials::census.memo_hits,
                (unsigned long long)materials::census.compared,(unsigned long long)materials::census.memo_compared,
                (unsigned long long)materials::census.uniform_compared,(unsigned long long)materials::census.template_compared);
    }

    const auto* native_images_plan=native_images::mode()?&image_materials::plan(shaders):nullptr;
    if(!native_images_plan||!image_materials::supported(*native_images_plan)) {
    const DrawTextures& t = g_textures;
    for (size_t k = 0; k < t.bindings.size(); k++) {
        auto [s, b] = t.bindings[k];
        const ResolvedTexture r = k < t.resolved.size() ? t.resolved[k] : ResolvedTexture{};
        VkDescriptorImageInfo ii{};
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ii.imageView = r.texture ? sampled_view(cb, r.texture, b->image, &r.view, &ii.imageLayout)
                                 : dummy_view(cb, b->image);
        ii.sampler = r.vk_sampler ? r.vk_sampler : sampler_for(r.sampler, b->image.depth);
        if (d.compact) {
            d.values.emplace_back(VkDescriptorType(b->type), b->binding, 2, ii);
            continue;
        }
        images.push_back(ii);
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set_of(2);
        w.dstBinding = b->binding;
        w.descriptorCount = 1;
        w.descriptorType = VkDescriptorType(b->type);
        w.pImageInfo = &images.back();
        writes.push_back(w);
    }
    for (const ShaderInfo* s : shaders) {
        int st = api_stage(s->stage);
        for (const DescriptorBinding& b : s->refl.bindings) {
            if (b.set != 3) continue;
            uint64_t handle = b.nvn.source == NvnSource::ImageBinding && b.nvn.index >= 0 && b.nvn.index < 16
                                  ? g_state.images[st][b.nvn.index]
                                  : 0;
            uint64_t texture = 0;
            SamplerInfo si;
            ViewInfo view;
            if (handle) texture_for(handle & 0xFFFFF, texture, si, &view);
            VkDescriptorImageInfo ii{};
            ii.imageView = storage_view(cb, texture, b.image, texture ? &view : nullptr);
            ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            if (d.compact) {
                d.values.emplace_back(VkDescriptorType(b.type), b.binding, 3, ii);
                continue;
            }
            images.push_back(ii);
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set_of(3);
            w.dstBinding = b.binding;
            w.descriptorCount = 1;
            w.descriptorType = VkDescriptorType(b.type);
            w.pImageInfo = &images.back();
            writes.push_back(w);
        }
    }
    } else {
        const auto& material=*native_images_plan;const auto& textures=g_textures;
        if(textures.resolved.size()!=material.sampled.size())fatal("Native image material gather/build composition differs");
        auto emit_image=[&](const image_materials::Recipe& recipe,VkDescriptorImageInfo ii) {
            const auto& dst=recipe.destination;
            if(d.compact) {d.values.emplace_back(dst.type,dst.binding,uint8_t(dst.set),ii);return;}
            images.push_back(ii);VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet=set_of(dst.set);w.dstBinding=dst.binding;w.descriptorCount=1;w.descriptorType=dst.type;
            w.pImageInfo=&images.back();writes.push_back(w);
        };
        for(size_t k=0;k<material.sampled.size();++k) {
            const auto& recipe=material.sampled[k];const auto& resolved=textures.resolved[k];
            if(native_images::mode()==2) {
                native_images::verify(recipe.contract,recipe.legacy->image);
                if(textures.bindings[k].first!=recipe.shader||textures.bindings[k].second!=recipe.legacy)
                    fatal("Native sampled material ordering differs from gather projection");
            }
            VkDescriptorImageInfo ii{};ii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            const auto binding=k<image_materials::sample_bindings.size()?image_materials::sample_bindings[k].get():nullptr;
            ii.imageView=binding?sampled_view_bound(cb,*binding,recipe.contract,&ii.imageLayout):
                resolved.texture?sampled_view_native(cb,resolved.texture,recipe.contract,&resolved.view,&ii.imageLayout):dummy_view_native(cb,recipe.contract);
            ii.sampler=binding?binding->sampler:resolved.vk_sampler?resolved.vk_sampler:sampler_for(resolved.sampler,recipe.contract.depth);
            emit_image(recipe,ii);++image_materials::census.sampled;
        }
        for(const auto& recipe:material.storage) {
            const auto handle=recipe.read(recipe);image_materials::verify(recipe,handle);
            uint64_t texture=0;SamplerInfo sampler;ViewInfo view;
            if(handle)texture_for(handle&0xFFFFF,texture,sampler,&view);
            const auto reference=native_bindings::capture(texture,texture?get<Texture>(texture):nullptr,VK_NULL_HANDLE,view);
            auto* binding=reference.get();
            if(binding)prepare_bound_view(*binding,recipe.contract,image_of_bound(*binding),true);
            VkDescriptorImageInfo ii{};ii.imageView=binding?storage_view_bound(cb,*binding,recipe.contract):storage_view_native(cb,texture,recipe.contract,texture?&view:nullptr);
            ii.imageLayout=VK_IMAGE_LAYOUT_GENERAL;emit_image(recipe,ii);++image_materials::census.storage;
        }
    }

}

// A set with no binding in the layout is statically used by no shader, so nothing need be bound there: the runs of
// sets build_sets allocated are bound, in ascending order (each bind leaves the lower sets, bound with this same
// layout, undisturbed).
void bind_sets(VkCommandBuffer cb, VkPipelineBindPoint point, const ProgramLayout& layout) {
    Descriptors& d = g_sets;
    if (layout.bytes[0]) {
        if (d.compact)
            bind_descriptor_values(cb, point, layout, d.values);
        else
            bind_descriptor_bytes(cb, point, layout, d.writes, d.set_index);
        return;
    }
    descriptor_bytes_legacy_bind(cb);
    if (set_cache_on()) resolve_sets(layout);
    if (!d.writes.empty()) p_vkUpdateDescriptorSets(vk_device(), uint32_t(d.writes.size()), d.writes.data(), 0, nullptr);
    // Set 0's dynamic offsets, in binding order (every dynamic binding of the layout; one no stage wrote: 0).
    uint32_t offsets[64];
    uint32_t noff = 0;
    for (uint64_t m = layout.dynamic; m; m &= m - 1) {
        uint32_t b = uint32_t(__builtin_ctzll(m));
        offsets[noff++] = d.dynamic >> b & 1 ? d.dynamic_offset[b] : 0;
    }
    if (draw_slow(kSlowEmptySets)) {
        p_vkCmdBindDescriptorSets(cb, point, layout.layout, 0, 4, d.sets, noff, offsets);
        return;
    }
    for (uint32_t k = 0; k < 4;) {
        if (!d.sets[k]) {
            k++, g_sets_skipped++;
            continue;
        }
        uint32_t n = 1;
        while (k + n < 4 && d.sets[k + n]) n++;
        p_vkCmdBindDescriptorSets(cb, point, layout.layout, k, n, d.sets + k, k == 0 ? noff : 0, offsets);
        k += n;
    }
}

// Texture bindings of a program's stages and their handles, into g_textures. kNeedBreak: one needs a transfer or a
// layout change (so no render pass may be open while the sets are built); kSampledPending: one has a pending clear.
uint32_t gather_textures_compat(const std::vector<const ShaderInfo*>& shaders) {
    DrawTextures& t = g_textures;
    if (draw_slow(kSlowScratch))
        t = DrawTextures{};
    else
        t.bindings.clear();
    uint32_t out = 0;
    for (const ShaderInfo* s : shaders)
        for (const DescriptorBinding& b : s->refl.bindings) {
            if (b.set == 2) t.bindings.push_back({s, &b});
            if (b.set == 3) out |= kNeedBreak;  // storage images change layout
        }
    t.handles.assign(t.bindings.size(), 0);
    t.resolved.assign(t.bindings.size(), ResolvedTexture{});
    const uint64_t submit_ns = submit_start_ns();
    for (size_t k = 0; k < t.bindings.size(); k++) {
        auto [s, b] = t.bindings[k];
        int st = api_stage(s->stage);
        uint64_t handle = 0;
        if (b->nvn.source == NvnSource::SamplerBinding && b->nvn.index >= 0 && b->nvn.index < 32)
            handle = g_state.textures[st][b->nvn.index];
        else if (b->nvn.source == NvnSource::HandleInUniform && b->nvn.bank >= 3) {
            const Range& r = g_state.uniforms[st][b->nvn.bank - 3];
            const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
            if (p && uint64_t(b->nvn.word) * 4 + 4 <= r.size) handle = *reinterpret_cast<const uint32_t*>(p + b->nvn.word * 4);
        }
        t.handles[k] = handle;
        ResolvedTexture& r = t.resolved[k];
        uint64_t texture = 0;
        Texture* sampled = nullptr;
        // A pool entry naming a texture that no longer exists samples the dummy, like no texture: it ended the
        // render pass at every draw (~1.4M times in a run, docs/nvn/DEBUG.md #34).
        if (HandleSlot* slot = handle_slot(handle)) {
            sampled = slot->object;
            if (sampled) texture = slot->texture;
            r.sampler = slot->sampler, r.view = slot->view;
            VkSampler& vk = slot->vk_sampler[b->image.depth ? 1 : 0];  // the same for every draw: a cache key built
            if (!vk) vk = slot->published_sampler?sampler_for_published(*slot->published_sampler,b->image.depth):sampler_for(slot->sampler,b->image.depth);  // per binding otherwise (nvn_sampler.cpp)
            r.vk_sampler = vk;
        } else {
            lookup_handle(handle, texture, r.sampler, &r.view);
            sampled = texture ? get<Texture>(texture) : nullptr;
        }
        // Vulkan depth comparisons require a depth format. Use the depth dummy for an unbound or color slot.
        if (!sampled || (b->image.depth && !(format_aspect(sampled->format) & VK_IMAGE_ASPECT_DEPTH_BIT))) texture = 0;
        if (texture) sampled->last_sampled = submit_ns;
        r.texture = texture;
        if (texture ? !sampled_ready(texture) : !dummy_ready(b->image)) out |= kNeedBreak;
        if (!texture) continue;
        GpuImage* img = image_of(texture);
        // Sampling one of the open pass's targets (a depth buffer read back by the character lighting) needs what
        // was rendered stored first: a fresh pass. SetRenderTargets used to end every pass, which did this by chance
        // (docs/nvn/DEBUG.md #35). Only when a draw of this pass wrote it (pass_wrote, nvn_draw.cpp).
        if (pass_wrote(img)) out |= kNeedBreak;
        if (clears_pending_image(img)) out |= kNeedBreak | kSampledPending;
    }
    return out;
}

uint32_t gather_textures(const std::vector<const ShaderInfo*>& shaders) {
    if(!native_images::mode())return gather_textures_compat(shaders);
    const auto& material=image_materials::plan(shaders);
    if(!image_materials::supported(material))return gather_textures_compat(shaders);
    image_materials::verify_plan(material,shaders);
    return image_materials::gather(material);
}


// The zero stream (vertex binding 15): one per submit instead of 64 bytes per draw. Host bytes that never change, so
// dedup may share them.
Upload zero_stream() {
    static const uint8_t zeros[64] = {};
    if (!draw_slow(kSlowUploads)) return dedup_upload(zeros, sizeof zeros, sizeof zeros, 16);
    Upload z = upload_alloc(64, 16);
    std::memset(z.ptr, 0, 64);
    return z;
}

// draw_inputs (nvn_written.cpp) with the indices shared: a mesh drawn in several passes of a submit (depth, shadows,
// colour) had its index buffer copied each time. Guest bytes, like the vertex streams, so dedup's rule holds; 8-bit
// indices are widened per draw as before.
bool draw_inputs_shared(VkCommandBuffer cb, const ShaderInfo* vs, bool indexed, const Cmd& c, DrawInputs& in) {
    if (!indexed || draw_slow(kSlowUploads))
        return draw_inputs(cb, vs, indexed, c, in);
    const bool engine = engine_vk::present(c);
    const auto type = engine ? engine_vk::decode(c).index_type : vk_index_type(c.mask);
    if (type == VK_INDEX_TYPE_UINT8_EXT)
        return draw_inputs(cb, vs, indexed, c, in);
    draw_inputs(cb, vs, false, c, in);  // the vertex streams
    const uint8_t* p = guest_bytes(c.a[0]);
    if (!p) return false;
    in.index_type = type;
    const uint32_t stride = type == VK_INDEX_TYPE_UINT16 ? 2 : 4;
    const uint64_t bytes = engine ? c.a[4] : uint64_t(uint32_t(c.i[0])) * stride;
    if (!written_read(cb, p, bytes, bytes, stride, in.index)) {
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
        if (watched_geometry_enabled(true) && (c.mask == 1 || c.mask == 2) && bytes >= kWatchedGeometryMinimum &&
            !written_overlaps(p, bytes))
            in.index = watched_geometry_upload(p, bytes, bytes, true);
        else
#endif
            in.index = dedup_upload(p, bytes, bytes, 16);
    }
    return true;
}

void sets_begin_submit() {
    handles_reset();
    descriptor_bytes_begin();
    g_set_generation++, g_set_used = 0, g_set_arena.clear();
    g_support_used = g_constants_used = 0;
}

void sets_log() {
    descriptor_bytes_log();
    if (g_handle_hits + g_handle_misses + g_sets_skipped)
        log("nvn: texture handles: %llu from the submit's cache, %llu looked up; %llu empty descriptor sets skipped",
            (unsigned long long)g_handle_hits, (unsigned long long)g_handle_misses, (unsigned long long)g_sets_skipped);
    if (g_set_hits + g_set_misses)
        log("nvn: descriptor sets: %llu reused from the submit's cache, %llu made", (unsigned long long)g_set_hits,
            (unsigned long long)g_set_misses);
    g_handle_hits = g_handle_misses = g_sets_skipped = g_set_hits = g_set_misses = 0;
}

}  // namespace nx::nvn::replay

