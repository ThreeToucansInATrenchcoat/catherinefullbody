#include <stdlib.h>
#include "cpu.h"
#include "engine_native_clear.h"
#include "engine_native_viewport.h"
void f_700032c18c_original(Cpu*);void f_700032c2ac(Cpu*);void f_70005c8cd0(Cpu*);
static _Thread_local int clear_diagnostic;
static int clear_mode(void){static _Thread_local int m=-1;if(m<0){const char*p=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");m=p?atoi(p):1;}return m;}
static void clear_bridge(Cpu*c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void native_scissor_leaf(Cpu*c){
 if(!catherine_engine_clear_callback(0,c->pc)){rt_dispatch(c);return;}
 const CatherineEngineRasterValues values={{(uint32_t)c->x[1],(uint32_t)c->x[2],(uint32_t)c->x[3],(uint32_t)c->x[4]}};
 catherine_engine_emit_raster(c->x[0],CatherineScissors,&values,clear_diagnostic);clear_bridge(c);
}
static void native_clear_leaf(Cpu*c){
 if(!catherine_engine_clear_callback(1,c->pc)){rt_dispatch(c);return;}
 const CatherineEngineDepthClear values={(uint32_t)c->v[0],(uint32_t)c->x[1],(uint32_t)c->x[2],(uint32_t)c->x[3]};
 catherine_engine_emit_depth_clear(c->x[0],&values,clear_diagnostic);clear_bridge(c);
}
static void native_clear_body(Cpu *c) {
    FN_ENTER(0x700032c18cull);
    Flags F = {0};
    uint64_t x0 = c->x[0], x1 = c->x[1], x2 = c->x[2], x8 = c->x[8], x20 = c->x[20], sp_ = c->sp;
    { uint64_t ea = sp_ - 64; ST64(ea, VS64(8)); sp_ = ea; }
    { uint64_t ea = sp_ + 0x8ull; ST64(ea, c->x[23]); }
    { uint64_t ea = sp_ + 16; ST64(ea, c->x[22]); ST64(ea + 8, c->x[21]); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, c->x[19]); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    x20 = (uint64_t)(0ull | x0);
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x1 + 8; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); c->x[23] = (uint64_t)(b); }
    { uint64_t ea = x8 + 0x330ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[21] = (uint32_t)(0u | (uint32_t)c->x[3]);
    c->x[19] = (uint32_t)(0u | (uint32_t)x2);
    c->x[3] = (uint32_t)(0u | 0x7fffffffull);
    c->x[4] = (uint32_t)(0u | 0x7fffffffull);
    x1 = (uint32_t)(0u | 0u);
    x2 = (uint32_t)(0u | 0u);
    VW(vu64, 1, 8, VREG(vu64, 0) | VREG(vu64, 0));
    c->pc = x8; c->x[30] = 0x700032c1dcull; c->x[0] = x0; c->x[1] = x1; c->x[2] = x2; c->x[8] = x8; c->x[20] = x20; c->sp = sp_; native_scissor_leaf(c); x0 = c->x[0]; x1 = c->x[1]; x2 = c->x[2]; x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x290ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x1 = (uint32_t)(0u | 0u);
    x2 = (uint64_t)(0ull | 0ull);
    c->x[3] = (uint64_t)(0ull | 0ull);
    c->x[4] = (uint64_t)(0ull | c->x[22]);
    c->x[5] = (uint64_t)(0ull | c->x[23]);
    c->pc = x8; c->x[30] = 0x700032c204ull; c->x[0] = x0; c->x[1] = x1; c->x[2] = x2; c->x[8] = x8; rt_dispatch(c); x0 = c->x[0]; x1 = c->x[1]; x2 = c->x[2]; x8 = c->x[8];
    (void)(fsub32(&F, (uint32_t)c->x[21], 0x3ull));
    if (fc_sub32(&F, CC_EQ)) { goto L_700032c240; }
    (void)(fsub32(&F, (uint32_t)c->x[21], 0x2ull));
    if (fc_sub32(&F, CC_EQ)) { goto L_700032c260; }
    (void)(fsub32(&F, (uint32_t)c->x[21], 0x1ull));
    if (fc_sub32(&F, CC_NE)) { goto L_700032c298; }
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x338ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    x1 = (uint32_t)(0u | 0x1ull);
    VW(vu64, 1, 0, VREG(vu64, 8) | VREG(vu64, 8));
    x2 = (uint32_t)(0u | (uint32_t)c->x[19]);
    c->x[3] = (uint32_t)(0u | 0u);
    goto L_700032c280;
L_700032c240:;
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x338ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    c->x[3] = (uint32_t)(0xffffffffull);
    x1 = (uint32_t)(0u | 0x1ull);
    VW(vu64, 1, 0, VREG(vu64, 8) | VREG(vu64, 8));
    goto L_700032c27c;
L_700032c260:;
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x20 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x338ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    VW(vu64, 1, 0, VREG(vu64, 8) | VREG(vu64, 8));
    c->x[3] = (uint32_t)(0xffffffffull);
    x1 = (uint32_t)(0u | 0u);
L_700032c27c:;
    x2 = (uint32_t)(0u | (uint32_t)c->x[19]);
L_700032c280:;
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 0x8ull; uint64_t v = LD64(ea); c->x[23] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); c->x[19] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); c->x[21] = (uint64_t)(b); }
    { uint64_t ea = sp_; c->v[8] = LD64(ea); sp_ = sp_ + 64; }
    c->nzcv = fnz_sub32(&F); c->x[0] = x0; c->x[1] = x1; c->x[2] = x2; c->x[8] = x8; c->x[20] = x20; c->sp = sp_; c->pc = c->x[4]; __attribute__((musttail)) return native_clear_leaf(c);
L_700032c298:;
    x0 = (uint64_t)(0x7000771000ull);
    x0 = (uint64_t)(x0 + 0x663ull);
    x1 = (uint64_t)(0ull | x0);
    x2 = (uint32_t)(0u | 0u);
    c->x[30] = 0x700032c2acull; c->x[0] = x0; c->x[1] = x1; c->x[2] = x2; f_70005c8cd0(c); x0 = c->x[0]; x1 = c->x[1]; x2 = c->x[2]; x8 = c->x[8];
    __attribute__((musttail)) return f_700032c2ac(c);
}


void f_700032c18c(Cpu*c){
 const int mode=clear_mode();if(!mode){f_700032c18c_original(c);return;}
 const uint32_t operation=(uint32_t)c->x[3];
 if(operation<1||operation>3){f_700032c18c_original(c);return;}
 const uint64_t scissor=LD64(0x7000ccf330ull),clear=LD64(0x7000ccf338ull);
 if(!scissor||!clear||!catherine_engine_clear_callback(0,LD64(scissor))||!catherine_engine_clear_callback(1,LD64(clear))){f_700032c18c_original(c);return;}
 const int previous=clear_diagnostic;clear_diagnostic=mode==2;native_clear_body(c);clear_diagnostic=previous;
}
