#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "cpu.h"
#include "engine_native_front_end.h"
#include "engine_native_arrays.h"
#include "engine_native_compute.h"
void f_700033022c(Cpu*);void f_700033034c(Cpu*);void f_7000330370(Cpu*);
void f_700032b560_original(Cpu*);void f_700032b574_original(Cpu*);void f_700032b5c4_original(Cpu*);
static _Thread_local int shared_draw_parity;
static int shared_draw_mode(void){static _Thread_local int m=-1;if(m<0){const char*p=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");m=p?atoi(p):1;}return m;}
static void shared_draw_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void shared_draw_note(unsigned kind){if(shared_draw_parity){static _Atomic uint64_t counts[3];const uint64_t n=atomic_fetch_add_explicit(&counts[kind],1,memory_order_relaxed)+1;if(n==1||n%100000==0)fprintf(stderr,"Native shared %s requests: %llu\n",kind==0?"dispatch":kind==1?"array draw":"indexed draw",(unsigned long long)n);}}
static void shared_dispatch_leaf(Cpu*c){
 if(!catherine_compute_callback(CatherineComputeDispatch,c->pc)){rt_dispatch(c);return;}
 const CatherineComputeDispatchSize packet={(uint32_t)c->x[1],(uint32_t)c->x[2],(uint32_t)c->x[3]};
 catherine_compute_emit_dispatch(c->x[0],&packet,shared_draw_parity);shared_draw_note(0);shared_draw_bridge(c);
}
static void shared_arrays_leaf(Cpu*c){
 if(!catherine_engine_array_callback(c->pc)){rt_dispatch(c);return;}
 const CatherineEngineArrayDraw packet={(uint32_t)c->x[1],(int32_t)c->x[2],(int32_t)c->x[3]};
 catherine_engine_array_emit(c->x[0],&packet,shared_draw_parity);shared_draw_note(1);shared_draw_bridge(c);
}
static void shared_indexed_leaf(Cpu*c){
 if(!catherine_engine_indexed_callback(c->pc)){rt_dispatch(c);return;}
 const CatherineEngineIndexedDraw packet={c->x[4],(uint32_t)c->x[1],(uint32_t)c->x[2],(uint32_t)c->x[3],(int32_t)c->x[5]};
 catherine_engine_indexed_emit(c->x[0],&packet,shared_draw_parity);shared_draw_note(2);shared_draw_bridge(c);
}
static int shared_draw_preflight(unsigned kind){const uint64_t slots[3]={0x7000ccf1e0ull,0x7000ccf1e8ull,0x7000ccf1f0ull};const uint64_t cell=LD64(slots[kind]);if(!cell)return 0;const uint64_t callback=LD64(cell);return kind==0?catherine_compute_callback(CatherineComputeDispatch,callback):kind==1?catherine_engine_array_callback(callback):catherine_engine_indexed_callback(callback);}
static void native_32b560(Cpu *c) {
    FN_ENTER(0x700032b560ull);
    uint64_t x0 = c->x[0], x4 = c->x[4], x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x1e0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x4 = (uint64_t)(v); }
    c->x[0] = x0; c->x[4] = x4; c->x[8] = x8; c->pc = x4; __attribute__((musttail)) return shared_dispatch_leaf(c);
}


static void native_32b574(Cpu *c) {
    FN_ENTER(0x700032b574ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], sp_ = c->sp;
    { uint64_t ea = sp_ - 48; ST64(ea, x21); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x20ull);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    x0 = (uint32_t)(0u | (uint32_t)c->x[1]);
    x19 = (uint32_t)(0u | (uint32_t)c->x[3]);
    x20 = (uint32_t)(0u | (uint32_t)c->x[2]);
    c->x[30] = 0x700032b598ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; f_700033022c(c); x0 = c->x[0]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x1e8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x19);
    c->x[3] = (uint32_t)(0u | (uint32_t)x20);
    c->x[1] = (uint32_t)(0u | (uint32_t)x0);
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    x0 = (uint64_t)(0ull | x21);
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 48; x21 = (uint64_t)(v); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[4]; __attribute__((musttail)) return shared_arrays_leaf(c);
}


static void native_32b5c4(Cpu *c) {
    FN_ENTER(0x700032b5c4ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x22 = c->x[22], x30 = c->x[30], sp_ = c->sp;
    { uint64_t ea = sp_ - 64; ST64(ea, c->x[24]); ST64(ea + 8, c->x[23]); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, x22); ST64(ea + 8, c->x[21]); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[20]); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, x30); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); c->x[23] = (uint64_t)(v); }
    x0 = (uint32_t)(0u | (uint32_t)c->x[1]);
    x19 = (uint32_t)(0u | (uint32_t)c->x[5]);
    c->x[20] = (uint32_t)(0u | (uint32_t)c->x[4]);
    c->x[21] = (uint64_t)(0ull | c->x[3]);
    x22 = (uint32_t)(0u | (uint32_t)c->x[2]);
    x30 = 0x700032b5f4ull; c->x[0] = x0; c->x[19] = x19; c->x[22] = x22; c->x[30] = x30; c->sp = sp_; f_700033022c(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    c->x[24] = (uint32_t)(0u | (uint32_t)x0);
    x0 = (uint32_t)(0u | (uint32_t)x22);
    x30 = 0x700032b600ull; c->x[0] = x0; c->x[30] = x30; f_700033034c(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    x22 = (uint32_t)(0u | (uint32_t)x0);
    { uint64_t ea = c->x[21] + 0; uint64_t a = LD64(ea), b = LD64(ea + 8); x0 = (uint64_t)(a); c->x[1] = (uint64_t)(b); }
    x30 = 0x700032b60cull; c->x[0] = x0; c->x[22] = x22; c->x[30] = x30; f_7000330370(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x1f0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[6] = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x22);
    c->x[3] = (uint32_t)(0u | (uint32_t)c->x[20]);
    c->x[5] = (uint32_t)(0u | (uint32_t)x19);
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    c->x[4] = (uint64_t)(0ull | x0);
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[20] = (uint64_t)(a); x19 = (uint64_t)(b); }
    x0 = (uint64_t)(0ull | c->x[23]);
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x22 = (uint64_t)(a); c->x[21] = (uint64_t)(b); }
    c->x[1] = (uint32_t)(0u | (uint32_t)c->x[24]);
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 64; c->x[24] = (uint64_t)(a); c->x[23] = (uint64_t)(b); }
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[22] = x22; c->x[30] = x30; c->sp = sp_; c->pc = c->x[6]; __attribute__((musttail)) return shared_indexed_leaf(c);
}


void f_700032b560(Cpu*c){const int mode=shared_draw_mode();if(!mode||!shared_draw_preflight(0)){f_700032b560_original(c);return;}const int previous=shared_draw_parity;shared_draw_parity=mode==2;native_32b560(c);shared_draw_parity=previous;}
void f_700032b574(Cpu*c){const int mode=shared_draw_mode();if(!mode||!shared_draw_preflight(1)){f_700032b574_original(c);return;}const int previous=shared_draw_parity;shared_draw_parity=mode==2;native_32b574(c);shared_draw_parity=previous;}
void f_700032b5c4(Cpu*c){const int mode=shared_draw_mode();if(!mode||!shared_draw_preflight(2)){f_700032b5c4_original(c);return;}const int previous=shared_draw_parity;shared_draw_parity=mode==2;native_32b5c4(c);shared_draw_parity=previous;}
