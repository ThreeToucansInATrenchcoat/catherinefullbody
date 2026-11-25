#include "c02_movement_bridge.h"
#include "engine_native_texture.h"
#include "engine_native_layouts.h"
#include "engine_native_polygon_options.h"
#include "engine_native_fixed_groups.h"
#include "engine_native_polygon.h"
#include <cmath>
#include "engine_native_viewport.h"
#include "engine_native_geometry.h"
#include "engine_native_compute.h"
#include "engine_native_arrays.h"
#include "engine_native_front_end.h"
// NVN command buffers (docs/NVN.md, N6): binds, state objects and draws, recorded for nvn_draw.cpp to replay.
//
// State objects are plain structs of the SDK driver; they are decoded here, while recording, through its getters
// (the game reuses its structs right after binding them). Signatures from the game's call sites (nvn_trace.txt):
//   BindProgram(cmd, program, stageBits)            BindUniformBuffer(cmd, stage, index, address, size)
//   BindStorageBuffer(cmd, stage, index, address, size)   BindTexture(cmd, stage, index, handle)
//   BindVertexBuffer(cmd, index, address, size)     BindVertexAttribState(cmd, count, const state*)  (4 bytes each)
//   BindVertexStreamState(cmd, count, const state*) (8 bytes each)   Bind{Blend,Color,...}State(cmd, const state*)
//   UpdateUniformBuffer(cmd, address, size, offset, dataSize, const void* data)
//   DrawArrays(cmd, mode, first, count)             DrawElements(cmd, mode, indexType, count, indexAddress)
//   DrawElementsBaseVertex(..., baseVertex)         DrawElementsInstanced(..., baseVertex, baseInstance, instances)
//   DrawArraysInstanced(cmd, mode, first, count, baseInstance, instances)
// Shader stages are numbered 0 vertex, 1 fragment (the pairs the game binds), 5 compute.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "nvn.h"
#include "engine_vk_indexed.h"
#include "block20_command_packet.h"
#include "root15_command_packet.h"
#include "root9_uniform_packet.h"
#include "nvn_shaders.h"
#include "platform.h"
#include "state_cache84.h"

#include "c02_uniform_provenance.h"

