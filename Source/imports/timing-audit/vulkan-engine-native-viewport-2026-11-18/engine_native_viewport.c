
#include <stdlib.h>
#include "cpu.h"
#include "simd.h"
#include "engine_native_viewport.h"
void f_700032ba60_original(Cpu*);
static _Thread_local int viewport_parity;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void viewport_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void viewport_depth_scalar_leaf(Cpu*c){
    if(!catherine_viewport_callback(CatherineDepthScalar,c->pc)){rt_dispatch(c);return;}
    const CatherineEngineRasterValues values={{VS32(0),VS32(1),0,0}};
    catherine_engine_emit_raster(c->x[0],CatherineDepthScalar,&values,viewport_parity);viewport_bridge(c);
}
static void viewport_viewports_leaf(Cpu*c){
    if(!catherine_viewport_callback(CatherineViewports,c->pc)){rt_dispatch(c);return;}
    if(c->x[1]==0&&(int32_t)c->x[2]>=1&&c->x[3]){
        CatherineEngineRasterValues values;
        for(unsigned k=0;k<4;++k)values.bits[k]=LD32(c->x[3]+4*k);
        catherine_engine_emit_raster(c->x[0],CatherineViewports,&values,viewport_parity);
    }
    viewport_bridge(c);
}
static void viewport_scissors_leaf(Cpu*c){
    if(!catherine_viewport_callback(CatherineScissors,c->pc)){rt_dispatch(c);return;}
    if(c->x[1]==0&&(int32_t)c->x[2]>=1&&c->x[3]){
        CatherineEngineRasterValues values;
        for(unsigned k=0;k<4;++k)values.bits[k]=LD32(c->x[3]+4*k);
        catherine_engine_emit_raster(c->x[0],CatherineScissors,&values,viewport_parity);
    }
    viewport_bridge(c);
}
static void viewport_depth_ranges_leaf(Cpu*c){
    if(!catherine_viewport_callback(CatherineDepthRanges,c->pc)){rt_dispatch(c);return;}
    if(c->x[1]==0&&(int32_t)c->x[2]>=1&&c->x[3]){
        const CatherineEngineRasterValues values={{LD32(c->x[3]),LD32(c->x[3]+4),0,0}};
        catherine_engine_emit_raster(c->x[0],CatherineDepthScalar,&values,viewport_parity);
    }
    viewport_bridge(c);
}
static int viewport_preflight(uint64_t object){
    static const uint32_t slots[]={0x2a0,0x2a8,0x2b0,0x2b8};
    const unsigned required=(int32_t)LD32(object+8)>=2?4:3;
    for(unsigned k=0;k<required;++k){
        const uint64_t slot=LD64(0x7000ccf000ull+slots[k]);
        if(!slot||!catherine_viewport_callback(k,LD64(slot)))return 0;
    }
    return 1;
}
static void native_viewport_body(Cpu *c) {
    FN_ENTER(0x700032ba60ull);
    Flags F = {0};
    uint64_t x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], x23 = c->x[23], sp_ = c->sp;
    { uint64_t ea = sp_ - 64; ST64(ea, c->x[24]); ST64(ea + 8, x23); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, c->x[22]); ST64(ea + 8, x21); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    x20 = (uint64_t)(0ull | c->x[1]);
    { uint64_t ea = x20 + 32; u128 a = LD64(ea), b = LD64(ea + 8); c->v[0] = a; c->v[1] = b; }
    x19 = (uint64_t)(0ull | c->x[0]);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x2a0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    FP_SETS(1, simd_d2s(c, VSF64(1)));
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    FP_SETS(0, simd_d2s(c, VSF64(0)));
    c->pc = x8; c->x[30] = 0x700032ba9cull; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; viewport_depth_scalar_leaf(c); x8 = c->x[8];
    c->x[24] = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = c->x[24] + 0x2a8ull; uint64_t v = LD64(ea); c->x[24] = (uint64_t)(v); }
    { uint64_t ea = c->x[24] + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[3] = (uint64_t)(x20 + 0x10ull);
    c->x[2] = (uint32_t)(0u | 0x1ull);
    c->x[1] = (uint32_t)(0u | 0u);
    c->pc = x8; c->x[30] = 0x700032babcull; c->x[8] = x8; viewport_viewports_leaf(c); x8 = c->x[8];
    x23 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x23 + 0x2b0ull; uint64_t v = LD64(ea); x23 = (uint64_t)(v); }
    { uint64_t ea = x23 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[3] = (uint64_t)(x20 + 0x30ull);
    c->x[2] = (uint32_t)(0u | 0x1ull);
    c->x[1] = (uint32_t)(0u | 0u);
    c->pc = x8; c->x[30] = 0x700032badcull; c->x[8] = x8; c->x[23] = x23; viewport_scissors_leaf(c); x8 = c->x[8];
    { uint64_t ea = x20 + 0x8ull; uint64_t v = LD32(ea); x8 = (uint64_t)(v); }
    (void)(fsub32(&F, (uint32_t)x8, 0x2ull));
    if (fc_sub32(&F, CC_LT)) { goto L_700032bb54; }
    { uint64_t ea = x20 + 0x0ull; uint64_t v = LD64(ea); c->x[3] = (uint64_t)(v); }
    x21 = (uint32_t)((uint32_t)x8 - 0x1ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = c->x[24] + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x1ull);
    c->x[2] = (uint32_t)(0u | (uint32_t)x21);
    x20 = (uint64_t)(c->x[3] + ((uint64_t)(int64_t)(int32_t)x21 << 4));
    c->x[22] = (uint64_t)(x20 + ((uint64_t)(int64_t)(int32_t)x21 << 3));
    c->pc = x8; c->x[30] = 0x700032bb0cull; c->x[8] = x8; c->x[20] = x20; c->x[21] = x21; viewport_viewports_leaf(c); x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x2b8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x1ull);
    c->x[2] = (uint32_t)(0u | (uint32_t)x21);
    c->x[3] = (uint64_t)(0ull | x20);
    c->pc = x8; c->x[30] = 0x700032bb2cull; c->x[8] = x8; viewport_depth_ranges_leaf(c); x8 = c->x[8];
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x23 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x21);
    c->x[3] = (uint64_t)(0ull | c->x[22]);
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    c->x[1] = (uint32_t)(0u | 0x1ull);
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); x21 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 64; c->x[24] = (uint64_t)(a); x23 = (uint64_t)(b); }
    c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[23] = x23; c->sp = sp_; c->pc = c->x[4]; __attribute__((musttail)) return viewport_scissors_leaf(c);
L_700032bb54:;
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); x21 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 64; c->x[24] = (uint64_t)(a); x23 = (uint64_t)(b); }
    c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[23] = x23; c->sp = sp_; return;
}


/* Original symbol remains the caller-visible native entry. Dirty lookup/cache
 * f311210 and every direct guest caller are unchanged. */
void f_700032ba60(Cpu*c){
    const int mode=front_end_mode();
    if(!mode||!viewport_preflight(c->x[1])){f_700032ba60_original(c);return;}
    const int previous=viewport_parity;viewport_parity=mode==2;native_viewport_body(c);viewport_parity=previous;
}
