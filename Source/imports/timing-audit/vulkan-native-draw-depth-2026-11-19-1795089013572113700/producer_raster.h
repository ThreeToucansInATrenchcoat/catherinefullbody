#pragma once
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <type_traits>
#include "nvn_draw_state.h"

namespace nx::nvn::producer_raster {
constexpr uint64_t packet_tag = 0x31545352504b5655ull;
enum Field : uint32_t { Viewport = 1, Scissor = 2, DepthRange = 4, Blend = 8, Raster = 16, DepthOutput = 32 };
constexpr uint32_t all_fields = Viewport | Scissor | DepthRange | Blend | Raster | DepthOutput;
struct DynamicState {
    uint32_t raster[10] = {0,0,255,255,255,255,0,0,0,0};
    float blend[4] = {};
};
struct Packet {
    uint32_t fields = 0, raster_mask = 0;
    DynamicState dynamic;
    VkViewport viewport{};
    VkRect2D scissor{};
    float depth_near = 0, depth_far = 1;
    uint32_t writes_depth_stencil = 0;
};
static_assert(std::is_trivially_copyable_v<Packet>);
// perf 2026-11-22: default 0 (legacy reference path). The producer lowering cost ~30 FPS at the
// Stage 9/6 boss and ~12 FPS in the Day 5 bar; set the variable to 1 (or 2 to verify) to re-enable.
inline int mode() {
    static const int value = [] { const char* p = std::getenv("CATHERINE_VULKAN_PRODUCER_RASTER"); return p ? std::atoi(p) : 0; }();
    return value;
}
inline uint32_t affected(const Cmd& c, uint32_t& raster_mask) {
    raster_mask = 0;
    switch (c.op) {
        case Cmd::SetViewport: return Viewport;
        case Cmd::SetScissor: return Scissor;
        case Cmd::SetDepthRange: return DepthRange;
        case Cmd::SetBlendColor: return Blend;
        case Cmd::SetStencilRef: case Cmd::SetStencilMask: case Cmd::SetStencilValueMask: {
            const uint32_t first = c.op == Cmd::SetStencilRef ? 0 : c.op == Cmd::SetStencilValueMask ? 2 : 4;
            raster_mask = (c.mask & 3) << first;
            return Raster;
        }
        case Cmd::SetPolygonOffsetClamp: raster_mask = 7u << 6; return Raster;
        case Cmd::BindDepthStencilState: raster_mask = 1u << 9; return Raster | DepthOutput;
        default: return 0;
    }
}
// Runs only at game recording. Packet contains values, no guest pointers or Vulkan handles.
inline bool encode(const Cmd& c, const void* blob, size_t bytes, Packet& p) {
    p.fields = affected(c, p.raster_mask);
    if (!p.fields) return false;
    switch (c.op) {
        case Cmd::SetViewport:
            p.viewport = {float(c.i[0]), float(c.i[1]) + float(c.i[3]), float(c.i[2]), -float(c.i[3]), 0, 1}; break;
        case Cmd::SetScissor:
            p.scissor = {{std::max(0,c.i[0]),std::max(0,c.i[1])}, {uint32_t(std::max(0,c.i[2])),uint32_t(std::max(0,c.i[3]))}}; break;
        case Cmd::SetDepthRange: p.depth_near = c.f[0]; p.depth_far = c.f[1]; break;
        case Cmd::SetBlendColor: std::memcpy(p.dynamic.blend,c.f,16); break;
        case Cmd::SetStencilRef: case Cmd::SetStencilMask: case Cmd::SetStencilValueMask:
            for (uint32_t i=0;i<6;++i) if (p.raster_mask >> i & 1) p.dynamic.raster[i] = c.n & 255; break;
        case Cmd::SetPolygonOffsetClamp: std::memcpy(p.dynamic.raster+6,c.f,12); break;
        case Cmd::BindDepthStencilState: {
            if (!blob || bytes < sizeof(DepthStencilInfo)) return false;
            DepthStencilInfo d{}; std::memcpy(&d,blob,sizeof d);
            p.dynamic.raster[9] = d.depth_test && equal_slack_pass(d);
            p.writes_depth_stencil = d.depth_write || d.stencil_test; break;
        }
        default: return false;
    }
    return true;
}
inline std::atomic<uint64_t> recorded_packets{0};
inline void attach(Cmd& c, const void*& blob, size_t& bytes, std::vector<uint8_t>& owned) {
    if (!mode()) return;
    Packet p; std::memset(&p,0,sizeof p);
    if (!encode(c,blob,bytes,p)) return;
    const size_t prefix = blob ? bytes : 0;
    owned.resize(prefix + sizeof p);
    if (prefix) std::memcpy(owned.data(),blob,prefix);
    std::memcpy(owned.data()+prefix,&p,sizeof p);
    c.a[7] = prefix; c.a[8] = packet_tag;
    blob = owned.data(); bytes = owned.size();
    recorded_packets.fetch_add(1,std::memory_order_relaxed);
}
struct Shadow {
    uint32_t valid = all_fields, raster_valid = 1023;
    DynamicState dynamic;
    VkViewport viewport{};
    VkRect2D scissor{};
    float depth_near = 0, depth_far = 1;
    bool has_viewport = false, has_scissor = false, writes_depth_stencil = false;
};
inline Shadow state; // Replay thread owns state; producer never reads or writes it.
inline uint64_t replayed_packets = 0, unsupported_packets = 0, prepared_draws = 0, compared_draws = 0;
// Apply in command order, including called lists. No save/restore at a CallList boundary: NVN state inherits.
inline void apply(const Cmd& c, const CmdList& list) {
    if (!mode()) return;
    uint32_t raster_mask = 0; const uint32_t fields = affected(c,raster_mask);
    if (!fields) return;
    const uint8_t* data = nullptr;
    if (c.a[8] == packet_tag && c.blob < list.blobs.size()) {
        const auto& b = list.blobs[c.blob];
        if (c.a[7] <= b.size() && sizeof(Packet) <= b.size()-size_t(c.a[7])) data = b.data()+c.a[7];
    }
    if (!data) { state.valid &= ~fields; state.raster_valid &= ~raster_mask; ++unsupported_packets; return; }
    Packet p; std::memcpy(&p,data,sizeof p);
    if (p.fields != fields || p.raster_mask != raster_mask) {
        state.valid &= ~fields; state.raster_valid &= ~raster_mask; ++unsupported_packets; return;
    }
    ++replayed_packets;
    if (fields & Viewport) state.viewport=p.viewport,state.has_viewport=true;
    if (fields & Scissor) state.scissor=p.scissor,state.has_scissor=true;
    if (fields & DepthRange) state.depth_near=p.depth_near,state.depth_far=p.depth_far;
    if (fields & Blend) std::memcpy(state.dynamic.blend,p.dynamic.blend,16);
    if (fields & DepthOutput) state.writes_depth_stencil=p.writes_depth_stencil!=0;
    for (uint32_t i=0;i<10;++i) if (raster_mask >> i & 1) state.dynamic.raster[i]=p.dynamic.raster[i];
    state.valid |= fields; state.raster_valid |= raster_mask;
}
inline DynamicState legacy_dynamic(const replay::State& s) {
    DynamicState d;
    d.raster[0]=s.stencil_ref[0];d.raster[1]=s.stencil_ref[1];
    d.raster[2]=s.stencil_compare_mask[0];d.raster[3]=s.stencil_compare_mask[1];
    d.raster[4]=s.stencil_write_mask[0];d.raster[5]=s.stencil_write_mask[1];
    std::memcpy(d.raster+6,&s.bias_factor,4);std::memcpy(d.raster+7,&s.bias_units,4);std::memcpy(d.raster+8,&s.bias_clamp,4);
    d.raster[9]=s.depth.depth_test&&equal_slack_pass(s.depth);std::memcpy(d.blend,s.blend_color,16);return d;
}
struct Prepared {
    DynamicState dynamic;
    VkViewport viewport{};VkRect2D scissor{};
    bool viewport_uses_extent=false,scissor_uses_extent=false,writes_depth_stencil=false;
};
inline Prepared legacy(const replay::State& s,VkExtent2D extent) {
    Prepared p;p.dynamic=legacy_dynamic(s);p.viewport_uses_extent=!s.has_viewport;p.scissor_uses_extent=!s.has_scissor;
    p.viewport={0,float(extent.height),float(extent.width),-float(extent.height),0,1};
    if(s.has_viewport)p.viewport={float(s.viewport[0]),float(s.viewport[1])+float(s.viewport[3]),float(s.viewport[2]),-float(s.viewport[3]),s.depth_near,s.depth_far};
    p.scissor={{0,0},extent};
    if(s.has_scissor)p.scissor={{std::max(0,s.scissor[0]),std::max(0,s.scissor[1])},{uint32_t(std::max(0,s.scissor[2])),uint32_t(std::max(0,s.scissor[3]))}};
    p.writes_depth_stencil=s.depth.depth_write||s.depth.stencil_test;return p;
}
inline Prepared prepare(const replay::State& s,VkExtent2D extent) {
    if (!mode()) return legacy(s,extent);
    Prepared p;p.dynamic=state.dynamic;
    const bool vp_known=state.valid&Viewport,sc_known=state.valid&Scissor;
    const bool has_vp=vp_known?state.has_viewport:s.has_viewport,has_sc=sc_known?state.has_scissor:s.has_scissor;
    p.viewport_uses_extent=!has_vp;p.scissor_uses_extent=!has_sc;
    p.viewport=has_vp?(vp_known?state.viewport:VkViewport{float(s.viewport[0]),float(s.viewport[1])+float(s.viewport[3]),float(s.viewport[2]),-float(s.viewport[3]),0,1}):VkViewport{0,float(extent.height),float(extent.width),-float(extent.height),0,1};
    if(has_vp){p.viewport.minDepth=state.valid&DepthRange?state.depth_near:s.depth_near;p.viewport.maxDepth=state.valid&DepthRange?state.depth_far:s.depth_far;}
    p.scissor=has_sc?(sc_known?state.scissor:VkRect2D{{std::max(0,s.scissor[0]),std::max(0,s.scissor[1])},{uint32_t(std::max(0,s.scissor[2])),uint32_t(std::max(0,s.scissor[3]))}}):VkRect2D{{0,0},extent};
    if(!(state.valid&Blend))std::memcpy(p.dynamic.blend,s.blend_color,16);
    if(state.raster_valid!=1023){const auto d=legacy_dynamic(s);for(uint32_t i=0;i<10;++i)if(!(state.raster_valid>>i&1))p.dynamic.raster[i]=d.raster[i];}
    p.writes_depth_stencil=state.valid&DepthOutput?state.writes_depth_stencil:(s.depth.depth_write||s.depth.stencil_test);
    ++prepared_draws;
    if(mode()==2){const auto old=legacy(s,extent);if(std::memcmp(&p.viewport,&old.viewport,sizeof p.viewport)||std::memcmp(&p.scissor,&old.scissor,sizeof p.scissor)||std::memcmp(&p.dynamic,&old.dynamic,sizeof p.dynamic)||p.writes_depth_stencil!=old.writes_depth_stencil||p.viewport_uses_extent!=old.viewport_uses_extent||p.scissor_uses_extent!=old.scissor_uses_extent)fatal("Producer Vulkan raster packet disagrees with compatibility state");if(++compared_draws==1||compared_draws%10000==0)log("Producer Vulkan raster verified: %llu draws, %llu packet applications, %llu unsupported",(unsigned long long)compared_draws,(unsigned long long)replayed_packets,(unsigned long long)unsupported_packets);}
    return p;
}
}