namespace nx::nvn {
#include "native_packed_state_policy.inc"

namespace {

// NX_NVN_STATES=1: log each distinct decoded state once (to check the NVN enum values against what the game uses).
template <typename T>
void log_state(const char* what, const T& s) {
    static const bool on = std::getenv("NX_NVN_STATES") != nullptr;
    if (!on) return;
    static std::mutex lock;
    static std::set<std::string> seen;
    std::string key(reinterpret_cast<const char*>(&s), sizeof s);
    std::lock_guard lk(lock);
    if (!seen.insert(key).second || seen.size() > 200) return;
    std::string words;
    for (size_t k = 0; k + 4 <= sizeof s; k += 4) {
        uint32_t w;
        std::memcpy(&w, reinterpret_cast<const uint8_t*>(&s) + k, 4);
        char b[16];
        std::snprintf(b, sizeof b, " %x", w);
        words += b;
    }
    log("nvn state %s:%s", what, words.c_str());
}

// ---- shaders and resources --------------------------------------------------------------------------------------

void bind_program_typed(uint64_t command,uint64_t program,uint32_t stages) {
    Cmd cmd{Cmd::BindProgram};
    cmd.a[0] = program;
    cmd.mask = uint32_t(stages);
    record(command, cmd);
    static const bool trace = std::getenv("NX_TRACE_PROGRAMS") != nullptr;  // binds of skinned programs (input 6)
    static int logged = 0;
    if (const auto* sh = trace && logged < 40 ? program_shaders(program) : nullptr)
        for (const ShaderInfo* s : *sh)
            for (const ShaderIo& in : s->refl.inputs)
                if (s->stage == ShaderStage::Vertex && in.location == 6 && logged++ < 40)
                    log("nvn: skinned program %llx (vs %s) bound into cmd %llx", (unsigned long long)program,
                        s->hash.c_str(), (unsigned long long)command);
}
void BindProgram(CPU& c){bind_program_typed(c.x[0],c.x[1],uint32_t(c.x[2]));}

void BindUniformBuffer_native(const uint64_t* x) {
    Cmd cmd{Cmd::BindUniformBuffer};
    cmd.i[0] = int32_t(x[1]), cmd.i[1] = int32_t(x[2]);
    cmd.a[0] = x[3], cmd.a[1] = x[4];
    if(cmd.i[0]==0&&cmd.i[1]==0) {
        const bool owned=catherine_c02_c3_record_owned(cmd.a[0],cmd.a[1])!=0;
        if(owned)cmd.mask=c02_uniform_provenance::command_magic;
        c02_uniform_provenance::record(owned);
    }
    catherine::root9::RigidPacket packet9;if(root9_capture_uniform(x[0],cmd,packet9)){cmd.mask=root9_uniform_magic;record(x[0],cmd,&packet9,sizeof packet9);}else record(x[0],cmd);
}
void BindUniformBuffer(CPU& c) { BindUniformBuffer_native(c.x); }

void BindStorageBuffer_native(const uint64_t* x) {
    Cmd cmd{Cmd::BindStorageBuffer};
    cmd.i[0] = int32_t(x[1]), cmd.i[1] = int32_t(x[2]);
    cmd.a[0] = x[3], cmd.a[1] = x[4];
    Root15CommandPacket packet15;Block20CommandPacket packet;if(root15_capture_command(cmd,packet15)){cmd.mask=root15_command_magic;record(x[0],cmd,&packet15,sizeof packet15);}else if(block20_capture_command(cmd,packet)){cmd.mask=block20_command_magic;record(x[0],cmd,&packet,sizeof packet);}else record(x[0],cmd);
}
void BindStorageBuffer(CPU& c) { BindStorageBuffer_native(c.x); }

void BindTexture_native(const uint64_t* x) {
    Cmd cmd{Cmd::BindTexture};
    cmd.i[0] = int32_t(x[1]), cmd.i[1] = int32_t(x[2]);
    cmd.a[0] = x[3];
    record(x[0], cmd);
}
void BindTexture(CPU& c) { BindTexture_native(c.x); }

void BindVertexBuffer_native(const uint64_t* x) {
    Cmd cmd{Cmd::BindVertexBuffer};
    cmd.i[0] = int32_t(x[1]);
    cmd.a[0] = x[2], cmd.a[1] = x[3];
    record(x[0], cmd);
}
void BindVertexBuffer(CPU& c) { BindVertexBuffer_native(c.x); }

void SetTexturePool_native(const uint64_t* x) {
    Cmd cmd{Cmd::SetTexturePool};
    cmd.a[0] = x[1];
    record(x[0], cmd);
}
void SetTexturePool(CPU& c) { SetTexturePool_native(c.x); }

void SetSamplerPool_native(const uint64_t* x) {
    Cmd cmd{Cmd::SetSamplerPool};
    cmd.a[0] = x[1];
    record(x[0], cmd);
}
void SetSamplerPool(CPU& c) { SetSamplerPool_native(c.x); }

void UpdateUniformBuffer_native(const uint64_t* x) {  // the data is copied now: the game's buffer is on its stack
    Cmd cmd{Cmd::UpdateUniformBuffer};
    cmd.a[0] = x[1], cmd.a[1] = x[2], cmd.a[2] = x[3];
    size_t n = size_t(x[4]);
    record(x[0], cmd, reinterpret_cast<const void*>(x[5]), n);
}
void UpdateUniformBuffer(CPU& c) { UpdateUniformBuffer_native(c.x); }

// ---- state objects ------------------------------------------------------------------------------------------------

#include "state_binds84_native.inc"

void SetBlendColor(CPU& c) {
    Cmd cmd{Cmd::SetBlendColor};
    if (c.x[1]) std::memcpy(cmd.f, reinterpret_cast<const void*>(c.x[1]), 16);
    record(c.x[0], cmd);
}

void SetDepthRange(CPU& c) {  // (cmd, float near [s0], float far [s1])
    Cmd cmd{Cmd::SetDepthRange};
    cmd.f[0] = c.v[0].f32[0], cmd.f[1] = c.v[1].f32[0];
    record(c.x[0], cmd);
}

std::atomic<uint64_t> g_recorded_draws{0};

// ---- draws --------------------------------------------------------------------------------------------------------

void DrawArrays_native(const uint64_t* x) {
    g_recorded_draws++;
    Cmd cmd{Cmd::DrawArrays};
    cmd.n = uint32_t(x[1]);                                    // mode
    cmd.i[0] = int32_t(x[2]), cmd.i[1] = int32_t(x[3]);      // first, count
    cmd.i[2] = 0, cmd.i[3] = 1;                                  // base instance, instances
    if (engine_vk::mode() && cmd.n != 7 && cmd.i[0] >= 0 && cmd.i[1] > 0 && cmd.i[3] > 0)
        engine_vk::encode_arrays(cmd);
    record(x[0], cmd);
}
void DrawArrays(CPU& c) { DrawArrays_native(c.x); }

void DrawArraysInstanced_native(const uint64_t* x) {
    Cmd cmd{Cmd::DrawArrays};
    cmd.n = uint32_t(x[1]);
    cmd.i[0] = int32_t(x[2]), cmd.i[1] = int32_t(x[3]);
    cmd.i[2] = int32_t(x[4]), cmd.i[3] = int32_t(x[5]);
    if (engine_vk::mode() && cmd.n != 7 && cmd.i[0] >= 0 && cmd.i[1] > 0 && cmd.i[3] > 0)
        engine_vk::encode_arrays(cmd);
    record(x[0], cmd);
}
void DrawArraysInstanced(CPU& c) { DrawArraysInstanced_native(c.x); }

void draw_elements(const uint64_t* x, int32_t base_vertex, int32_t base_instance, int32_t instances, bool engine_draw = false) {
    g_recorded_draws++;
    Cmd cmd{Cmd::DrawElements};
    cmd.n = uint32_t(x[1]);        // mode
    cmd.mask = uint32_t(x[2]);     // index type: 0 u8, 1 u16, 2 u32
    cmd.i[0] = int32_t(x[3]);      // count
    cmd.a[0] = x[4];               // index buffer address
    cmd.i[1] = base_vertex, cmd.i[2] = base_instance, cmd.i[3] = instances;
    // Every direct indexed variant has the same Vulkan argument contract.
    // Indirect/GPU-generated argument buffers retain their separate path.
    if (engine_draw || (engine_vk::mode() && cmd.mask <= 2 && cmd.i[0] > 0 &&
                        cmd.i[3] > 0 && cmd.a[0]))
        engine_vk::encode(cmd);
    record(x[0], cmd);
}

void DrawElements_native(const uint64_t* x) { draw_elements(x, 0, 0, 1); }
void DrawElements(CPU& c) { DrawElements_native(c.x); }
void DrawElementsBaseVertex_native(const uint64_t* x) { draw_elements(x, int32_t(x[5]), 0, 1); }
void DrawElementsBaseVertex(CPU& c) { DrawElementsBaseVertex_native(c.x); }
void DrawElementsInstanced_native(const uint64_t* x) { draw_elements(x, int32_t(x[5]), int32_t(x[6]), int32_t(x[7])); }
void DrawElementsInstanced(CPU& c) { DrawElementsInstanced_native(c.x); }

void ignore(CPU&) {}

// ReportCounter(cmd, NVNcounterType, NVNbufferAddress): the GPU writes {u64 value, u64 timestamp} there when it gets
// to this point. Engines read these to know how far the GPU is (Wonder skips drawing the scene when they never
// arrive), so the replay writes them.
void ReportCounter_native(const uint64_t* x) {
    Cmd cmd{Cmd::ReportCounter};
    cmd.n = uint32_t(x[1]);
    cmd.a[0] = x[2];
    record(x[0], cmd);
}
void ReportCounter(CPU& c) { ReportCounter_native(c.x); }

// Our GPU timestamps are host nanoseconds: the conversions are identities.
void GetTimestampInNanoseconds(CPU& c) { c.x[0] = c.x[1] ? ld<uint64_t>(c.x[1] + 8) : 0; }  // (device, counterData*)
void GetCurrentTimestampInNanoseconds(CPU& c) { c.x[0] = plat::monotonic_ns(); }


}  // namespace


using RecordingFn = void (*)(const uint64_t*);
RecordingFn engine_compute_recording_handler(HleFn handler);

RecordingFn game_recording_handler(HleFn handler) {
    if (auto compute = engine_compute_recording_handler(handler)) return compute;
    if (handler == BindUniformBuffer) return BindUniformBuffer_native;
    if (handler == BindStorageBuffer) return BindStorageBuffer_native;
    if (handler == BindTexture) return BindTexture_native;
    if (handler == BindVertexBuffer) return BindVertexBuffer_native;
    if (handler == SetTexturePool) return SetTexturePool_native;
    if (handler == SetSamplerPool) return SetSamplerPool_native;
    if (handler == UpdateUniformBuffer) return UpdateUniformBuffer_native;
    if (handler == DrawArrays) return DrawArrays_native;
    if (handler == DrawArraysInstanced) return DrawArraysInstanced_native;
    if (handler == DrawElements) return DrawElements_native;
    if (handler == DrawElementsBaseVertex) return DrawElementsBaseVertex_native;
    if (handler == DrawElementsInstanced) return DrawElementsInstanced_native;
    if (handler == ReportCounter) return ReportCounter_native;
    return nullptr;
}

bool record_engine_indexed(RecordingFn recording, const uint64_t* registers) {
    if (recording != DrawElementsBaseVertex_native || !engine_vk::mode()) return false;
    const auto index_type = uint32_t(registers[2]);
    if ((index_type != 1 && index_type != 2) || int32_t(registers[3]) <= 0 || !registers[4])
        return false;
    draw_elements(registers, int32_t(registers[5]), 0, 1, true);
    return true;
}

uint64_t recorded_draws() { return g_recorded_draws; }

NX_HLE("nvnCommandBufferBindProgram", BindProgram);
NX_HLE("nvnCommandBufferBindUniformBuffer", BindUniformBuffer);
NX_HLE("nvnCommandBufferBindStorageBuffer", BindStorageBuffer);
NX_HLE("nvnCommandBufferBindTexture", BindTexture);
NX_HLE("nvnCommandBufferBindVertexBuffer", BindVertexBuffer);
NX_HLE("nvnCommandBufferSetTexturePool", SetTexturePool);
NX_HLE("nvnCommandBufferSetSamplerPool", SetSamplerPool);
NX_HLE("nvnCommandBufferUpdateUniformBuffer", UpdateUniformBuffer);
NX_HLE("nvnCommandBufferBindVertexAttribState", BindVertexAttribState);
NX_HLE("nvnCommandBufferBindVertexStreamState", BindVertexStreamState);
NX_HLE("nvnCommandBufferBindBlendState", BindBlendState);
NX_HLE("nvnCommandBufferBindColorState", BindColorState);
NX_HLE("nvnCommandBufferBindChannelMaskState", BindChannelMaskState);
NX_HLE("nvnCommandBufferBindDepthStencilState", BindDepthStencilState);
NX_HLE("nvnCommandBufferBindPolygonState", BindPolygonState);
NX_HLE("nvnCommandBufferBindMultisampleState", BindMultisampleState);
NX_HLE("nvnCommandBufferSetBlendColor", SetBlendColor);
NX_HLE("nvnCommandBufferSetDepthRange", SetDepthRange);
NX_HLE("nvnCommandBufferDrawArrays", DrawArrays);
NX_HLE("nvnCommandBufferDrawArraysInstanced", DrawArraysInstanced);
NX_HLE("nvnCommandBufferDrawElements", DrawElements);
NX_HLE("nvnCommandBufferDrawElementsBaseVertex", DrawElementsBaseVertex);
NX_HLE("nvnCommandBufferDrawElementsInstanced", DrawElementsInstanced);
NX_HLE("nvnCommandBufferBarrier", ignore);
NX_HLE("nvnCommandBufferReportCounter", ReportCounter);
NX_HLE("nvnDeviceGetTimestampInNanoseconds", GetTimestampInNanoseconds);
NX_HLE("nvnDeviceGetCurrentTimestampInNanoseconds", GetCurrentTimestampInNanoseconds);  // every replayed command is serialized for now

}  // namespace nx::nvn


