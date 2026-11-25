#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
int catherine_engine_buffer_map_callback(uint64_t callback);
uint64_t catherine_engine_buffer_map_pointer(uint64_t guest,int checked);
void f_700032b2a4_original(Cpu*);
static int buffer_map_mode(void){static _Thread_local int m=-1;if(m<0){const char*p=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");m=p?atoi(p):1;}return m;}
static void buffer_map_leaf(Cpu*c,int checked){
 if(!catherine_engine_buffer_map_callback(c->pc)){rt_dispatch(c);return;}
 c->x[0]=catherine_engine_buffer_map_pointer(c->x[0],checked);
 c->x[31]=c->sp;c->nzcv&=0xf0000000ull;
}
static void native_buffer_map(Cpu *c,int checked) {
    FN_ENTER(0x700032b2a4ull);
    uint64_t x0 = c->x[0], x1 = c->x[1], x8 = c->x[8];
    x8 = (uint64_t)(0x7000ccf000ull);
    { uint64_t ea = x8 + 0x188ull; uint64_t v = LD64(ea); x8 = (uint64_t)(v); }
    { uint64_t ea = x0 + 0x8ull; uint64_t v = LD64(ea); x0 = (uint64_t)(v); }
    { uint64_t ea = x8 + 0x0ull; uint64_t v = LD64(ea); x1 = (uint64_t)(v); }
    c->x[0] = x0; c->x[1] = x1; c->x[8] = x8; c->pc = x1; return buffer_map_leaf(c,checked);
}


void f_700032b2a4(Cpu*c){
 const int mode=buffer_map_mode();
 if(!mode){f_700032b2a4_original(c);return;}
 const uint64_t cell=LD64(0x7000ccf188ull);
 if(!cell||!catherine_engine_buffer_map_callback(LD64(cell))){f_700032b2a4_original(c);return;}
 native_buffer_map(c,mode==2);
}
