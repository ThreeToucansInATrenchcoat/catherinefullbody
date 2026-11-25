
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "cpu.h"
#include "engine_native_texture.h"
void f_700032c394_original(Cpu*);void f_70003302f8(Cpu*);
static _Thread_local int texture_diagnostic;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void texture_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void texture_handle_leaf(Cpu*c){
 if(!catherine_texture_callback(0,c->pc)){rt_dispatch(c);return;}
 c->x[0]=catherine_engine_texture_handle(c->x[0],(uint32_t)c->x[1],(uint32_t)c->x[2]);
 if(texture_diagnostic){static _Atomic uint64_t requests;const uint64_t n=atomic_fetch_add_explicit(&requests,1,memory_order_relaxed)+1;if(n==1||n%100000==0)fprintf(stderr,"Native shared texture handle requests: %llu\n",(unsigned long long)n);}
 texture_bridge(c);
}
static void texture_binding_leaf(Cpu*c){
 if(!catherine_texture_callback(1,c->pc)){rt_dispatch(c);return;}
 catherine_engine_emit_texture_binding(c->x[0],(uint32_t)c->x[1],(uint32_t)c->x[2],c->x[3],texture_diagnostic);texture_bridge(c);
}
static int texture_preflight(void){for(unsigned kind=0;kind<2;++kind){const uint64_t cell=LD64(0x7000ccf368ull+8*kind);if(!cell||!catherine_texture_callback(kind,LD64(cell)))return 0;}return 1;}
static void native_texture_body(Cpu *c) {
    FN_ENTER(0x700032c394ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], sp_ = c->sp;
    { uint64_t ea = sp_ - 48; ST64(ea, c->x[22]); ST64(ea + 8, x21); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x20ull);
    x20 = (uint64_t)(0ull | x0);
    { uint64_t ea = x20 + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    x8 = (uint64_t)(0x7000ccf000ull);
    x19 = (uint32_t)(0u | (uint32_t)c->x[2]);
    x21 = (uint32_t)(0u | (uint32_t)c->x[1]);
    { uint64_t ea = c->x[3] + 0x0ull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    { uint64_t ea = c->x[4] + 0x0ull; uint64_t v = LD32(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x368ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; c->x[30] = 0x700032c3d0ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; texture_handle_leaf(c); x0 = c->x[0]; x8 = c->x[8];
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x20 = (uint64_t)(v); }
    c->x[22] = (uint64_t)(0ull | x0);
    x0 = (uint32_t)(0u | (uint32_t)x19);
    c->x[30] = 0x700032c3e0ull; c->x[0] = x0; c->x[20] = x20; f_70003302f8(c); x0 = c->x[0]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x370ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | (uint32_t)x0);
    x0 = (uint64_t)(0ull | x20);
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x21);
    c->x[3] = (uint64_t)(0ull | c->x[22]);
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 48; c->x[22] = (uint64_t)(a); x21 = (uint64_t)(b); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[4]; __attribute__((musttail)) return texture_binding_leaf(c);
}


void f_700032c394(Cpu*c){const int mode=front_end_mode();if(!mode||!texture_preflight()){f_700032c394_original(c);return;}const int previous=texture_diagnostic;texture_diagnostic=mode==2;native_texture_body(c);texture_diagnostic=previous;}
