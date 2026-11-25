
#include <stdlib.h>
#include "cpu.h"
#include "engine_native_compute.h"
#include "engine_native_geometry.h"
void f_70002fc100(Cpu*);void f_70002fc4c0(Cpu*);
static _Thread_local int geometry_parity;
static int front_end_mode(void){static _Thread_local int mode=-1;if(mode<0){const char* v=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");mode=v?atoi(v):1;}return mode;}
static void geometry_bridge(Cpu* c){c->x[31]=c->sp;c->nzcv&=0xf0000000ull;}
static void geometry_address_leaf(Cpu* c){
    if(!catherine_compute_callback(CatherineComputeAddress,c->pc)){rt_dispatch(c);return;}
    c->x[0]=catherine_compute_buffer_address(c->x[0]);geometry_bridge(c);
}
static void geometry_vertex_leaf(Cpu* c){
    if(!catherine_engine_vertex_callback(c->pc)){rt_dispatch(c);return;}
    const CatherineEngineVertexRange range={c->x[2],c->x[3],(uint32_t)c->x[1]};
    catherine_engine_emit_vertex_range(c->x[0],&range,geometry_parity);geometry_bridge(c);
}
static int geometry_preflight(void){
    const uint64_t address_slot=LD64(0x7000ccf198ull),vertex_slot=LD64(0x7000ccf298ull);
    if(!address_slot||!vertex_slot)return 0;
    return catherine_compute_callback(CatherineComputeAddress,LD64(address_slot))&&
           catherine_engine_vertex_callback(LD64(vertex_slot));
}
static void geometry_700032b2dc(Cpu*);
static void geometry_700032ba10(Cpu*);
static void geometry_7000330370(Cpu*);
static void geometry_70002fc100(Cpu*);
static void geometry_70002fc4c0(Cpu*);
static void geometry_700032b2dc(Cpu *c) {
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
    c->pc = x8; x30 = 0x700032b300ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[29] = x29; c->x[30] = x30; c->sp = sp_; geometry_address_leaf(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x19 + 0; ST64(ea, x0); ST64(ea + 8, 0ull); }
    { uint64_t ea = sp_ + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x29 = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_; uint64_t v = LD64(ea); sp_ = sp_ + 32; x19 = (uint64_t)(v); }
    c->x[19] = x19; c->x[29] = x29; c->x[30] = x30; c->sp = sp_; return;
}


static void geometry_700032ba10(Cpu *c) {
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
    c->x[30] = 0x700032ba34ull; c->x[0] = x0; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; geometry_7000330370(c); x0 = c->x[0]; x8 = c->x[8];
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
    c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; c->pc = c->x[4]; __attribute__((musttail)) return geometry_vertex_leaf(c);
}


static void geometry_7000330370(Cpu *c) {
    FN_ENTER(0x7000330370ull);
    return;
}


static void geometry_70002fc100(Cpu *c) {
    FN_ENTER(0x70002fc100ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x19 = c->x[19], x20 = c->x[20], x21 = c->x[21], sp_ = c->sp;
    sp_ = (uint64_t)(sp_ - 0x40ull);
    { uint64_t ea = sp_ + 8; ST64(ea, 0ull); ST64(ea + 8, x21); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, x19); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, c->x[30]); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    x21 = (uint64_t)(0x7000cc9000ull);
    { uint64_t ea = x0 + 0x8ull; uint64_t v = LD64(ea); x20 = (uint64_t)(v); }
    { uint64_t ea = x21 + 0x498ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    x8 = (uint32_t)(0x6c88ull);
    x19 = (uint64_t)(0ull | x0);
    x0 = (uint64_t)(x21 + x8);
    c->x[1] = (uint64_t)(sp_ + 0x0ull);
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, 0ull); }
    c->x[30] = 0x70002fc138ull; c->x[0] = x0; c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; geometry_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8];
    { uint64_t ea = x20 + 16; uint64_t a = LD64(ea), b = LD64(ea + 8); x8 = (uint64_t)(a); c->x[3] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 0x0ull; uint64_t v = LD64(ea); c->x[9] = (uint64_t)(v); }
    x8 = (uint64_t)(c->x[9] + x8);
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, x8); }
    { uint64_t ea = x21 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x8ull; uint64_t v = LD32(ea); c->x[1] = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x20ull; uint64_t v = LD64(ea); c->x[4] = (uint64_t)(v); }
    c->x[2] = (uint64_t)(sp_ + 0x0ull);
    c->x[30] = 0x70002fc15cull; c->x[0] = x0; c->x[8] = x8; geometry_700032ba10(c); x0 = c->x[0]; x8 = c->x[8];
    { uint64_t ea = x21 + 0x6d20ull; ST64(ea, 0ull); }
    { uint64_t ea = x19 + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x8 = (uint64_t)(x8 + 0x28ull);
    { uint64_t ea = x19 + 0x8ull; ST64(ea, x8); }
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); c->x[30] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); x19 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 0x10ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    sp_ = (uint64_t)(sp_ + 0x40ull);
    c->x[8] = x8; c->x[19] = x19; c->x[20] = x20; c->x[21] = x21; c->sp = sp_; return;
}