namespace nx::nvn {
// This is the game producer's typed entry. It never accepts Cpu or an NVN ABI
// register array. Cmd keeps compatibility identity/order while its payload owns
// the actual Vulkan draw arguments consumed before the legacy replay switch.
void emit_engine_indexed_typed(uint64_t command, const CatherineEngineIndexedDraw& d, bool parity) {
    ++g_recorded_draws;
    Cmd cmd{Cmd::DrawElements};
    cmd.n=d.primitive;cmd.mask=d.index_type;
    cmd.i[0]=int32_t(d.index_count);cmd.i[1]=d.base_vertex;
    cmd.i[2]=0;cmd.i[3]=1;cmd.a[0]=d.index_address;
    if (engine_vk::mode() && cmd.mask <= 2 && cmd.i[0] > 0 && cmd.a[0])
        engine_vk::encode(cmd);
    if (parity) {
        if (cmd.n!=d.primitive || cmd.mask!=d.index_type ||
            uint32_t(cmd.i[0])!=d.index_count || cmd.i[1]!=d.base_vertex ||
            cmd.i[2]!=0 || cmd.i[3]!=1 || cmd.a[0]!=d.index_address)
            fatal("Native engine indexed producer differs from SDK draw contract");
        if (engine_vk::present(cmd)) {
            const auto native=engine_vk::decode(cmd);
            const auto& a=native.args;
            if (a.indexCount!=d.index_count || a.instanceCount!=1 || a.firstIndex!=0 ||
                a.vertexOffset!=d.base_vertex || a.firstInstance!=0 ||
                native.index_type!=vk_index_type(d.index_type) ||
                cmd.a[4]!=uint64_t(d.index_count)*index_size(d.index_type))
                fatal("Native engine indexed Vulkan arguments differ from SDK contract");
        }
        static std::atomic<uint64_t> compared{0};
        const uint64_t checked=compared.fetch_add(1,std::memory_order_relaxed)+1;
        if (checked==1 || checked%100000==0)
            log("Native engine indexed packets verified: %llu",(unsigned long long)checked);
    }
    record(command,cmd);
}
}
extern "C" void catherine_engine_indexed_emit(uint64_t command,
    const CatherineEngineIndexedDraw* draw, int parity) {
    nx::nvn::emit_engine_indexed_typed(command,*draw,parity!=0);
}


