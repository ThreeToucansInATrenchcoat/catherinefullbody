
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "cpu.h"
#include "engine_native_compute.h"
void f_700032c40c_original(Cpu*);void f_700032c478_original(Cpu*);void f_70003302f8(Cpu*);void f_7000330370(Cpu*);
static _Thread_local int range_parity;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void range_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void range_leaf(Cpu*c,unsigned storage){
 if(!catherine_compute_callback(storage?CatherineComputeStorage:CatherineComputeUniform,c->pc)){rt_dispatch(c);return;}
 const CatherineComputeRange range={c->x[3],c->x[4],(uint32_t)c->x[1],(uint32_t)c->x[2]};
 /* The existing emitter captures Block20 ownership now, inside the original job scope. */
 catherine_compute_emit_range(c->x[0],storage,&range,range_parity);
 if(range_parity){static _Atomic uint64_t counts[2];const uint64_t n=atomic_fetch_add_explicit(&counts[storage],1,memory_order_relaxed)+1;if(n==1||n%100000==0)fprintf(stderr,"Native shared %s range requests: %llu\n",storage?"storage":"uniform",(unsigned long long)n);}
 range_bridge(c);
}
static void shared_uniform_leaf(Cpu*c){range_leaf(c,0);}
static void shared_storage_leaf(Cpu*c){range_leaf(c,1);}
static int range_preflight(unsigned storage){const uint64_t cell=LD64(storage?0x7000ccf380ull:0x7000ccf378ull);return cell&&catherine_compute_callback(storage?CatherineComputeStorage:CatherineComputeUniform,LD64(cell));}
static void native_700032c40c(Cpu *c) {
    FN_ENTER(0x700032c40cull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], sp_ = c->sp;
    { uint64_t ea = sp_ - 64; ST64(ea, c->x[23]); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, c->x[22]); ST64(ea + 8, x21); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); c->x[22] = (uint64_t)(v); }
    x0 = (uint32_t)(0u | (uint32_t)c->x[2]);
    x19 = (uint64_t)(0ull | c->x[4]);
    x20 = (uint64_t)(0ull | c->x[3]);
    x21 = (uint32_t)(0u | (uint32_t)c->x[1]);
    c->x[30] = 0x700032c438ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; f_70003302f8(c); x0 = c->x[0]; x8 = c->x[8];
    c->x[23] = (uint32_t)(0u | (uint32_t)x0);
    { uint64_t ea = x20 + 0; uint64_t a = LD64(ea), b = LD64(ea + 8); x0 = (uint64_t)(a); c->x[1] = (uint64_t)(b); }
    c->x[30] = 0x700032c444ull; c->x[0] = x0; f_7000330370(c); x0 = c->x[0]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x378ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[3] = (uint64_t)(0ull | x0);
    x0 = (uint64_t)(0ull | c->x[22]);
    c->x[2] = (uint32_t)(0u | (uint32_t)x21);
    c->x[4] = (uint64_t)(0ull | x19);
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    c->x[1] = (uint32_t)(0u | (uint32_t)c->x[23]);
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[5] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); x21 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 64; c->x[23] = (uint64_t)(v); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[5]; __attribute__((musttail)) return shared_uniform_leaf(c);
}


static void native_700032c478(Cpu *c) {
    FN_ENTER(0x700032c478ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], sp_ = c->sp;
    { uint64_t ea = sp_ - 64; ST64(ea, c->x[23]); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, c->x[22]); ST64(ea + 8, x21); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); c->x[22] = (uint64_t)(v); }
    x0 = (uint32_t)(0u | (uint32_t)c->x[2]);
    x19 = (uint64_t)(0ull | c->x[4]);
    x20 = (uint64_t)(0ull | c->x[3]);
    x21 = (uint32_t)(0u | (uint32_t)c->x[1]);
    c->x[30] = 0x700032c4a4ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; f_70003302f8(c); x0 = c->x[0]; x8 = c->x[8];
    c->x[23] = (uint32_t)(0u | (uint32_t)x0);
    { uint64_t ea = x20 + 0; uint64_t a = LD64(ea), b = LD64(ea + 8); x0 = (uint64_t)(a); c->x[1] = (uint64_t)(b); }
    c->x[30] = 0x700032c4b0ull; c->x[0] = x0; f_7000330370(c); x0 = c->x[0]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x380ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[3] = (uint64_t)(0ull | x0);
    x0 = (uint64_t)(0ull | c->x[22]);
    c->x[2] = (uint32_t)(0u | (uint32_t)x21);
    c->x[4] = (uint64_t)(0ull | x19);
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    c->x[1] = (uint32_t)(0u | (uint32_t)c->x[23]);
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[5] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); x21 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 64; c->x[23] = (uint64_t)(v); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[5]; __attribute__((musttail)) return shared_storage_leaf(c);
}


void f_700032c40c(Cpu*c){const int mode=front_end_mode();if(!mode||!range_preflight(0)){f_700032c40c_original(c);return;}const int previous=range_parity;range_parity=mode==2;native_700032c40c(c);range_parity=previous;}
void f_700032c478(Cpu*c){const int mode=front_end_mode();if(!mode||!range_preflight(1)){f_700032c478_original(c);return;}const int previous=range_parity;range_parity=mode==2;native_700032c478(c);range_parity=previous;}
