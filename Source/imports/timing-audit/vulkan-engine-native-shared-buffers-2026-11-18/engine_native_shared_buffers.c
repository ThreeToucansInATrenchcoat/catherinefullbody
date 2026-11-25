
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "cpu.h"
#include "engine_native_compute.h"
#include "engine_native_geometry.h"
void f_700032b2dc_original(Cpu*);void f_700032ba10_original(Cpu*);void f_7000330370(Cpu*);
static _Thread_local int shared_parity;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void shared_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void shared_note(unsigned kind){if(shared_parity){static _Atomic uint64_t counts[2];const uint64_t n=atomic_fetch_add_explicit(&counts[kind],1,memory_order_relaxed)+1;if(n==1||n%100000==0)fprintf(stderr,"Native shared buffer %s requests: %llu\n",kind?"vertex":"address",(unsigned long long)n);}}
static void shared_address_leaf(Cpu*c){
 if(!catherine_compute_callback(CatherineComputeAddress,c->pc)){rt_dispatch(c);return;}
 c->x[0]=catherine_compute_buffer_address(c->x[0]);shared_note(0);shared_bridge(c);
}
static void shared_vertex_leaf(Cpu*c){
 if(!catherine_engine_vertex_callback(c->pc)){rt_dispatch(c);return;}
 const CatherineEngineVertexRange range={c->x[2],c->x[3],(uint32_t)c->x[1]};
 catherine_engine_emit_vertex_range(c->x[0],&range,shared_parity);shared_note(1);shared_bridge(c);
}
static int shared_preflight(unsigned kind){const uint64_t p=LD64(kind?0x7000ccf298ull:0x7000ccf198ull);if(!p)return 0;return kind?catherine_engine_vertex_callback(LD64(p)):catherine_compute_callback(CatherineComputeAddress,LD64(p));}
static void native_700032b2dc(Cpu *c) {
    FN_ENTER(0x700032b2dcull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x29 = c->x[29], x30 = c->x[30], sp_ = c->sp;
    { uint64_t ea = sp_ - 32; ST64(ea, x19); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x29); ST64(ea + 8, x30); }
    x29 = (uint64_t)(sp_ + 0x10ull);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x0 + 0x8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x198ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x19 = (uint64_t)(0ull | c->x[1]);
    c->pc = x8; x30 = 0x700032b300ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[29] = x29; c->x[30] = x30; c->sp = sp_; shared_address_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0; ST64(ea, x0); ST64(ea + 8, 0ull); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x29 = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 32; x19 = (uint64_t)(v); }
    c->x[19] = x19; c->x[29] = x29; c->x[30] = x30; c->sp = sp_; return;
}


static void native_700032ba10(Cpu *c) {
    FN_ENTER(0x700032ba10ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], sp_ = c->sp;
    { uint64_t ea = sp_ - 48; ST64(ea, x21); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x20ull);
    x20 = (uint32_t)(0u | (uint32_t)c->x[1]);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    x19 = (uint64_t)(0ull | c->x[4]);
    { uint64_t ea = c->x[2] + 0; uint64_t a = LD64(ea), b = LD64(ea + 8); x0 = (uint64_t)(a); c->x[1] = (uint64_t)(b); }
    c->x[30] = 0x700032ba34ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; f_7000330370(c); x0 = c->x[0]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x298ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | (uint32_t)x20);
    c->x[3] = (uint64_t)(0ull | x19);
    c->x[2] = (uint64_t)(0ull | x0);
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    x0 = (uint64_t)(0ull | x21);
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 48; x21 = (uint64_t)(v); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[4]; __attribute__((musttail)) return shared_vertex_leaf(c);
}


void f_700032b2dc(Cpu*c){const int mode=front_end_mode();if(!mode||!shared_preflight(0)){f_700032b2dc_original(c);return;}const int previous=shared_parity;shared_parity=mode==2;native_700032b2dc(c);shared_parity=previous;}
void f_700032ba10(Cpu*c){const int mode=front_end_mode();if(!mode||!shared_preflight(1)){f_700032ba10_original(c);return;}const int previous=shared_parity;shared_parity=mode==2;native_700032ba10(c);shared_parity=previous;}