namespace nx::nvn {
void emit_engine_array_typed(uint64_t command,const CatherineEngineArrayDraw& d,bool parity) {
    ++g_recorded_draws;
    Cmd cmd{Cmd::DrawArrays};cmd.n=d.primitive;
    cmd.i[0]=d.first_vertex;cmd.i[1]=d.vertex_count;cmd.i[2]=0;cmd.i[3]=1;
    if(engine_vk::mode()&&cmd.n!=7&&cmd.i[0]>=0&&cmd.i[1]>0)
        engine_vk::encode_arrays(cmd);
    if(parity) {
        if(cmd.n!=d.primitive||cmd.i[0]!=d.first_vertex||cmd.i[1]!=d.vertex_count||cmd.i[2]!=0||cmd.i[3]!=1)
            fatal("Native engine array producer differs from SDK draw contract");
        if(engine_vk::arrays_present(cmd)) {
            const auto a=engine_vk::decode_arrays(cmd);
            if(a.vertexCount!=uint32_t(d.vertex_count)||a.instanceCount!=1||
               a.firstVertex!=uint32_t(d.first_vertex)||a.firstInstance!=0)
                fatal("Native engine array Vulkan arguments differ from SDK contract");
        }
        static std::atomic<uint64_t> compared{0};
        const auto checked=compared.fetch_add(1,std::memory_order_relaxed)+1;
        if(checked==1||checked%10000==0)
            log("Native engine array packets verified: %llu",(unsigned long long)checked);
    }
    record(command,cmd);
}
}
extern "C" void catherine_engine_array_emit(uint64_t command,const CatherineEngineArrayDraw* draw,int parity) {
    nx::nvn::emit_engine_array_typed(command,*draw,parity!=0);
}


