#pragma once
#include "cpu.h"

typedef struct {int32_t core;uint64_t mask;} CatherineThreadAffinity;

static inline CatherineThreadAffinity *catherine_affinity_thread(uint32_t handle,
    CatherineThreadAffinity *main_thread,CatherineThreadAffinity *current,
    CatherineThreadAffinity *const *threads,unsigned count){
    if(handle==1)return main_thread;
    if(handle==0xffff8000u)return current;
    if(handle>=200&&handle-200<count)return threads[handle-200];
    return NULL;
}

// These are virtual guest cores, matching this host's existing three-core model;
// Windows remains free to schedule its host threads on the available processors.
static inline int catherine_thread_affinity_svc(Cpu *cpu,uint32_t svc,
    CatherineThreadAffinity *main_thread,CatherineThreadAffinity *current,
    CatherineThreadAffinity *const *threads,unsigned count){
    if(svc!=0x0e&&svc!=0x0f)return 0;
    const uint32_t handle=(uint32_t)cpu->x[svc==0x0e?2:0];
    CatherineThreadAffinity *thread=catherine_affinity_thread(handle,main_thread,current,threads,count);
    if(!thread){cpu->x[0]=0xe401;return 1;}
    if(svc==0x0e){
        cpu->x[0]=0;cpu->x[1]=(uint32_t)thread->core;cpu->x[2]=thread->mask;return 1;
    }
    int32_t core=(int32_t)cpu->x[1];
    const uint64_t mask=cpu->x[2]?cpu->x[2]:thread->mask;
    if(core==-1)core=0; // Process default, as in reference/runtime/kernel.c.
    else if(core==-2||core==-3)core=thread->core;
    if(!mask||(mask&~7ull)||core<0||core>2||!(mask&(1ull<<core))){
        cpu->x[0]=0xf001;return 1;
    }
    thread->core=core;thread->mask=mask;cpu->x[0]=0;return 1;
}
