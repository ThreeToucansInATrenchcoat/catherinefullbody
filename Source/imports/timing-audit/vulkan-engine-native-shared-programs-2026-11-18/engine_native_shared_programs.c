
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "cpu.h"
#include "engine_native_compute.h"
void f_700032b910_original(Cpu*);void f_7000330308(Cpu*);
static _Thread_local int program_diagnostic;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void shared_program_leaf(Cpu*c){
 if(!catherine_compute_callback(CatherineComputeProgram,c->pc)){rt_dispatch(c);return;}
 catherine_compute_emit_program(c->x[0],c->x[1],(uint32_t)c->x[2]);
 if(program_diagnostic){static _Atomic uint64_t requests;const uint64_t n=atomic_fetch_add_explicit(&requests,1,memory_order_relaxed)+1;if(n==1||n%100000==0)fprintf(stderr,"Native shared program bind requests: %llu\n",(unsigned long long)n);}
 c->x[31]=c->sp;c->nzcv&=0xf0000000ull;
}
static int program_preflight(void){const uint64_t cell=LD64(0x7000ccf288ull);return cell&&catherine_compute_callback(CatherineComputeProgram,LD64(cell));}
static void native_program_body(Cpu *c) {
    FN_ENTER(0x700032b910ull);
    uint64_t x0 = c->x[0], x1 = c->x[1], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], sp_ = c->sp;
    { uint64_t ea = sp_ - 32; ST64(ea, x20); ST64(ea + 8, x19); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x10ull);
    x19 = (uint64_t)(0ull | x0);
    if (x1 == 0) { goto L_700032b938; }
    { uint64_t ea = x1 + 0x8ull; uint64_t v = LD64(ea); x20 = (uint64_t)(v); }
    { uint64_t ea = x1 + 0x1ull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    if (((x8 >> 0) & 1) != 0) { goto L_700032b93c; }
    { uint64_t ea = x1 + 0x4ull; uint64_t v = LD32(ea); c->x[2] = (uint64_t)(v); }
    goto L_700032b948;
L_700032b938:;
    x20 = (uint64_t)(0ull | 0ull);
L_700032b93c:;
    x0 = (uint32_t)(0u | (uint32_t)c->x[2]);
    c->x[30] = 0x700032b944ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; f_7000330308(c); x0 = c->x[0]; x1 = c->x[1]; x8 = c->x[8];
    c->x[2] = (uint32_t)(0u | (uint32_t)x0);
L_700032b948:;
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x288ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[3] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    x1 = (uint64_t)(0ull | x20);
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 32; x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    c->x[0] = x0; c->x[1] = x1; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; c->pc = c->x[3]; __attribute__((musttail)) return shared_program_leaf(c);
}


void f_700032b910(Cpu*c){const int mode=front_end_mode();if(!mode||!program_preflight()){f_700032b910_original(c);return;}const int previous=program_diagnostic;program_diagnostic=mode==2;native_program_body(c);program_diagnostic=previous;}
