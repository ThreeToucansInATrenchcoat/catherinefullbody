#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "cpu.h"
#include "engine_native_pools.h"
void f_700032c338_original(Cpu*);
static _Thread_local int pool_diagnostic;
static int pool_mode(void){static _Thread_local int m=-1;if(m<0){const char*p=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");m=p?atoi(p):1;}return m;}
static void pool_leaf(Cpu*c,uint32_t kind){
 if(!catherine_engine_pool_callback(kind,c->pc)){rt_dispatch(c);return;}
 catherine_engine_emit_pool(c->x[0],kind,c->x[1],pool_diagnostic);
 c->x[31]=c->sp;c->nzcv&=0xf0000000ull;
}
static void texture_pool_leaf(Cpu*c){pool_leaf(c,1);}
static void sampler_pool_leaf(Cpu*c){pool_leaf(c,2);}
static void native_pool_body(Cpu *c) {
    FN_ENTER(0x700032c338ull);
    Flags F = {0};
    uint64_t x0 = c->x[0], x1 = c->x[1], x2 = c->x[2], x8 = c->x[8];
    { uint64_t ea = x1 + 0x1ull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }
    (void)(fsub32(&F, (uint32_t)x8, 0x2ull));
    if (fc_sub32(&F, CC_EQ)) { goto L_700032c364; }
    (void)(fsub32(&F, (uint32_t)x8, 0x1ull));
    if (fc_sub32(&F, CC_NE)) { goto L_700032c37c; }
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x1 + 0x10ull; uint64_t v = LD64(ea); x1 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x350ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x2 = (uint64_t)(v); }
    c->nzcv = fnz_sub32(&F); c->x[0] = x0; c->x[1] = x1; c->x[2] = x2; c->x[8] = x8; c->pc = x2; __attribute__((musttail)) return texture_pool_leaf(c);
L_700032c364:;
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x1 + 0x10ull; uint64_t v = LD64(ea); x1 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x358ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x2 = (uint64_t)(v); }
    c->nzcv = fnz_sub32(&F); c->x[0] = x0; c->x[1] = x1; c->x[2] = x2; c->x[8] = x8; c->pc = x2; __attribute__((musttail)) return sampler_pool_leaf(c);
L_700032c37c:;
    c->x[8] = x8; return;
}


void f_700032c338(Cpu*c){
 const int mode=pool_mode();if(!mode){f_700032c338_original(c);return;}
 const uint32_t kind=LD8(c->x[1]+1);
 if(kind!=1&&kind!=2){f_700032c338_original(c);return;}
 const uint64_t cell=LD64(kind==1?0x7000ccf350ull:0x7000ccf358ull);
 if(!cell||!catherine_engine_pool_callback(kind,LD64(cell))){f_700032c338_original(c);return;}
 const int previous=pool_diagnostic;pool_diagnostic=mode==2;native_pool_body(c);pool_diagnostic=previous;
}
