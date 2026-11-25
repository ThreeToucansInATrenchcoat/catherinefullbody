
#include <stdlib.h>
#include "cpu.h"
#include "engine_native_compute.h"
#include "block20_owner_bridge.h"
void f_70002fb6b0(Cpu* c);
static _Thread_local int parity_mode;
static int front_end_mode(void) {
    static _Thread_local int mode=-1;
    if(mode<0){const char* value=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=value?atoi(value):1;}
    return mode;
}
static void bridge_effects(Cpu* c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void native_address_leaf(Cpu* c) {
    if(!catherine_compute_callback(CatherineComputeAddress,c->pc)){rt_dispatch(c);return;}
    c->x[0]=catherine_compute_buffer_address(c->x[0]);bridge_effects(c);
}
static void native_program_leaf(Cpu* c) {
    if(!catherine_compute_callback(CatherineComputeProgram,c->pc)){rt_dispatch(c);return;}
    catherine_compute_emit_program(c->x[0],c->x[1],(uint32_t)c->x[2]);bridge_effects(c);
}
static void native_uniform_leaf(Cpu* c) {
    if(!catherine_compute_callback(CatherineComputeUniform,c->pc)){rt_dispatch(c);return;}
    const CatherineComputeRange range={c->x[3],c->x[4],(uint32_t)c->x[1],(uint32_t)c->x[2]};
    catherine_compute_emit_range(c->x[0],0,&range,parity_mode);bridge_effects(c);
}
static void native_storage_leaf(Cpu* c) {
    if(!catherine_compute_callback(CatherineComputeStorage,c->pc)){rt_dispatch(c);return;}
    const CatherineComputeRange range={c->x[3],c->x[4],(uint32_t)c->x[1],(uint32_t)c->x[2]};
    catherine_compute_emit_range(c->x[0],1,&range,parity_mode);bridge_effects(c);
}
static void native_dispatch_leaf(Cpu* c) {
    if(!catherine_compute_callback(CatherineComputeDispatch,c->pc)){rt_dispatch(c);return;}
    const CatherineComputeDispatchSize size={(uint32_t)c->x[1],(uint32_t)c->x[2],(uint32_t)c->x[3]};
    catherine_compute_emit_dispatch(c->x[0],&size,parity_mode);bridge_effects(c);
}
static void native_700032b2dc(Cpu* c);
static void native_700032b910(Cpu* c);
static void native_700032c40c(Cpu* c);
static void native_700032c478(Cpu* c);
static void native_700032b560(Cpu* c);
static void native_7000330308(Cpu* c);
static void native_70003302f8(Cpu* c);
static void native_7000330370(Cpu* c);
static void native_700032ab10(Cpu* c);
static void native_7000310a70(Cpu* c);
static void native_70002fb6b0(Cpu* c);
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
    c->pc = x8; x30 = 0x700032b300ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[29] = x29; c->x[30] = x30; c->sp = sp_; native_address_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0; ST64(ea, x0); ST64(ea + 8, 0ull); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x29 = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 32; x19 = (uint64_t)(v); }
    c->x[19] = x19; c->x[29] = x29; c->x[30] = x30; c->sp = sp_; return;
}

static void native_700032b910(Cpu *c) {
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
    c->x[30] = 0x700032b944ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; native_7000330308(c); x0 = c->x[0]; x1 = c->x[1]; x8 = c->x[8];
    c->x[2] = (uint32_t)(0u | (uint32_t)x0);
L_700032b948:;
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x19 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x288ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); c->x[3] = (uint64_t)(v); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    x1 = (uint64_t)(0ull | x20);
    { uint64_t ea = sp_; uint64_t a = LD64(ea), b = LD64(ea + 8); sp_ = ea + 32; x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    c->x[0] = x0; c->x[1] = x1; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->sp = sp_; c->pc = c->x[3]; __attribute__((musttail)) return native_program_leaf(c);
}

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
    c->x[30] = 0x700032c438ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; native_70003302f8(c); x0 = c->x[0]; x8 = c->x[8];
    c->x[23] = (uint32_t)(0u | (uint32_t)x0);
    { uint64_t ea = x20 + 0; uint64_t a = LD64(ea), b = LD64(ea + 8); x0 = (uint64_t)(a); c->x[1] = (uint64_t)(b); }
    c->x[30] = 0x700032c444ull; c->x[0] = x0; native_7000330370(c); x0 = c->x[0]; x8 = c->x[8];
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
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[5]; __attribute__((musttail)) return native_uniform_leaf(c);
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
    c->x[30] = 0x700032c4a4ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; native_70003302f8(c); x0 = c->x[0]; x8 = c->x[8];
    c->x[23] = (uint32_t)(0u | (uint32_t)x0);
    { uint64_t ea = x20 + 0; uint64_t a = LD64(ea), b = LD64(ea + 8); x0 = (uint64_t)(a); c->x[1] = (uint64_t)(b); }
    c->x[30] = 0x700032c4b0ull; c->x[0] = x0; native_7000330370(c); x0 = c->x[0]; x8 = c->x[8];
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
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[5]; __attribute__((musttail)) return native_storage_leaf(c);
}

