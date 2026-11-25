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
    nx::CPU cpu{};
    to_backend(*guest,cpu);
    function(cpu);
    to_guest(cpu,*guest);
    active_guest=previous;
}
