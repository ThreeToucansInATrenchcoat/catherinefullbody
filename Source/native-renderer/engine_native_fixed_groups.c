#include <stdint.h>
extern uint64_t catherine_native_record_scope_begin(void);
extern void catherine_native_record_scope_end(uint64_t);
extern void catherine_native_record_scope_abandon(void);

#include <stdlib.h>
#include "cpu.h"
#include "simd.h"
#include "engine_native_fixed_groups.h"
void f_700032b740_original(Cpu*);void f_700032b7e4_original(Cpu*);
static _Thread_local int fixed_parity;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
/* Each invoked leaf still authenticates its live callback immediately before use. */
static int omit_preflight(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_NATIVE_PREFLIGHT");mode=v?atoi(v):1;}return mode!=0;}
static void fixed_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void fixed_blend_leaf(Cpu*c){
 if(!catherine_fixed_callback(CatherineFixedBlend,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const uint32_t a=LD32(c->x[1]),b=LD32(c->x[1]+4);
 const CatherineEngineFixedValues v={{a&7,b&127,(b>>8)&127,(b>>16)&127,(b>>24)&127,(a>>24)&7,(a>>28)&7,0,0,0,0,0}};
 catherine_engine_emit_fixed(c->x[0],CatherineFixedBlend,&v,fixed_parity);fixed_bridge(c);
}
static void fixed_color_leaf(Cpu*c){
 if(!catherine_fixed_callback(CatherineFixedColor,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const uint32_t packed=LD32(c->x[1]);CatherineEngineFixedValues v={{0}};
 for(unsigned k=0;k<8;++k)v.words[k]=(packed>>(8+k))&1;
 v.words[8]=(packed>>24)&15;v.words[9]=(packed>>16)&255;
 catherine_engine_emit_fixed(c->x[0],CatherineFixedColor,&v,fixed_parity);fixed_bridge(c);
}
static void fixed_channels_leaf(Cpu*c){
 if(!catherine_fixed_callback(CatherineFixedChannels,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const uint32_t packed=LD32(c->x[1]);CatherineEngineFixedValues v={{0}};
 for(unsigned k=0;k<8;++k)v.words[k]=(packed>>(4*k))&15;
 catherine_engine_emit_fixed(c->x[0],CatherineFixedChannels,&v,fixed_parity);fixed_bridge(c);
}
static void fixed_blend_color_leaf(Cpu*c){
 if(!catherine_fixed_callback(CatherineFixedBlendColor,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 CatherineEngineFixedValues v={{0}};if(c->x[1])for(unsigned k=0;k<4;++k)v.words[k]=LD32(c->x[1]+4*k);
 catherine_engine_emit_fixed(c->x[0],CatherineFixedBlendColor,&v,fixed_parity);fixed_bridge(c);
}
static void fixed_depth_leaf(Cpu*c){
 if(!catherine_fixed_callback(CatherineFixedDepth,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const uint32_t a=LD32(c->x[1]),b=LD32(c->x[1]+4);
 /* Actual SDK6560/6580: func,fail,depth-fail,pass nibble ordering differs. */
 const CatherineEngineFixedValues v={{a&1,(a>>1)&1,(a>>2)&1,(a>>4)&15,b&15,(b>>16)&15,
 (b>>4)&15,(b>>20)&15,(b>>12)&15,(b>>28)&15,(b>>8)&15,(b>>24)&15}};
 catherine_engine_emit_fixed(c->x[0],CatherineFixedDepth,&v,fixed_parity);fixed_bridge(c);
}
static void fixed_stencil_leaf(Cpu*c){
 const int verified=catherine_fixed_callback(CatherineFixedStencilAny,c->pc);
 if(!verified){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const CatherineEngineFixedValues v={{(uint32_t)c->x[1]&3,(uint32_t)c->x[2]&255,0,0,0,0,0,0,0,0,0,0}};
 catherine_engine_emit_fixed(c->x[0],(uint32_t)(verified-1),&v,fixed_parity);fixed_bridge(c);
}
static void fixed_bounds_leaf(Cpu*c){
 if(!catherine_fixed_callback(CatherineFixedBounds,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const CatherineEngineFixedValues v={{(uint32_t)c->x[1]&1,VS32(0),VS32(1),0,0,0,0,0,0,0,0,0}};
 catherine_engine_emit_fixed(c->x[0],CatherineFixedBounds,&v,fixed_parity);fixed_bridge(c);
}
static int fixed_preflight(int depth){
 static const uint32_t blend_slots[]={0x230,0x238,0x240,0x248},depth_slots[]={0x250,0x258,0x260,0x268,0x270};
 static const uint32_t blend_kinds[]={0,1,2,3},depth_kinds[]={4,9,9,9,8};
 const unsigned count=depth?5:4;
 for(unsigned k=0;k<count;++k){const uint64_t slot=LD64(0x7000ccf000ull+(depth?depth_slots[k]:blend_slots[k]));
  if(!slot||!catherine_fixed_callback(depth?depth_kinds[k]:blend_kinds[k],LD64(slot)))return 0;}
 return 1;
}
static void native_700032b740(Cpu *c) {
    FN_ENTER(0x700032b740ull);
    uint64_t x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], x22 = c->x[22], sp_ = c->sp;
    { uint64_t ea = sp_ - 64; ST64(ea, c->x[23]); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x22); ST64(ea + 8, x21); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    x19 = (uint64_t)(0ull | c->x[1]);
    x20 = (uint64_t)(0ull | c->x[0]);
    { uint64_t ea = x19 + 0x25ull; uint64_t v = LD8(ea); x22 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    if (x22 == 0) { goto L_700032b790; }
    c->x[23] = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0x0ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    { uint64_t ea = c->x[23] + 0x230ull; uint64_t v = LD64(ea); c->x[23] = (uint64_t)(v); }
L_700032b774:;
    { uint64_t ea = c->x[23] + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(0ull | x21);
    c->pc = x8; c->x[30] = 0x700032b780ull; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[22] = x22; c->sp = sp_; fixed_blend_leaf(c); x8 = c->x[8];
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    x21 = (uint64_t)(x21 + 0x8ull);
    x22 = (uint64_t)(x22 - 0x1ull);
    if (x22 != 0) { goto L_700032b774; }
L_700032b790:;
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x238ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(x19 + 0xcull);
    c->pc = x8; c->x[30] = 0x700032b7a4ull; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[22] = x22; c->sp = sp_; fixed_color_leaf(c); x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x240ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(x19 + 0x8ull);
    c->pc = x8; c->x[30] = 0x700032b7bcull; c->x[8] = x8; fixed_channels_leaf(c); x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x248ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(x19 + 0x10ull);
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x22 = (uint64_t)(a); x21 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 64; c->x[23] = (uint64_t)(v); }
    c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[22] = x22; c->sp = sp_; c->pc = c->x[2]; __attribute__((musttail)) return fixed_blend_color_leaf(c);
}


static void native_700032b7e4(Cpu *c) {
    FN_ENTER(0x700032b7e4ull);
    uint64_t x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], x30 = c->x[30], sp_ = c->sp;
    { uint64_t ea = sp_ - 48; ST64(ea, x21); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[29]); ST64(ea + 8, x30); }
    c->x[29] = (uint64_t)(sp_ + 0x20ull);
    x19 = (uint64_t)(0ull | c->x[0]);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x250ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x20 = (uint64_t)(0ull | c->x[1]);
    c->pc = x8; x30 = 0x700032b810ull; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[30] = x30; c->sp = sp_; fixed_depth_leaf(c); x8 = c->x[8]; x30 = c->x[30];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x8ull; uint64_t v = LD32(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x258ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x3ull);
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b82cull; c->x[8] = x8; c->x[30] = x30; fixed_stencil_leaf(c); x8 = c->x[8]; x30 = c->x[30];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x20 + 0xcull; uint64_t v = LD32(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x260ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x3ull);
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b848ull; c->x[8] = x8; c->x[30] = x30; fixed_stencil_leaf(c); x8 = c->x[8]; x30 = c->x[30];
    x21 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x10ull; uint64_t v = LD32(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = x21 + 0x268ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x2ull);
    { uint64_t ea = x21 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b864ull; c->x[8] = x8; c->x[21] = x21; c->x[30] = x30; fixed_stencil_leaf(c); x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x14ull; uint64_t v = LD32(ea); c->x[2] = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x1ull);
    { uint64_t ea = x21 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; x30 = 0x700032b878ull; c->x[8] = x8; c->x[30] = x30; fixed_stencil_leaf(c); x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x20 + 0x1aull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    if (((x8 >> 3) & 1) != 0) { goto L_700032b8ac; }
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); c->x[0] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x270ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    c->v[0] = 0u;
    c->v[1] = 0x3f800000ull;
    c->x[1] = (uint32_t)(0u | 0u);
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 48; x21 = (uint64_t)(v); }
    c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[30] = x30; c->sp = sp_; c->pc = c->x[2]; __attribute__((musttail)) return fixed_bounds_leaf(c);
L_700032b8ac:;
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 48; x21 = (uint64_t)(v); }
    c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->x[30] = x30; c->sp = sp_; return;
}


void f_700032b740(Cpu*c){const int mode=front_end_mode();if(!mode||(!omit_preflight()&&!fixed_preflight(0))){catherine_native_record_scope_abandon();f_700032b740_original(c);return;}const int previous=fixed_parity;fixed_parity=mode==2;const uint64_t record_scope=catherine_native_record_scope_begin();native_700032b740(c);catherine_native_record_scope_end(record_scope);fixed_parity=previous;}

void f_700032b7e4(Cpu*c){const int mode=front_end_mode();if(!mode||(!omit_preflight()&&!fixed_preflight(1))){catherine_native_record_scope_abandon();f_700032b7e4_original(c);return;}const int previous=fixed_parity;fixed_parity=mode==2;const uint64_t record_scope=catherine_native_record_scope_begin();native_700032b7e4(c);catherine_native_record_scope_end(record_scope);fixed_parity=previous;}
