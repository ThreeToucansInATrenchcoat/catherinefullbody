#include "engine_native_polygon_options.h"

#include <stdlib.h>
#include "cpu.h"
#include "simd.h"
#include "engine_native_polygon.h"
void f_700032b644_original(Cpu*);
static _Thread_local int polygon_parity;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void polygon_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void polygon_state_leaf(Cpu*c){
    if(!catherine_polygon_callback(CatherinePolygonState,c->pc)){rt_dispatch(c);return;}
    /* Proven against the actual SDK getters 5426660/70/80/90. */
    const uint32_t packed=LD32(c->x[1]);
    const CatherineEnginePolygonValues values={{packed&3,(packed>>2)&1,(packed>>3)&3,(packed>>15)&15}};
    catherine_engine_emit_polygon(c->x[0],CatherinePolygonState,&values,polygon_parity);polygon_bridge(c);
}
static void polygon_bias_leaf(Cpu*c){
    if(!catherine_polygon_callback(CatherinePolygonBias,c->pc)){rt_dispatch(c);return;}
    const CatherineEnginePolygonValues values={{VS32(0),VS32(1),VS32(2),0}};
    catherine_engine_emit_polygon(c->x[0],CatherinePolygonBias,&values,polygon_parity);polygon_bridge(c);
}
static void polygon_multisample_leaf(Cpu*c){
    if(!catherine_polygon_callback(CatherinePolygonMultisample,c->pc)){rt_dispatch(c);return;}
    /* Actual SDK getter5426760 reads alpha-to-coverage from bit1. */
    const CatherineEnginePolygonValues values={{c->x[1]?(LD32(c->x[1])>>1)&1:0,0,0,0}};
    catherine_engine_emit_polygon(c->x[0],CatherinePolygonMultisample,&values,polygon_parity);polygon_bridge(c);
}

static void polygon_option_leaf(Cpu*c,uint32_t kind){
 const int verified=catherine_polygon_option_callback(kind,c->pc);
 if(!verified){rt_dispatch(c);return;}kind=(uint32_t)(verified-1);
 catherine_engine_emit_polygon_option(c->x[0],kind,(uint32_t)c->x[1],polygon_parity);
 /* The selected sample-mask/conservative trace stub returns x0=0 and records no GPU command. */
 if(kind==CatherinePolygonSampleMask||kind==CatherinePolygonConservative)c->x[0]=0;
 polygon_bridge(c);
}
static void polygon_discard_leaf(Cpu*c){polygon_option_leaf(c,CatherinePolygonDiscard);}
static void polygon_sample_mask_leaf(Cpu*c){polygon_option_leaf(c,CatherinePolygonSampleMask);}
static void polygon_conservative_leaf(Cpu*c){polygon_option_leaf(c,CatherinePolygonConservative);}
static void polygon_depth_clamp_leaf(Cpu*c){polygon_option_leaf(c,CatherinePolygonDepthClamp);}
static int polygon_preflight(void){
    static const uint32_t slots[]={0x1f8,0x200,0x208,0x210,0x218,0x220,0x228};
    for(unsigned k=0;k<7;++k){const uint64_t slot=LD64(0x7000ccf000ull+slots[k]);if(!slot||!(k<3?catherine_polygon_callback(k,LD64(slot)):catherine_polygon_option_callback(k-3,LD64(slot))))return 0;}
    return 1;
}
static void native_polygon_body(Cpu *c) {
    FN_ENTER(0x700032b644ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x30 = c->x[30], sp_ = c->sp;
    { uint64_t ea = sp_ - 48; ST64(ea, c->x[21]); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[29]); ST64(ea + 8, x30); }
    c->x[29] = (uint64_t)(sp_ + 0x20ull);
    x20 = (uint64_t)(0ull | x0);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x1f8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x19 = (uint64_t)(0ull | c->x[1]);
    c->pc = x8; x30 = 0x700032b670ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[30] = x30; c->sp = sp_; polygon_state_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x19 + 0x20ull; c->v[1] = LD32(ea); }
    { uint64_t ea = x19 + 36; u128 a = LD32(ea), b = LD32(ea + 4); c->v[2] = a; c->v[0] = b; }
    { uint64_t ea = x8 + 0x200ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b68cull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; polygon_bias_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x208ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(x19 + 0x4ull);
    c->pc = x8; x30 = 0x700032b6a4ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; polygon_multisample_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0x2dull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    if (((x8 >> 0) & 1) == 0) { goto L_700032b6c8; }
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x19 + 0x1cull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x210ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b6c4ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; polygon_sample_mask_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0x2dull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
L_700032b6c8:;
    c->x[21] = (uint32_t)(0u | 0x1ull);
    c->x[1] = (uint32_t)((uint32_t)c->x[21] & (uint32_t)~((uint32_t)x8 >> 1));
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x218ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b6e4ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; polygon_depth_clamp_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0x2dull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)((uint32_t)c->x[21] & (uint32_t)~((uint32_t)x8 >> 2));
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x220ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b700ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; polygon_discard_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x20 + 0x1ull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    if (((x8 >> 1) & 1) != 0) { goto L_700032b718; }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 48; c->x[21] = (uint64_t)(v); }
    c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[30] = x30; c->sp = sp_; return;
L_700032b718:;
    { uint64_t ea = x19 + 0x2dull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(ROR32((uint32_t)x8, 3) & 0x1ull);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x228ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 48; c->x[21] = (uint64_t)(v); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[30] = x30; c->sp = sp_; c->pc = c->x[2]; __attribute__((musttail)) return polygon_conservative_leaf(c);
}


void f_700032b644(Cpu*c){
    const int mode=front_end_mode();if(!mode||!polygon_preflight()){f_700032b644_original(c);return;}
    const int previous=polygon_parity;polygon_parity=mode==2;native_polygon_body(c);polygon_parity=previous;
}