namespace nx::nvn {
void emit_compute_range_typed(uint64_t command,bool storage,const CatherineComputeRange& r,bool parity) {
    Cmd cmd{storage?Cmd::BindStorageBuffer:Cmd::BindUniformBuffer};
    cmd.i[0]=int32_t(r.stage);cmd.i[1]=int32_t(r.slot);cmd.a[0]=r.address;cmd.a[1]=r.size;
    if(parity&&(uint32_t(cmd.i[0])!=r.stage||uint32_t(cmd.i[1])!=r.slot||cmd.a[0]!=r.address||cmd.a[1]!=r.size))
        fatal("Typed engine compute range differs from SDK contract");
    if(!storage&&cmd.i[0]==0&&cmd.i[1]==0) {
        const bool owned=catherine_c02_c3_record_owned(cmd.a[0],cmd.a[1])!=0;
        if(owned)cmd.mask=c02_uniform_provenance::command_magic;
        c02_uniform_provenance::record(owned);
    }
    catherine::root9::RigidPacket packet9;
    Root15CommandPacket packet15;
    Block20CommandPacket packet;
    if(!storage&&root9_capture_uniform(command,cmd,packet9)){cmd.mask=root9_uniform_magic;record(command,cmd,&packet9,sizeof packet9);}
    else if(storage&&root15_capture_command(cmd,packet15)){cmd.mask=root15_command_magic;record(command,cmd,&packet15,sizeof packet15);}
    else if(storage&&block20_capture_command(cmd,packet)){cmd.mask=block20_command_magic;record(command,cmd,&packet,sizeof packet);}
    else record(command,cmd);
}
void emit_compute_dispatch_typed(uint64_t command,const CatherineComputeDispatchSize& d,bool parity) {
    Cmd cmd{Cmd::DispatchCompute};cmd.i[0]=int32_t(d.x);cmd.i[1]=int32_t(d.y);cmd.i[2]=int32_t(d.z);
    if(engine_vk::mode()&&cmd.i[0]>0&&cmd.i[1]>0&&cmd.i[2]>0)engine_vk::encode_dispatch(cmd);
    if(parity){
        if(uint32_t(cmd.i[0])!=d.x||uint32_t(cmd.i[1])!=d.y||uint32_t(cmd.i[2])!=d.z)
            fatal("Typed engine compute dimensions differ from SDK contract");
        if(engine_vk::dispatch_present(cmd)){
            const auto p=engine_vk::decode_dispatch(cmd);
            if(p.x!=d.x||p.y!=d.y||p.z!=d.z)fatal("Typed engine Vulkan dispatch differs from SDK contract");
        }
        static std::atomic<uint64_t> compared{0};const auto checked=compared.fetch_add(1,std::memory_order_relaxed)+1;
        if(checked==1||checked%10000==0)log("Native engine compute packets verified: %llu",(unsigned long long)checked);
    }
    record(command,cmd);
}
}
extern "C" void catherine_compute_emit_program(uint64_t command,uint64_t program,uint32_t stages){nx::nvn::bind_program_typed(command,program,stages);}
extern "C" void catherine_compute_emit_range(uint64_t command,int storage,const CatherineComputeRange* range,int parity){nx::nvn::emit_compute_range_typed(command,storage!=0,*range,parity!=0);}
extern "C" void catherine_compute_emit_dispatch(uint64_t command,const CatherineComputeDispatchSize* size,int parity){nx::nvn::emit_compute_dispatch_typed(command,*size,parity!=0);}


