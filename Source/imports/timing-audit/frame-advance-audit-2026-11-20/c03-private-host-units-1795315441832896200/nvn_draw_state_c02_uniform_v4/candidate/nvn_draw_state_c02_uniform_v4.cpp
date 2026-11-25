// The replay's state commands (split from nvn_draw.cpp): binds, viewports, uniform updates, counters; draws and
// dispatches go back to nvn_draw.cpp.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "nvn_draw_state.h"
#include "block20_command_packet.h"
#include "root15_command_packet.h"
#include "root9_uniform_packet.h"
#include "platform.h"

#include "c02_uniform_provenance.h"
#include "c03_command_packet.h"

namespace nx::nvn {

using namespace replay;

void draw_command(VkCommandBuffer cb, const Cmd& c, const CmdList& list, const Targets& targets) {
    State& s = g_state;
    auto blob = [&](size_t size) -> const uint8_t* {
        return c.blob < list.blobs.size() && list.blobs[c.blob].size() >= size ? list.blobs[c.blob].data() : nullptr;
    };
    switch (c.op) {
        case Cmd::SetViewport:
            std::memcpy(s.viewport, c.i, sizeof s.viewport), s.has_viewport = true;
            break;
        case Cmd::SetRasterizerDiscard: s.rasterizer_discard = c.n != 0; break;
        case Cmd::BindImage:
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 16) s.images[c.i[0]][c.i[1]] = c.a[0];
            break;
        case Cmd::DispatchCompute: dispatch_call(cb, c); break;
        case Cmd::SetRenderEnable: s.render_enable = c.n != 0; break;
        case Cmd::SetScissor:
            std::memcpy(s.scissor, c.i, sizeof s.scissor), s.has_scissor = true;
            break;
        case Cmd::SetDepthRange:
            s.depth_near = c.f[0], s.depth_far = c.f[1];
            break;
        case Cmd::BindProgram: {
            s.program = c.a[0];
            const auto* shaders = program_shaders(c.a[0]);
            static int logged = 0;
            if (logged++ < 40) {
                log("bind program %#llx mask %#x shaders %zu", (unsigned long long)c.a[0], c.mask,
                    shaders ? shaders->size() : 0);
                if (shaders)
                    for (const auto* shader : *shaders) log(" stage %s %s", stage_name(shader->stage), shader->hash.c_str());
            }
            constexpr uint32_t compute_bit = 1u << 5;
            auto graphics_bit = [](ShaderStage stage) -> uint32_t {
                return stage == ShaderStage::Vertex ? 1u : stage == ShaderStage::Fragment ? 2u : 0u;
            };
            // NVN can keep the graphics stages and a compute program bound at once. Each Vulkan bind point
            // needs its own stage list, so compute bindings never change the layout of a cached graphics pipeline.
            if (c.mask & compute_bit) s.compute_shader = nullptr;
            std::erase_if(s.graphics_shaders, [&](const ShaderInfo* shader) { return graphics_bit(shader->stage) & c.mask; });
            if (shaders)
                for (const auto* shader : *shaders) {
                    if (shader->stage == ShaderStage::Compute && (c.mask & compute_bit)) s.compute_shader = shader;
                    else if (graphics_bit(shader->stage) & c.mask) s.graphics_shaders.push_back(shader);
                }
            break;
        }
        case Cmd::BindUniformBuffer:
            root9_apply_uniform(c.i[0],c.i[1],c.a[0],c.a[1],c.mask==root9_uniform_magic?blob(sizeof(catherine::root9::RigidPacket)):nullptr);
            c02_uniform_provenance::apply(c.i[0],c.i[1],c.a[0],c.a[1],c.mask);
            c03_apply_uniform(c.i[0],c.i[1],c.a[0],c.a[1],c.mask==c03_uniform_magic&&c.blob<list.blobs.size()&&list.blobs[c.blob].size()==sizeof(CatherineC03RigidPacket)?blob(sizeof(CatherineC03RigidPacket)):nullptr);
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 18) s.uniforms[c.i[0]][c.i[1]] = {c.a[0], c.a[1]};
            break;
        case Cmd::BindStorageBuffer:
            root15_apply_command(c.i[0],c.i[1],c.a[0],c.a[1],c.mask==root15_command_magic?blob(sizeof(Root15CommandPacket)):nullptr);
            block20_apply_command(c.i[0],c.i[1],c.a[0],c.a[1],c.mask==root15_command_magic?blob(sizeof(Root15CommandPacket)):(c.mask==block20_command_magic?blob(sizeof(Block20CommandPacket)):nullptr));
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 16) s.storage[c.i[0]][c.i[1]] = {c.a[0], c.a[1]};
            break;
        case Cmd::BindDeferred: {
            const DeferredValues* dv = current_deferred();
            auto it = dv ? dv->find(c.a[0]) : DeferredValues::const_iterator{};
            uint32_t words = c.mask >= 2 ? 2 : 1;
            int st = c.i[0];
            if (st < 0 || st >= kStages) break;
            for (uint32_t k = 0; k < c.n; k++) {
                auto value = [&](uint32_t w) {  // the submit's snapshot, else the memory now
                    size_t at = size_t(k) * words + w;
                    return dv && it != dv->end() && at < it->second.size() ? it->second[at]
                                                                            : ld<uint64_t>(c.a[0] + 8 * at);
                };
                int slot = c.i[1] + int(k);
                if (c.mask == 0 && slot >= 0 && slot < 32) s.textures[st][slot] = value(0);
                if (c.mask == 1 && slot >= 0 && slot < 16) s.images[st][slot] = value(0);
                if (c.mask == 2 && slot >= 0 && slot < 18) {
                    s.uniforms[st][slot] = {value(0), value(1)};
                    c02_uniform_provenance::deferred(st,slot);
                    c03_apply_uniform(st,slot,0,0,nullptr);
                    root9_apply_uniform(st,slot,0,0,nullptr);
                }
                if (c.mask == 3 && slot >= 0 && slot < 16) {
                    s.storage[st][slot] = {value(0), value(1)};
                    const auto& range = s.storage[st][slot];
                    block20_apply_command(st,slot,range.address,range.size,nullptr);
                    root15_apply_command(st,slot,range.address,range.size,nullptr);
                }
            }
            break;
        }
        case Cmd::BindTexture:
            if (c.i[0] >= 0 && c.i[0] < kStages && c.i[1] >= 0 && c.i[1] < 32) s.textures[c.i[0]][c.i[1]] = c.a[0];
            break;
        case Cmd::BindVertexBuffer:
            if (c.i[0] >= 0 && c.i[0] < 16) s.vertex_buffers[c.i[0]] = {c.a[0], c.a[1]};
            break;
        case Cmd::SetTexturePool: s.texture_pool = c.a[0]; break;
        case Cmd::SetSamplerPool: s.sampler_pool = c.a[0]; break;
        case Cmd::BindVertexAttribs:
            if (auto* p = blob(c.n * sizeof(VertexAttrib)))
                s.attribs.assign(reinterpret_cast<const VertexAttrib*>(p), reinterpret_cast<const VertexAttrib*>(p) + c.n);
            break;
        case Cmd::BindVertexStreams:
            if (auto* p = blob(c.n * sizeof(VertexStream)))
                s.streams.assign(reinterpret_cast<const VertexStream*>(p), reinterpret_cast<const VertexStream*>(p) + c.n);
            break;
        case Cmd::BindBlendState:
            if (auto* p = blob(sizeof(BlendTarget))) {
                BlendTarget b;
                std::memcpy(&b, p, sizeof b);
                if (b.target < 8) s.blend[b.target] = b;
            }
            break;
        case Cmd::BindColorState:
            if (auto* p = blob(sizeof(ColorStateInfo))) std::memcpy(&s.color, p, sizeof s.color);
            break;
        case Cmd::BindChannelMaskState:
            if (auto* p = blob(sizeof(ChannelMaskInfo))) std::memcpy(&s.mask, p, sizeof s.mask);
            break;
        case Cmd::BindDepthStencilState:
            if (auto* p = blob(sizeof(DepthStencilInfo))) std::memcpy(&s.depth, p, sizeof s.depth);
            break;
        case Cmd::BindPolygonState:
            if (auto* p = blob(sizeof(PolygonInfo))) std::memcpy(&s.polygon, p, sizeof s.polygon);
            break;
        case Cmd::BindMultisampleState: s.alpha_to_coverage = c.n != 0; break;
        case Cmd::SetBlendColor: std::memcpy(s.blend_color, c.f, sizeof s.blend_color); break;
        case Cmd::UpdateUniformBuffer: {  // (buffer address, size, offset, data): written in order, like the GPU
            if(c.blob<list.blobs.size())
            {
                c02_uniform_provenance::update(c.a[0]+c.a[2],list.blobs[c.blob].size());
                if(c.a[2]>UINT64_MAX-c.a[0])c03_apply_uniform(0,0,0,0,nullptr);
                else c03_invalidate_uniform(c.a[0]+c.a[2],list.blobs[c.blob].size());
                if(c.a[2]>UINT64_MAX-c.a[0])root15_apply_command(5,0,0,0,nullptr);
                else root15_invalidate_command(c.a[0]+c.a[2],list.blobs[c.blob].size());
                if(c.a[2]>UINT64_MAX-c.a[0])root9_apply_uniform(0,0,0,0,nullptr);
                else root9_invalidate_uniform(c.a[0]+c.a[2],list.blobs[c.blob].size());
            }
            uint64_t off = c.a[2];
            // NX_TRACE_UUB=<first draw>: logs 40 updates from that draw on.
            static const char* uub = std::getenv("NX_TRACE_UUB");
            static const uint64_t uub_from = uub ? std::strtoull(uub, nullptr, 10) : 0;
            static int uub_n = 0;
            if (uub && g_stats.done >= uub_from && uub_n++ < 1500 && c.blob < list.blobs.size()) {
                const auto& b = list.blobs[c.blob];
                float f[8] = {};
                std::memcpy(f, b.data(), std::min<size_t>(b.size(), sizeof f));
                log("uub before draw %llu: buffer %llx size %llu offset %llu data %zu bytes: %g %g %g %g | %g %g %g %g",
                    (unsigned long long)g_stats.done, (unsigned long long)c.a[0], (unsigned long long)c.a[1],
                    (unsigned long long)off, b.size(), f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
            }
            if (c.blob < list.blobs.size())
                if (uint8_t* p = const_cast<uint8_t*>(guest_bytes(c.a[0] + off))) {
                    std::memcpy(p, list.blobs[c.blob].data(), list.blobs[c.blob].size());
                    dedup_invalidate(p, list.blobs[c.blob].size());
                    written_cpu_write(p, list.blobs[c.blob].size());
                }
            break;
        }
        case Cmd::ReportCounter: {  // {value, timestamp}: the work before it is done when the replay gets here
            root15_invalidate_command(c.a[0],16);
            root9_invalidate_uniform(c.a[0],16);
            c03_invalidate_uniform(c.a[0],16);
            c02_uniform_provenance::update(c.a[0],16); // NVN buffer GPU-address domain, before the original guest write.
            static uint64_t reports = 0, types_seen = 0;
            if (c.n < 64 && !(types_seen >> c.n & 1)) types_seen |= 1ull << c.n, log("nvn: first counter report of type %u", c.n);
            if (uint8_t* p = const_cast<uint8_t*>(guest_bytes(c.a[0]))) {
                // NX_FAKE_GPU_TIME=1: every report of a submit carries the submit's start (a GPU with no cost):
                // the game picks its present interval from the timings it measures (bisecting its 30 fps mode).
                static const bool fake = std::getenv("NX_FAKE_GPU_TIME") != nullptr;
                uint64_t v[2] = {++reports, fake ? submit_start_ns() : plat::monotonic_ns()};
                std::memcpy(p, v, sizeof v);
                dedup_invalidate(p, sizeof v);
                written_cpu_write(p, sizeof v);
            }
            break;
        }
        case Cmd::DrawArrays: draw_call(cb, targets, c.n, false, c); break;
        case Cmd::DrawElements: draw_call(cb, targets, c.n, true, c); break;
        default: raster_command(c); break;
    }
}

}  // namespace nx::nvn
