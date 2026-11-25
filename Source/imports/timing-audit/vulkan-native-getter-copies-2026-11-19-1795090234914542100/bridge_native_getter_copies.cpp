#include <cstdint>
#include <cstring>
#include <nx/runtime.h>
#include "graphics.h"
struct alignas(16) GuestCpu {
    uint64_t x[32], sp, pc, nzcv, tpidr, tpidrro, excl_addr, excl_val;
    void *thread;
    uint64_t fpcr, fpsr, excl_val_hi;
    uint8_t padding[512-344];
    alignas(16) uint8_t v[32][16];
};
static_assert(offsetof(GuestCpu,v)==512);
static thread_local GuestCpu *active_guest;
static void to_backend(const GuestCpu& g,nx::CPU& n){
    memcpy(n.x,g.x,sizeof g.x);n.x[31]=g.sp;memcpy(n.v,g.v,sizeof g.v);
    n.nf=(g.nzcv>>31)&1;n.zf=(g.nzcv>>30)&1;n.cf=(g.nzcv>>29)&1;n.vf=(g.nzcv>>28)&1;
    n.tpidr=g.tpidr;n.tpidrro=g.tpidrro;n.fpcr=g.fpcr;n.fpsr=g.fpsr;
    n.excl_addr=g.excl_addr;n.excl_val=g.excl_val;n.excl_val_hi=g.excl_val_hi;
}
static void to_guest(const nx::CPU& n,GuestCpu& g){
    memcpy(g.x,n.x,sizeof g.x);g.sp=n.x[31];memcpy(g.v,n.v,sizeof g.v);
    g.nzcv=((uint64_t)n.nf<<31)|((uint64_t)n.zf<<30)|((uint64_t)n.cf<<29)|((uint64_t)n.vf<<28);
    g.tpidr=n.tpidr;g.tpidrro=n.tpidrro;g.fpcr=n.fpcr;g.fpsr=n.fpsr;
    g.excl_addr=n.excl_addr;g.excl_val=n.excl_val;g.excl_val_hi=n.excl_val_hi;
}
namespace nx {
void call(CPU& cpu,uint64_t target){
    if(!active_guest)fatal("Graphics guest callback lacks CPU context");
    to_guest(cpu,*active_guest);
    guest_call_original(active_guest,target);
    to_backend(*active_guest,cpu);
}
}
extern "C" void graphics_run(void *context, nx::HleFn function){
    GuestCpu *guest=static_cast<GuestCpu *>(context);
    GuestCpu *previous=active_guest;
    active_guest=guest;
    nx::CPU cpu;  // bridge94: to_backend writes every field
    to_backend(*guest,cpu);
    function(cpu);
    to_guest(cpu,*guest);
    active_guest=previous;
}

namespace nx::nvn {
bool record_engine_indexed(void (*recording)(const uint64_t*), const uint64_t* registers);
}

extern "C" void graphics_record_native(void* context, void (*recording)(const uint64_t*)) {
    auto& guest = *static_cast<GuestCpu*>(context);
    // The recompiled engine's render-packet draw calls the indexed wrapper
    // with this guest return address. Other call sites stay on their own path.
    constexpr uint64_t indexed_render_packet_return = 0x70002fc204ull;
    if (guest.x[30] != indexed_render_packet_return ||
        !nx::nvn::record_engine_indexed(recording, guest.x))
        recording(guest.x);
    // The generic bridge also normalizes the stack alias and flags.
    guest.x[31] = guest.sp;
    guest.nzcv &= 0xf0000000ull;
}

#include "native_texture_view_bridge.inc"

#include "native_texture_builder_bridge.inc"

#include "native_texture_builder_outputs_bridge.inc"

#include "native_sampler_builder_bridge.inc"

#include "native_window_builder_bridge.inc"

#include "native_packed_state_bridge.inc"

// graphics_run synchronizes the complete backend CPU at the HLE boundary.
// These four scalar getters change only saved general registers and output
// memory. Preserve ordered writes and PC; avoid dead intermediate context copies.
// Replacement callbacks still synchronize through the ordinary guest_call path.
extern "C" int catherine_native_view_prefix_step(nx::CPU* cpu,unsigned kind,uint64_t address,uint64_t view,uint64_t tmp,int compare,uint64_t* result){
 using namespace catherine::native_view;
 if(!cpu||!active_guest||!result||kind>=4||address!=addresses[kind]||!view||!tmp||view>UINT64_MAX-28||tmp>UINT64_MAX-16||
    overlaps(view,28,uint64_t(cpu),sizeof *cpu)||overlaps(tmp,16,uint64_t(cpu),sizeof *cpu)||
    overlaps(view,28,uint64_t(active_guest),sizeof *active_guest)||overlaps(tmp,16,uint64_t(active_guest),sizeof *active_guest))return 0;
 struct ScalarRegisters {uint64_t x[9]{};} local;local.x[0]=view;local.x[1]=tmp;local.x[2]=tmp+4;local.x[3]=tmp+8;local.x[4]=tmp+12;
 if(compare){ViewGetterShadow shadow(view,tmp);GuestCpu oracle{};to_guest(*cpu,oracle);view_arguments(oracle,kind,shadow.view,shadow.scratch);
  static void(*const original[])(void*)={f_7005427ca0,f_7005427cc0,f_7005427d00,f_7005427ce0};original[kind](&oracle);execute(local,kind);
  if(local.x[0]!=oracle.x[0]||!shadow.equal(tmp))nx::fatal("Compound texture view prefix differs: kind %u",kind);
 }else execute(local,kind);
 *result=local.x[0];active_guest->pc=address;if(compare){static uint64_t checks=0;if(++checks==1||checks%100000==0)nx::log("Compound texture view prefix: %llu compared",(unsigned long long)checks);}return 1;
}
