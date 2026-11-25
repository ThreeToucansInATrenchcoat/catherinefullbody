#include "cpu.h"
#include "thread_affinity.h"
#include <assert.h>
#include <stdio.h>

static CatherineThreadAffinity main_affinity={0,7},worker={2,7};
static CatherineThreadAffinity *entries[]={&worker,NULL};
static CatherineThreadAffinity *current=&main_affinity;
static unsigned calls;
volatile int g_fnprof=0;
void fnprof_hit(uint64_t pc,uint64_t lr,uint64_t x0){(void)pc;(void)lr;(void)x0;}
void rt_svc(Cpu *cpu,uint32_t svc,uint64_t pc){
    (void)pc;++calls;
    assert(catherine_thread_affinity_svc(cpu,svc,&main_affinity,current,entries,2));
}
void f_7005389884(Cpu *cpu);
void f_70053898a4(Cpu *cpu);

static void set(Cpu *cpu,uint32_t handle,int32_t core,uint64_t mask,uint32_t expected){
    cpu->x[0]=handle;cpu->x[1]=(uint32_t)core;cpu->x[2]=mask;cpu->x[3]=7;
    const unsigned before=calls;f_70053898a4(cpu);
    assert(calls==before+1&&cpu->x[0]==expected&&cpu->x[3]==7);
}
static void get(Cpu *cpu,uint32_t handle,int32_t core,uint64_t mask){
    int32_t actual_core=-99;uint64_t actual_mask=~0ull;
    const uint64_t sp=cpu->sp;
    cpu->x[0]=(uintptr_t)&actual_core;cpu->x[1]=(uintptr_t)&actual_mask;cpu->x[2]=handle;
    f_7005389884(cpu);
    assert(!cpu->x[0]&&actual_core==core&&actual_mask==mask&&cpu->sp==sp);
}
int main(void){
    uint64_t stack[16]={0};Cpu cpu={0};cpu.sp=(uintptr_t)(stack+16);
    // Exact observed register call is an invalid handle, not a current-thread alias.
    set(&cpu,0,-1,7,0xe401);assert(main_affinity.core==0&&main_affinity.mask==7);
    set(&cpu,1,2,4,0);get(&cpu,1,2,4);
    set(&cpu,1,-1,7,0);get(&cpu,1,0,7);
    set(&cpu,200,-2,4,0);get(&cpu,200,2,4);
    set(&cpu,200,-3,0,0);get(&cpu,200,2,4);
    current=&worker;set(&cpu,0xffff8000u,1,2,0);get(&cpu,0xffff8000u,1,2);
    get(&cpu,200,1,2);get(&cpu,1,0,7);
    set(&cpu,200,-4,7,0xf001);set(&cpu,200,3,7,0xf001);
    set(&cpu,200,1,8,0xf001);set(&cpu,200,1,1,0xf001);get(&cpu,200,1,2);
    set(&cpu,199,-1,7,0xe401);set(&cpu,201,-1,7,0xe401);set(&cpu,202,-1,7,0xe401);
    cpu.x[0]=0x1234;cpu.x[1]=0x5678;cpu.x[2]=0;
    assert(catherine_thread_affinity_svc(&cpu,0x0e,&main_affinity,current,entries,2));
    assert(cpu.x[0]==0xe401&&cpu.x[1]==0x5678&&cpu.x[2]==0);
    assert(!catherine_thread_affinity_svc(&cpu,0x10,&main_affinity,current,entries,2));
    puts("PASS actual native SVC wrappers: observed zero handle, main/worker/current round trips, process default, keep modes, invalid handles and masks");
}
