#pragma once
#include <stdlib.h>
#include "cpu.h"
static inline int native_allocator32_mode(void){static _Thread_local int selected=-1;if(selected<0){const char*e=getenv("CATHERINE_NATIVE_ALLOCATOR32");selected=e?atoi(e):1;}return selected;}
static inline int native_allocator32_try(Cpu*c){
    const uint64_t address=c->x[0],cpu=(uint64_t)(uintptr_t)c;
    if(!native_allocator32_mode()||(address&3u)||address-cpu<sizeof(*c)||cpu-address<4u)return 0;
    const uint32_t delta=(uint32_t)c->x[1];
    const uint32_t old=__atomic_fetch_add((uint32_t*)(uintptr_t)address,delta,__ATOMIC_SEQ_CST);
    c->excl_addr=~0ull;c->excl_val=old;
    c->x[0]=old;c->x[8]=old;c->x[9]=(uint32_t)(old+delta);c->x[10]=0;
    return 1;
}