static void native_700032b560(Cpu *c) {
    FN_ENTER(0x700032b560ull);
    uint64_t x0 = c->x[0], x4 = c->x[4], x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x1e0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x0 + 0xb8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x4 = (uint64_t)(v); }
    c->x[0] = x0; c->x[4] = x4; c->x[8] = x8; c->pc = x4; __attribute__((musttail)) return native_dispatch_leaf(c);
}

static void native_7000330308(Cpu *c) {
    FN_ENTER(0x7000330308ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x9 = c->x[9], x10 = c->x[10];
    x9 = (uint32_t)(ROR32((uint32_t)x0, 1) & 0x7fffffffull);
    x10 = (uint32_t)((uint32_t)x0 & 0x1ull);
    x10 = (uint32_t)(((uint32_t)x10 & 0xfffffff0ull) | ((((uint32_t)x10 & 0xfffffff7ull) | (ROR32((uint32_t)x9, 29) & 0x8ull)) & 0xfull));
    x8 = (uint32_t)(ROR32((uint32_t)x0, 2) & 0x3fffffffull);
    x10 = (uint32_t)(((uint32_t)x10 & 0xffffffe0ull) | ((((uint32_t)x10 & 0xffffffefull) | (ROR32((uint32_t)x8, 28) & 0x10ull)) & 0x1full));
    x8 = (uint32_t)((uint32_t)x9 & 0x4ull);
    x9 = (uint32_t)(ROR32((uint32_t)x0, 3) & 0x1fffffffull);
    x9 = (uint32_t)((uint32_t)x9 & 0x2ull);
    x8 = (uint32_t)((uint32_t)x8 | (uint32_t)x10);
    x8 = (uint32_t)((uint32_t)x9 | (uint32_t)x8);
    x9 = (uint32_t)((uint32_t)x0 & 0x20ull);
    x0 = (uint32_t)((uint32_t)x9 | (uint32_t)x8);
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; return;
}

static void native_70003302f8(Cpu *c) {
    FN_ENTER(0x70003302f8ull);
    uint64_t x0 = c->x[0], x8 = c->x[8];
    x8 = (uint64_t)(0x70007f4000ull);
    x8 = (uint64_t)(x8 + 0xb04ull);
    { uint64_t ea = x8 + ((uint64_t)(uint32_t)x0 << 2); uint64_t v = LD32(ea); x0 = (uint64_t)(v); }
    c->x[0] = x0; c->x[8] = x8; return;
}

static void native_7000330370(Cpu *c) {
    FN_ENTER(0x7000330370ull);
    return;
}

static void native_700032ab10(Cpu *c) {
    FN_ENTER(0x700032ab10ull);
    uint64_t x0 = c->x[0], x1 = c->x[1], x8 = c->x[8], x9 = c->x[9], x10 = c->x[10];
L_700032ab10:;
    x8 = (uint64_t)(ldx32(c, x0));
    x9 = (uint32_t)((uint32_t)x8 + (uint32_t)x1);
    { uint32_t st = stx32(c, x0, x9); x10 = (uint32_t)(st); }
    if ((uint32_t)x10 != 0) { goto L_700032ab10; }
    x0 = (uint32_t)(0u | (uint32_t)x8);
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; return;
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; rt_udf(c, 0x700032ab28ull);
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; rt_udf(c, 0x700032ab2cull);
}

static void native_7000310a70(Cpu *c) {
    FN_ENTER(0x7000310a70ull);
    Flags F = {0};
    uint64_t x0 = c->x[0], x8 = c->x[8], x27 = c->x[27], x28 = c->x[28], x30 = c->x[30], sp_ = c->sp;
    sp_ = (uint64_t)(sp_ - 0x70ull);
    { uint64_t ea = sp_ + 16; ST64(ea, x28); ST64(ea + 8, x27); }
    { uint64_t ea = sp_ + 32; ST64(ea, c->x[26]); ST64(ea + 8, c->x[25]); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[24]); ST64(ea + 8, c->x[23]); }
    { uint64_t ea = sp_ + 64; ST64(ea, c->x[22]); ST64(ea + 8, c->x[21]); }
    { uint64_t ea = sp_ + 80; ST64(ea, c->x[20]); ST64(ea + 8, c->x[19]); }
    { uint64_t ea = sp_ + 96; ST64(ea, c->x[29]); ST64(ea + 8, x30); }
    c->x[29] = (uint64_t)(sp_ + 0x60ull);
    x28 = (uint64_t)(0x7000cc9000ull);
    { uint64_t ea = x28 + 0x498ull; uint64_t v = LD64(ea); x28 = (uint64_t)(v); }
    { uint64_t ea = x28 + 0x6d18ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x27 = (uint64_t)(x0 + 0x8ull);
    c->x[20] = (uint64_t)(0ull | c->x[7]);
    c->x[22] = (uint64_t)(0ull | c->x[6]);
    c->x[23] = (uint64_t)(0ull | c->x[5]);
    c->x[19] = (uint64_t)(0ull | x0);
    c->x[24] = (uint64_t)(0ull | c->x[4]);
    c->x[25] = (uint64_t)(0ull | c->x[3]);
    c->x[26] = (uint64_t)(0ull | c->x[2]);
    c->x[21] = (uint32_t)(0u | (uint32_t)c->x[1]);
    (void)(fsub64(&F, x8, (uint64_t)(x27)));
    if (fc_sub64(&F, CC_EQ)) { goto L_7000310adc; }
    { uint64_t ea = x28 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | 0x20ull);
    c->x[1] = (uint64_t)(0ull | x27);
    x30 = 0x7000310ad8ull; c->x[0] = x0; c->x[8] = x8; c->x[27] = x27; c->x[28] = x28; c->x[30] = x30; c->sp = sp_; native_700032b910(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x28 + 0x6d18ull; ST64(ea, x27); }
L_7000310adc:;
    { uint64_t ea = x28 + 0x6570ull; uint64_t v = LD64(ea); x27 = (uint64_t)(v); }
    { uint64_t ea = x27 + 0x58ull; uint64_t v = LD32(ea); x8 = (uint64_t)(v); }
    c->x[9] = (uint32_t)((uint32_t)x8 + 0x3ull);
    x8 = (uint32_t)(0u - (uint32_t)x8);
    c->x[1] = (uint32_t)((uint32_t)c->x[9] & (uint32_t)x8);
    x0 = (uint64_t)(x27 + 0x64ull);
    x30 = 0x7000310af8ull; c->x[0] = x0; c->x[8] = x8; c->x[27] = x27; c->x[28] = x28; c->x[30] = x30; c->sp = sp_; native_700032ab10(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x27 + 0x60ull; uint64_t v = (uint64_t)(int64_t)(int32_t)LD32(ea); x8 = (uint64_t)(v); }
    x27 = (uint64_t)(x8 + (uint64_t)(int64_t)(int32_t)x0);
    { uint64_t ea = x28 + 0x6570ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x50ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + x27; ST32(ea, (uint32_t)c->x[21]); }
    { uint64_t ea = x28 + 0x6570ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(sp_ + 0x0ull);
    { uint64_t ea = sp_ + 0; ST64(ea, 0ull); ST64(ea + 8, 0ull); }
    x30 = 0x7000310b1cull; c->x[0] = x0; c->x[8] = x8; c->x[27] = x27; c->x[30] = x30; native_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = sp_ + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x8 = (uint64_t)(x8 + x27);
    x27 = (uint32_t)(0x5ull);
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, x8); }
    { uint64_t ea = x28 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[3] = (uint64_t)(sp_ + 0x0ull);
    c->x[2] = (uint32_t)(0u | (uint32_t)x27);
    { uint64_t ea = c->x[19] + 0xf0ull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    c->x[4] = (uint32_t)(0u | 0x4ull);
    x30 = 0x7000310b44ull; c->x[0] = x0; c->x[8] = x8; c->x[27] = x27; c->x[30] = x30; native_700032c40c(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x28 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = c->x[19] + 0xf4ull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x27);
    c->x[3] = (uint64_t)(0ull | c->x[26]);
    c->x[4] = (uint64_t)(0ull | c->x[25]);
    x30 = 0x7000310b5cull; c->x[0] = x0; c->x[30] = x30; native_700032c478(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x28 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = c->x[19] + 0xf8ull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x27);
    c->x[3] = (uint64_t)(0ull | c->x[24]);
    c->x[4] = (uint64_t)(0ull | c->x[23]);
    x30 = 0x7000310b74ull; c->x[0] = x0; c->x[30] = x30; native_700032c478(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x28 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = c->x[19] + 0xfcull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | (uint32_t)x27);
    c->x[3] = (uint64_t)(0ull | c->x[22]);
    c->x[4] = (uint64_t)(0ull | c->x[20]);
    x30 = 0x7000310b8cull; c->x[0] = x0; c->x[30] = x30; native_700032c478(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = c->x[19] + 0x100ull; uint64_t v = LD32(ea); x8 = (uint64_t)(v); }
    c->x[9] = (uint32_t)((uint32_t)c->x[21] + (uint32_t)x8);
    c->x[9] = (uint32_t)((uint32_t)c->x[9] - 0x1ull);
    c->x[1] = (uint32_t)(sdiv32((uint32_t)c->x[9], (uint32_t)x8));
    { uint64_t ea = x28 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[2] = (uint32_t)(0u | 0x1ull);
    c->x[3] = (uint32_t)(0u | 0x1ull);
    x30 = 0x7000310bacull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; native_700032b560(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = sp_ + 96; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 80; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[20] = (uint64_t)(a); c->x[19] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 64; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[22] = (uint64_t)(a); c->x[21] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[24] = (uint64_t)(a); c->x[23] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[26] = (uint64_t)(a); c->x[25] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x28 = (uint64_t)(a); x27 = (uint64_t)(b); }
    sp_ = (uint64_t)(sp_ + 0x70ull);
    c->x[27] = x27; c->x[28] = x28; c->x[30] = x30; c->sp = sp_; return;
    c->x[0] = x0; c->x[8] = x8; c->x[27] = x27; c->x[28] = x28; c->x[30] = x30; c->sp = sp_; rt_udf(c, 0x7000310bccull);
}

static void native_70002fb6b0(Cpu *c) {
    FN_ENTER(0x70002fb6b0ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x9 = c->x[9], x10 = c->x[10], x20 = c->x[20], sp_ = c->sp;
    sp_ = (uint64_t)(sp_ - 0x50ull);
    { uint64_t ea = sp_ + 48; ST64(ea, x20); ST64(ea + 8, c->x[19]); }
    { uint64_t ea = sp_ + 64; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x40ull);
    { uint64_t ea = x0 + 0x8ull; uint64_t v = LD64(ea); x20 = (uint64_t)(v); }
    { uint64_t ea = sp_ + 32; ST64(ea, 0ull); ST64(ea + 8, 0ull); }
    { uint64_t ea = x20 + 0x18ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    c->x[19] = (uint64_t)(0ull | x0);
    { uint64_t ea = x8 + 0xb0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(sp_ + 0x20ull);
    c->x[30] = 0x70002fb6dcull; c->x[0] = x0; c->x[8] = x8; c->x[20] = x20; c->sp = sp_; native_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8]; x9 = c->x[9]; x10 = c->x[10];
    { uint64_t ea = x20 + 0x18ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0xb8ull; uint64_t v = LD64(ea); x9 = (uint64_t)(v); }
    { uint64_t ea = sp_ + 0x20ull; uint64_t v = LD64(ea); x10 = (uint64_t)(v); }
    x9 = (uint64_t)(x10 + x9);
    { uint64_t ea = sp_ + 24; ST64(ea, 0ull); ST64(ea + 8, x9); }
    { uint64_t ea = sp_ + 0x10ull; ST64(ea, 0ull); }
    { uint64_t ea = x8 + 0xc8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(sp_ + 0x10ull);
    c->x[30] = 0x70002fb700ull; c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; native_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8]; x9 = c->x[9]; x10 = c->x[10];
    { uint64_t ea = x20 + 0x18ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x10 = (uint64_t)(0x7000cc9000ull);
    { uint64_t ea = sp_ + 0x10ull; uint64_t v = LD64(ea); x9 = (uint64_t)(v); }
    { uint64_t ea = x10 + 0x498ull; uint64_t v = LD64(ea); x10 = (uint64_t)(v); }
    c->x[1] = (uint64_t)(sp_ + 0x0ull);
    { uint64_t ea = x8 + 0xd0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, 0ull); }
    x8 = (uint64_t)(x9 + x8);
    { uint64_t ea = sp_ + 8; ST64(ea, 0ull); ST64(ea + 8, x8); }
    x8 = (uint32_t)(0x6c88ull);
    x0 = (uint64_t)(x10 + x8);
    c->x[30] = 0x70002fb730ull; c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; native_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8]; x9 = c->x[9]; x10 = c->x[10];
    { uint64_t ea = x20 + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x8 = (uint64_t)(a); c->x[3] = (uint64_t)(b); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = sp_ + 0x0ull; uint64_t v = LD64(ea); x9 = (uint64_t)(v); }
    x8 = (uint64_t)(x9 + x8);
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, x8); }
    { uint64_t ea = x20 + 0x18ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x10ull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0xc0ull; uint64_t v = LD64(ea); c->x[5] = (uint64_t)(v); }
    { uint64_t ea = x8 + 0xd8ull; uint64_t v = LD64(ea); c->x[7] = (uint64_t)(v); }
    c->x[2] = (uint64_t)(sp_ + 0x0ull);
    c->x[4] = (uint64_t)(sp_ + 0x20ull);
    c->x[6] = (uint64_t)(sp_ + 0x10ull);
    catherine_block20_job_enter(x20,LD64(c->x[2]));
    c->x[30] = 0x70002fb768ull; c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; native_7000310a70(c); x0 = c->x[0]; x8 = c->x[8]; x9 = c->x[9]; x10 = c->x[10];
    catherine_block20_job_leave();
    { uint64_t ea = c->x[19] + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x8 = (uint64_t)(x8 + 0x30ull);
    { uint64_t ea = c->x[19] + 0x8ull; ST64(ea, x8); }
    { uint64_t ea = sp_ + 64; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); c->x[19] = (uint64_t)(b); }
    sp_ = (uint64_t)(sp_ + 0x50ull);
    c->x[8] = x8; c->x[20] = x20; c->sp = sp_; return;
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; c->x[20] = x20; c->sp = sp_; rt_udf(c, 0x70002fb784ull);
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; c->x[20] = x20; c->sp = sp_; rt_udf(c, 0x70002fb788ull);
    c->x[0] = x0; c->x[8] = x8; c->x[9] = x9; c->x[10] = x10; c->x[20] = x20; c->sp = sp_; rt_udf(c, 0x70002fb78cull);
}

void catherine_engine_compute_packet(Cpu* c) {
    const int mode=front_end_mode();
    if(!mode){f_70002fb6b0(c);return;}
    static const uint32_t slots[5]={0x198,0x288,0x378,0x380,0x1e0};
    uint64_t callbacks[5];
    /* All unsupported targets fall back before guest stack/ring/cache writes.
     * A callback changed later is also checked at its actual native leaf. */
    for(unsigned i=0;i<5;++i){
        const uint64_t slot=LD64(0x7000ccf000ull+slots[i]);
        if(!slot){f_70002fb6b0(c);return;}
        callbacks[i]=LD64(slot);
    }
    if(!catherine_compute_preflight(callbacks)){f_70002fb6b0(c);return;}
    const int previous=parity_mode;parity_mode=mode==2;
    native_70002fb6b0(c);
    parity_mode=previous;
}