static void geometry_70002fc4c0(Cpu *c) {
    FN_ENTER(0x70002fc4c0ull);
    uint64_t x0 = c->x[0], x8 = c->x[8], x20 = c->x[20], x21 = c->x[21], x30 = c->x[30], sp_ = c->sp;
    sp_ = (uint64_t)(sp_ - 0x40ull);
    { uint64_t ea = sp_ + 8; ST64(ea, 0ull); ST64(ea + 8, x21); }
    { uint64_t ea = sp_ + 32; ST64(ea, x20); ST64(ea + 8, c->x[19]); }
    { uint64_t ea = sp_ + 48; ST64(ea, c->x[29]); ST64(ea + 8, x30); }
    c->x[29] = (uint64_t)(sp_ + 0x30ull);
    x20 = (uint64_t)(0x7000cc9000ull);
    { uint64_t ea = x0 + 0x8ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x498ull; uint64_t v = LD64(ea); x20 = (uint64_t)(v); }
    x8 = (uint32_t)(0x6c88ull);
    c->x[19] = (uint64_t)(0ull | x0);
    x0 = (uint64_t)(x20 + x8);
    c->x[1] = (uint64_t)(sp_ + 0x0ull);
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, 0ull); }
    x30 = 0x70002fc4f8ull; c->x[0] = x0; c->x[8] = x8; c->x[20] = x20; c->x[21] = x21; c->x[30] = x30; c->sp = sp_; geometry_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x21 + 0x10ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = sp_ + 0x0ull; uint64_t v = LD64(ea); c->x[9] = (uint64_t)(v); }
    x8 = (uint64_t)(c->x[9] + x8);
    { uint64_t ea = sp_ + 0x0ull; ST64(ea, x8); }
    { uint64_t ea = x21 + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 88; uint64_t a = LD32(ea), b = LD32(ea + 4); c->x[4] = (uint64_t)(a); c->x[3] = (uint64_t)(b); }
    c->x[2] = (uint64_t)(sp_ + 0x0ull);
    c->x[1] = (uint32_t)(0u | 0u);
    x30 = 0x70002fc520ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; geometry_700032ba10(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x21 + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0xb0ull; uint64_t v = LD32(ea); c->x[9] = (uint64_t)(v); }
    if ((uint32_t)c->x[9] == 0) { goto L_70002fc554; }
    x0 = (uint64_t)(x8 + 0x60ull);
    c->x[1] = (uint64_t)(sp_ + 0x0ull);
    x30 = 0x70002fc538ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; geometry_700032b2dc(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x21 + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x20 + 0x5ca0ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    c->x[1] = (uint32_t)(0u | 0x1ull);
    { uint64_t ea = x8 + 176; uint64_t a = LD32(ea), b = LD32(ea + 4); c->x[4] = (uint64_t)(a); c->x[3] = (uint64_t)(b); }
    c->x[2] = (uint64_t)(sp_ + 0x0ull);
    x30 = 0x70002fc550ull; c->x[0] = x0; c->x[8] = x8; c->x[30] = x30; geometry_700032ba10(c); x0 = c->x[0]; x8 = c->x[8]; x30 = c->x[30];
    { uint64_t ea = x21 + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
L_70002fc554:;
    { uint64_t ea = x20 + 0x6d20ull; ST64(ea, x8); }
    { uint64_t ea = c->x[19] + 0x8ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    x8 = (uint64_t)(x8 + 0x18ull);
    { uint64_t ea = c->x[19] + 0x8ull; ST64(ea, x8); }
    { uint64_t ea = sp_ + 48; uint64_t a = LD64(ea), b = LD64(ea + 8); c->x[29] = (uint64_t)(a); x30 = (uint64_t)(b); }
    { uint64_t ea = sp_ + 32; uint64_t a = LD64(ea), b = LD64(ea + 8); x20 = (uint64_t)(a); c->x[19] = (uint64_t)(b); }
    { uint64_t ea = sp_ + 0x10ull; uint64_t v = LD64(ea); x21 = (uint64_t)(v); }
    sp_ = (uint64_t)(sp_ + 0x40ull);
    c->x[8] = x8; c->x[20] = x20; c->x[21] = x21; c->x[30] = x30; c->sp = sp_; return;
    c->x[0] = x0; c->x[8] = x8; c->x[20] = x20; c->x[21] = x21; c->x[30] = x30; c->sp = sp_; rt_udf(c, 0x70002fc578ull);
    c->x[0] = x0; c->x[8] = x8; c->x[20] = x20; c->x[21] = x21; c->x[30] = x30; c->sp = sp_; rt_udf(c, 0x70002fc57cull);
}


void catherine_engine_stream_packet(Cpu* c){
    const int mode=front_end_mode();
    if(!mode||!geometry_preflight()){f_70002fc100(c);return;}
    const int previous=geometry_parity;geometry_parity=mode==2;
    geometry_70002fc100(c);geometry_parity=previous;
}

void catherine_engine_mesh_packet(Cpu* c){
    const int mode=front_end_mode();
    if(!mode||!geometry_preflight()){f_70002fc4c0(c);return;}
    const int previous=geometry_parity;geometry_parity=mode==2;
    geometry_70002fc4c0(c);geometry_parity=previous;
}
