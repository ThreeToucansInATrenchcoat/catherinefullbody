#include "C:/catherine/recomp/Source/imports/timing-audit/perf-2026-11-22/phase_timer.h"
#include "submit_audit29.h"
// The replay of recorded NVN command lists into a Vulkan command buffer (split from nvn_vk.cpp): render targets,
// clears, calls into other lists, copies; draws and state go to nvn_draw.cpp.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "nvn_draw_state.h"
#include "root9_replay_scope.h"
#include "nvn_vk.h"
#include "engine_vk_indexed.h"
#include "producer_raster.h"
#include "producer_clears.h"
#include "producer_pipeline.h"
#include "producer_fixed_pipeline.h"
#include "producer_targets.h"

namespace nx::nvn {
void fast89_flush_invalidate(VkCommandBuffer cb);  // batch91 (nvn_draw.cpp)
void fast89_flush_invalidate_clear155(VkCommandBuffer cb, const Targets& st, const Cmd& c);  // cmd155
void fast89_flush_invalidate_copy159(VkCommandBuffer cb, const Cmd& c);  // cmd159

using namespace nx::vk;

namespace {
// The lists called during one submit, by handle (PHASE4.md C5): command_list() takes a mutex and walks a map, and a
// course frame calls hundreds of lists, each looked up twice (the dispatch count, then the replay). The first lookup
// of a handle in a submit is kept for the rest of it: a list re-recorded meanwhile on the game's recording thread
// is not mixed into a submit already being replayed. NX_CALL_TABLE=0: every call looks up.
std::unordered_map<uint64_t, std::shared_ptr<CmdList>> g_called;
}  // namespace

void submit_lists_reset() { g_called.clear(); }

std::shared_ptr<CmdList> submit_command_list(uint64_t handle) {
    static const bool off = [] {
        const char* e = std::getenv("NX_CALL_TABLE");
        return e && e[0] == '0';
    }();
    if (off) return command_list(handle);
    auto it = g_called.find(handle);
    if (it != g_called.end()) return it->second;
    auto l = command_list(handle);
    if (l) g_called.emplace(handle, l);
    return l;
}

void replay_list(VkCommandBuffer cb, Targets& st, const CmdList& list, int depth) {
    const uint64_t ps_list = depth == 0 ? perf22::start() : 0;
    struct PsEnd { uint64_t t; ~PsEnd() { perf22::add(perf22::ReplayList, t); } } ps_end{ps_list};
    catherine::root9::ReplayScope root9_scope;
    if (skin_lists_on()) skin_list_replayed(list, depth);
    for (const Cmd& c : list.cmds) {
        producer_raster::apply(c,list);
        producer_pipeline::apply(c,list);
        producer_fixed::apply(c,list);
        if (c.op == Cmd::ClearColor || c.op == Cmd::ClearDepthStencil)
            fast89_flush_invalidate_clear155(cb, st, c);  // cmd155: batches; that image's pending resolves
        else if (c.op == Cmd::CopyTextureToTexture || c.op == Cmd::CopyBufferToTexture || c.op == Cmd::ClearTexture)
            fast89_flush_invalidate_copy159(cb, c);  // cmd159: batches; the touched images, pending resolves
        cb = frame_chunk(cb, replay::g_stats.done, pass_open());  // part of the submit queued early (PLAN.md R1)
        // NX_TRACE_CMDS=<first draw>: the ops replayed from that draw on (20000), with n, mask and a[0].
        static const char* tc = std::getenv("NX_TRACE_CMDS");
        static const uint64_t tc_from = tc ? std::strtoull(tc, nullptr, 10) : 0;
        static int tc_n = 0;
        if (tc && replay::g_stats.done >= tc_from && tc_n++ < 20000)
            log("cmd after draw %llu: op %u n %u mask %x a0 %llx a1 %llx i %d %d %d %d f %g %g %g %g depth %d",
                (unsigned long long)replay::g_stats.done, unsigned(c.op), c.n, c.mask, (unsigned long long)c.a[0],
                (unsigned long long)c.a[1], c.i[0], c.i[1], c.i[2], c.i[3], c.f[0], c.f[1], c.f[2], c.f[3], depth);
        producer_clears::Intent clear_intent;
        if (producer_clears::read(c,list,clear_intent)) {
            draw_clear_native(cb,st,clear_intent);
            continue;
        }
        if (engine_vk::present(c)) {
            if (engine_vk::mode() == 2) {
                const auto packet = engine_vk::decode(c);
                if (packet.args.indexCount != uint32_t(c.i[0]) || packet.args.instanceCount != uint32_t(std::max(1,c.i[3])) ||
                    packet.args.firstIndex != 0 || packet.args.vertexOffset != c.i[1] ||
                    packet.args.firstInstance != uint32_t(c.i[2]) || packet.index_type != vk_index_type(c.mask) ||
                    c.a[4] != uint64_t(uint32_t(c.i[0])) * index_size(c.mask))
                    fatal("Engine Vulkan indexed draw payload does not match legacy arguments");
                static uint64_t passed = 0;
                if (++passed == 1 || passed % 100000 == 0)
                    log("Engine Vulkan indexed draws verified: %llu", (unsigned long long)passed);
            }
            draw_call(cb, st, c.n, true, c);
            continue;
        }
        if (engine_vk::arrays_present(c)) {
            if (engine_vk::mode() == 2) {
                const auto p = engine_vk::decode_arrays(c);
                if (p.vertexCount != uint32_t(c.i[1]) || p.instanceCount != uint32_t(c.i[3]) ||
                    p.firstVertex != uint32_t(c.i[0]) || p.firstInstance != uint32_t(c.i[2]))
                    fatal("Engine Vulkan array draw payload does not match legacy arguments");
                static uint64_t passed = 0;
                if (++passed == 1 || passed % 10000 == 0)
                    log("Engine Vulkan array draws verified: %llu", (unsigned long long)passed);
            }
            draw_call(cb, st, c.n, false, c);
            continue;
        }
        if (engine_vk::dispatch_present(c)) {
            if (engine_vk::mode() == 2) {
                const auto p = engine_vk::decode_dispatch(c);
                if (p.x != uint32_t(c.i[0]) || p.y != uint32_t(c.i[1]) || p.z != uint32_t(c.i[2]))
                    fatal("Engine Vulkan dispatch payload does not match legacy arguments");
                static uint64_t passed = 0;
                if (++passed == 1 || passed % 10000 == 0)
                    log("Engine Vulkan compute dispatches verified: %llu", (unsigned long long)passed);
            }
            dispatch_call(cb, c);
            cb = frame_after_dispatch(cb);
            continue;
        }
        switch (c.op) {
            case Cmd::SetRenderTargets: {  // the pass ends only when a draw or clear needs other targets
                static const bool always = std::getenv("NX_TARGETS_BREAK") != nullptr;  // bisecting: the old way
                if (always) draw_end_rendering(cb, 7);
                if(producer_targets::read(c,list,st)) break;
                st.color_count = c.n;
                for (uint32_t k = 0; k < 8; k++) st.colors[k] = k < c.n ? c.a[k] : 0;
                st.depth = c.a[8];
                st.views = {};
                if (c.blob < list.blobs.size() && (list.blobs[c.blob].size() == sizeof(TargetViews) || (c.mask==producer_targets::tag && list.blobs[c.blob].size()==sizeof(producer_targets::Payload))))
                    std::memcpy(&st.views, list.blobs[c.blob].data(), sizeof st.views);
                break;
            }
            case Cmd::ClearColor:
            case Cmd::ClearDepthStencil: draw_clear(cb, st, c); break;
            case Cmd::CallList:
                submit_audit29::call();
                if (depth < 8) {
                    if (auto l = submit_command_list(c.a[0])) {
                        replay_list(cb, st, *l, depth + 1);
                        cb = frame_cb(cb);  // NX_PIPELINED: the called list may have split the submit
                    } else {
                        static uint64_t missing = 0;
                        if (++missing <= 20 || missing % 1000 == 0)
                            log("nvn: called handle %llx is unknown (%llu calls of unknown handles): %s",
                                (unsigned long long)c.a[0], (unsigned long long)missing,
                                explain_handle(c.a[0]).c_str());
                    }
                }
                break;
            case Cmd::CopyTextureToTexture:
            case Cmd::CopyBufferToTexture:
            case Cmd::ClearTexture:
                draw_end_rendering(cb, 5);
                if (gpu_profile_on()) gpu_profile_mark(cb, "texture copies");
                if(c.op==Cmd::CopyBufferToTexture)submit_audit29::copy();
                replay_copy(cb, c, list);
                if (gpu_profile_on()) gpu_profile_mark(cb, "outside passes (clears, uploads, copies)");
                break;
            default:
                draw_command(cb, c, list, st);
                // NX_PIPELINED: after the submit's last dispatch the rest goes into a second command buffer.
                if (c.op == Cmd::DispatchCompute) cb = frame_after_dispatch(cb);
                break;
        }
    }
}

}  // namespace nx::nvn
