#include <stdint.h>
extern uint64_t catherine_native_record_scope_begin(void);
extern void catherine_native_record_scope_end(uint64_t);
extern void catherine_native_record_scope_abandon(void);

#include <stdlib.h>
#include "cpu.h"
#include "engine_native_layouts.h"
void f_700032b8bc_original(Cpu*);
static _Thread_local int layout_diagnostic;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static int omit_preflight(void){static _Thread_local int mode=-1;if(mode<0){const char*v=getenv("CATHERINE_NATIVE_PREFLIGHT");mode=v?atoi(v):1;}return mode!=0;}
static void layout_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void layout_attributes_leaf(Cpu*c){
 if(!catherine_layout_callback(0,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const uint32_t count=(uint32_t)c->x[1]>32?32:(uint32_t)c->x[1];CatherineEngineAttrib values[32];
 for(uint32_t k=0;k<count;++k){const uint32_t packed=LD32(c->x[2]+4ull*k);values[k]=(CatherineEngineAttrib){(packed>>21)&1023,(packed>>7)&16383,packed&31};}
 catherine_engine_emit_attributes(c->x[0],count,values,layout_diagnostic);layout_bridge(c);
}
static void layout_streams_leaf(Cpu*c){
#if defined(CATHERINE_NATIVE_SCOPE_TEST_SECOND_FALLBACK)
 catherine_native_record_scope_abandon();rt_dispatch(c);return;
#endif
 if(!catherine_layout_callback(1,c->pc)){catherine_native_record_scope_abandon();rt_dispatch(c);return;}
 const uint32_t count=(uint32_t)c->x[1]>32?32:(uint32_t)c->x[1];CatherineEngineStream values[32];
 for(uint32_t k=0;k<count;++k)values[k]=(CatherineEngineStream){LD32(c->x[2]+8ull*k),LD32(c->x[2]+8ull*k+4)};
 catherine_engine_emit_streams(c->x[0],count,values,layout_diagnostic);layout_bridge(c);
}
static int layout_preflight(void){for(unsigned k=0;k<2;++k){const uint64_t cell=LD64(0x7000ccf278ull+8*k);if(!cell||!catherine_layout_callback(k,LD64(cell)))return 0;}return 1;}
static void native_layout_body(Cpu *c) {
    FN_ENTER(0x700032b8bcull);
    uint64_t x0 = c->x[0], x1 = c->x[1], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], sp_ = c->sp;
    { uint64_t ea = sp_ - 32; ST64(ea, x20); ST64(ea + 8, x19); sp_ = ea; }
    { uint64_t ea = sp_ + 16; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x10ull);
    x19 = (uint64_t)(0ull | x1);
    x20 = (uint64_t)(0ull | x0);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x19 + 0x8ull; uint64_t v = LD32(ea); x1 = (uint64_t)(v); }
    { uint64_t ea = x19 + 0x18ull; uint64_t v = LD64(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x278ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->pc = x8; c->x[30] = 0x700032b8ecull; c->x[0] = x0; c->x[1] = x1; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; layout_attributes_leaf(c); x0 = c->x[0]; x1 = c->x[1]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x19 + 0x4ull; uint64_t v = LD32(ea); x1 = (uint64_t)(v); }
    { uint64_t ea = x19 + 0x10ull; uint64_t v = LD64(ea); c->x[2] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x280ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[3] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 32; x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    c->x[0] = x0; c->x[1] = x1; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; c->pc = c->x[3]; __attribute__((musttail)) return layout_streams_leaf(c);
}


void f_700032b8bc(Cpu*c){const int mode=front_end_mode();if(!mode||(!omit_preflight()&&!layout_preflight())){catherine_native_record_scope_abandon();f_700032b8bc_original(c);return;}const int previous=layout_diagnostic;layout_diagnostic=mode==2;const uint64_t record_scope=catherine_native_record_scope_begin();native_layout_body(c);catherine_native_record_scope_end(record_scope);layout_diagnostic=previous;}