extern "C" void catherine_engine_emit_vertex_range(uint64_t command,const CatherineEngineVertexRange* range,int parity){
    using namespace nx::nvn;
    Cmd cmd{Cmd::BindVertexBuffer};cmd.i[0]=int32_t(range->binding);cmd.a[0]=range->address;cmd.a[1]=range->size;
    if(parity){
        if(uint32_t(cmd.i[0])!=range->binding||cmd.a[0]!=range->address||cmd.a[1]!=range->size)
            nx::fatal("Typed engine vertex range differs from SDK contract");
        static std::atomic<uint64_t> compared{0};const auto checked=compared.fetch_add(1,std::memory_order_relaxed)+1;
        if(checked==1||checked%100000==0)nx::log("Native engine geometry packets verified: %llu",(unsigned long long)checked);
    }
    record(command,cmd);
}


extern "C" void catherine_engine_emit_raster(uint64_t command,uint32_t kind,const CatherineEngineRasterValues* values,int parity){
    using namespace nx::nvn;Cmd cmd{kind==CatherineDepthScalar?Cmd::SetDepthRange:kind==CatherineViewports?Cmd::SetViewport:Cmd::SetScissor};
    if(kind==CatherineDepthScalar){std::memcpy(cmd.f,values->bits,8);}
    else if(kind==CatherineViewports){
        for(unsigned k=0;k<4;++k){float value;std::memcpy(&value,&values->bits[k],4);cmd.i[k]=int32_t(std::lround(value));}
    }else{std::memcpy(cmd.i,values->bits,16);}
    if(parity){
        if(kind==CatherineDepthScalar&&std::memcmp(cmd.f,values->bits,8))nx::fatal("Typed viewport depth range differs from SDK contract");
        if(kind==CatherineScissors&&std::memcmp(cmd.i,values->bits,16))nx::fatal("Typed viewport scissor differs from SDK contract");
        static std::atomic<uint64_t> compared{0};const auto checked=compared.fetch_add(1,std::memory_order_relaxed)+1;
        if(checked==1||checked%10000==0)nx::log("Native engine viewport values verified: %llu",(unsigned long long)checked);
    }
    record(command,cmd);
}


extern "C" void catherine_engine_emit_polygon(uint64_t command,uint32_t kind,const CatherineEnginePolygonValues*values,int parity){
    using namespace nx::nvn;
    Cmd cmd{kind==CatherinePolygonState?Cmd::BindPolygonState:kind==CatherinePolygonBias?Cmd::SetPolygonOffsetClamp:Cmd::BindMultisampleState};
    PolygonInfo polygon{};
    if(kind==CatherinePolygonState){
        polygon.cull=values->words[0];polygon.front=values->words[1];polygon.mode=values->words[2];polygon.offset_enables=values->words[3];
    }else if(kind==CatherinePolygonBias){std::memcpy(cmd.f,values->words,12);}
    else cmd.n=values->words[0];
    if(parity){
        if(kind==CatherinePolygonState&&(polygon.cull!=values->words[0]||polygon.front!=values->words[1]||polygon.mode!=values->words[2]||polygon.offset_enables!=values->words[3]))
            nx::fatal("Typed polygon state differs from SDK getter contract");
        if(kind==CatherinePolygonBias&&std::memcmp(cmd.f,values->words,12))nx::fatal("Typed polygon bias differs from SDK contract");
        static std::atomic<uint64_t> compared{0};const auto checked=compared.fetch_add(1,std::memory_order_relaxed)+1;
        if(checked==1||checked%10000==0)nx::log("Native engine polygon values verified: %llu",(unsigned long long)checked);
    }
    if(kind==CatherinePolygonState)record(command,cmd,&polygon,sizeof polygon);else record(command,cmd);
}


extern "C" void catherine_engine_emit_fixed(uint64_t command,uint32_t kind,const CatherineEngineFixedValues*v,int parity){
 using namespace nx::nvn;const auto*w=v->words;
 static const Cmd::Op ops[]={Cmd::BindBlendState,Cmd::BindColorState,Cmd::BindChannelMaskState,Cmd::SetBlendColor,Cmd::BindDepthStencilState,Cmd::SetStencilMask,Cmd::SetStencilValueMask,Cmd::SetStencilRef,Cmd::SetDepthBounds};
 if(kind>=9)nx::fatal("Unsupported typed fixed-state operation");Cmd cmd{ops[kind]};
 BlendTarget blend{};ColorStateInfo color{};ChannelMaskInfo channels{};DepthStencilInfo depth{};const void*blob=nullptr;size_t bytes=0;
 if(kind==0){blend={w[0],w[1],w[2],w[3],w[4],w[5],w[6]};blob=&blend;bytes=sizeof blend;}
 else if(kind==1){for(unsigned k=0;k<8;++k)color.blend_enable[k]=uint8_t(w[k]);color.alpha_test=w[8];color.logic_op=w[9];blob=&color;bytes=sizeof color;}
 else if(kind==2){for(unsigned k=0;k<8;++k)channels.mask[k]=uint8_t(w[k]);blob=&channels;bytes=sizeof channels;}
 else if(kind==3)std::memcpy(cmd.f,w,16);
 else if(kind==4){depth.depth_test=uint8_t(w[0]);depth.depth_write=uint8_t(w[1]);depth.stencil_test=uint8_t(w[2]);depth.depth_func=w[3];for(unsigned f=0;f<2;++f){depth.stencil_func[f]=w[4+f];depth.stencil_fail[f]=w[6+f];depth.depth_fail[f]=w[8+f];depth.depth_pass[f]=w[10+f];}blob=&depth;bytes=sizeof depth;}
 else if(kind<8){cmd.mask=w[0]&3;cmd.n=w[1]&255;}
 else {cmd.n=w[0]&1;std::memcpy(cmd.f,w+1,8);}
 if(parity){static std::atomic<uint64_t>compared{0};const auto checked=compared.fetch_add(1,std::memory_order_relaxed)+1;if(checked==1||checked%10000==0)nx::log("Native engine blend/depth values verified: %llu",(unsigned long long)checked);}
 record(command,cmd,blob,bytes);
}


extern "C" void catherine_engine_emit_polygon_option(uint64_t command,uint32_t kind,uint32_t value,int diagnostic){
 using namespace nx::nvn;
 if(kind>=4)nx::fatal("Unsupported typed polygon option");
 if(kind==CatherinePolygonDiscard||kind==CatherinePolygonDepthClamp){
  Cmd cmd{kind==CatherinePolygonDiscard?Cmd::SetRasterizerDiscard:Cmd::SetDepthClamp};cmd.n=value&1;record(command,cmd);
 }
 /* Sample mask/conservative raster enable were unimplemented trace stubs: retain their no-command behavior. */
 if(diagnostic){static std::atomic<uint64_t>requests[4];const auto n=requests[kind].fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%10000==0)nx::log("Native engine polygon option %u requests: %llu",kind,(unsigned long long)n);}
}


extern "C" void catherine_engine_emit_attributes(uint64_t command,uint32_t count,const CatherineEngineAttrib*values,int diagnostic){
 using namespace nx::nvn;if(count>32)nx::fatal("Typed vertex attribute count out of bounds");VertexAttrib out[32]{};
 for(uint32_t k=0;k<count;++k){out[k].format=values[k].format;out[k].offset=values[k].offset;out[k].stream=values[k].stream;}
 Cmd cmd{Cmd::BindVertexAttribs};cmd.n=count;record(command,cmd,out,count*sizeof(VertexAttrib));
 if(diagnostic){static std::atomic<uint64_t>requests{0};const auto n=requests.fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%10000==0)nx::log("Native shared vertex attribute requests: %llu",(unsigned long long)n);}
}
extern "C" void catherine_engine_emit_streams(uint64_t command,uint32_t count,const CatherineEngineStream*values,int diagnostic){
 using namespace nx::nvn;if(count>32)nx::fatal("Typed vertex stream count out of bounds");VertexStream out[32]{};
 for(uint32_t k=0;k<count;++k){out[k].stride=values[k].stride;out[k].divisor=values[k].divisor;}
 Cmd cmd{Cmd::BindVertexStreams};cmd.n=count;record(command,cmd,out,count*sizeof(VertexStream));
 if(diagnostic){static std::atomic<uint64_t>requests{0};const auto n=requests.fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%10000==0)nx::log("Native shared vertex stream requests: %llu",(unsigned long long)n);}
}


extern "C" void catherine_engine_emit_texture_binding(uint64_t command,uint32_t stage,uint32_t slot,uint64_t handle,int diagnostic){
 using namespace nx::nvn;Cmd cmd{Cmd::BindTexture};cmd.i[0]=int32_t(stage);cmd.i[1]=int32_t(slot);cmd.a[0]=handle;record(command,cmd);
 if(diagnostic){static std::atomic<uint64_t>requests{0};const auto n=requests.fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%100000==0)nx::log("Native shared texture binding requests: %llu",(unsigned long long)n);}
}

extern "C" void catherine_engine_emit_pool(uint64_t command,uint32_t kind,uint64_t pool,int diagnostic){
    using namespace nx::nvn;
    if(kind!=1&&kind!=2)return;
    Cmd cmd{kind==1?Cmd::SetTexturePool:Cmd::SetSamplerPool};
    cmd.a[0]=pool; // key only; marked pool/version/slot resolution stays on replay
    if(diagnostic){
        static std::atomic<uint64_t> requests[2];
        const uint64_t n=requests[kind-1].fetch_add(1,std::memory_order_relaxed)+1;
        if(n==1||n%100000==0)nx::log("Native shared %s pool bindings: %llu",kind==1?"texture":"sampler",(unsigned long long)n);
    }
    record(command,cmd);
}
