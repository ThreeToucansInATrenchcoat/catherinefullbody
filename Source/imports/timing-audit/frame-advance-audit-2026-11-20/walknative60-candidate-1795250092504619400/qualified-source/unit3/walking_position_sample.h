#pragma once
#include "cpu.h"
#include "timing_clock.h"
#include <math.h>
#include <string.h>
uint64_t catherine_camera_render_clock(void);
uint64_t catherine_announcement_frame_clock(void);
typedef struct CatherineWalkingHistory {
    uint64_t owner,root_class,child_class,wrapper,wrapper_class,model;
    uint32_t previous[3],current[3],epoch;
    uint32_t last_model_position[3];
    unsigned valid,model_valid;
} CatherineWalkingHistory;
typedef struct CatherineWalkingSample {
    CatherineWalkingHistory *history;
    uint64_t owner,root_class,child_class,wrapper,wrapper_class,caller,resolved_model;
    uint32_t before[3],authoritative[3],displayed[3],epoch;
    unsigned enabled,staged,tick,prepared;
    float phase;
    _Alignas(16) unsigned char descriptor[32];
} CatherineWalkingSample;
static _Thread_local CatherineWalkingHistory catherine_walking_histories[4];
static void catherine_walking_invalidate(uint64_t owner) {
    for(unsigned i=0;i<4;i++)if(catherine_walking_histories[i].owner==owner)catherine_walking_histories[i].valid=0;
}
static int catherine_walking_owner(uint64_t owner,uint64_t caller) {
    uint64_t cell,root;
    if(caller==0x70000b7d40ull){cell=LD64(0x7000cca2c8ull);root=cell?LD64(cell):0;return root&&owner==root+0x90ull&&LD64(owner-0x10)==0x70009f74b0ull&&LD64(owner)==0x70009f6ae0ull;}
    if(caller==0x70000cce50ull){cell=LD64(0x7000cca408ull);root=cell?LD64(cell):0;return root&&owner==root+0x40ull&&LD64(owner-0x10)==0x70009f7f70ull&&LD64(owner)==0x70009f6ae0ull;}
    return 0;
}
static void catherine_walking_begin(CatherineWalkingSample *sample,uint64_t owner,uint64_t caller) {
    memset(sample,0,sizeof *sample);sample->owner=owner;sample->caller=caller;
    if((caller!=0x70000b7d40ull&&caller!=0x70000cce50ull)||!catherine_high_refresh_active()||
       (LD8(0x7000d06360ull)&1u)||owner<0x10ull){catherine_walking_invalidate(owner);return;}
    if(!catherine_walking_owner(owner,caller)){catherine_walking_invalidate(owner);return;}
    if(LD32(owner+0xa0)!=0x40400000u||LD32(owner+0xa4)!=0x40000000u||LD32(owner+0xa8)!=0x40400000u){catherine_walking_invalidate(owner);return;}
    uint32_t dt_bits=LD32(0x70031cc1e8ull);float dt;memcpy(&dt,&dt_bits,4);
    sample->epoch=(uint32_t)(catherine_announcement_frame_clock()>>32);
    if(!isfinite(dt)||dt<0.f||dt>.1f||!sample->epoch){catherine_walking_invalidate(owner);return;}
    sample->wrapper=LD64(owner+0x10);if(!sample->wrapper){catherine_walking_invalidate(owner);return;}
    sample->root_class=LD64(owner-0x10);sample->child_class=LD64(owner);sample->wrapper_class=LD64(sample->wrapper);
    for(unsigned i=0;i<3;i++){
        sample->before[i]=LD32(owner+0x30+4*i);uint32_t target=LD32(owner+0x50+4*i);float a,t;
        memcpy(&a,&sample->before[i],4);memcpy(&t,&target,4);if(!isfinite(a)||!isfinite(t)){catherine_walking_invalidate(owner);return;}
    }
    CatherineWalkingHistory *history=0;
    for(unsigned i=0;i<4;i++)if(catherine_walking_histories[i].owner==owner){history=&catherine_walking_histories[i];break;}
    if(!history){for(unsigned i=0;i<4;i++)if(!catherine_walking_histories[i].valid){history=&catherine_walking_histories[i];break;}}
    if(!history){history=&catherine_walking_histories[0];for(unsigned i=1;i<4;i++)if((uint32_t)(sample->epoch-catherine_walking_histories[i].epoch)>(uint32_t)(sample->epoch-history->epoch))history=&catherine_walking_histories[i];}
    sample->history=history;sample->tick=catherine_timing_tick();uint32_t phase_bits=(uint32_t)catherine_camera_render_clock()&0x7fffffffu;
    memcpy(&sample->phase,&phase_bits,4);if(!isfinite(sample->phase)||sample->phase<0.f||sample->phase>1.f)sample->phase=0.f;
    sample->enabled=1;
}
static int catherine_walking_identity(const CatherineWalkingSample *sample) {
    if(!catherine_walking_owner(sample->owner,sample->caller))return 0;
    if(LD64(sample->owner-0x10)!=sample->root_class||LD64(sample->owner)!=sample->child_class||LD64(sample->owner+0x10)!=sample->wrapper)return 0;
    return LD64(sample->wrapper)==sample->wrapper_class;
}
static void catherine_walking_stage(CatherineWalkingSample *sample) {
    if(!sample->enabled)return;
    CatherineWalkingHistory *history=sample->history;uint32_t next[3];unsigned finite=1;
    for(unsigned i=0;i<3;i++){next[i]=LD32(sample->owner+0x30+4*i);float n;memcpy(&n,&next[i],4);if(!isfinite(n))finite=0;sample->authoritative[i]=sample->tick?next[i]:sample->before[i];}
    if(!catherine_walking_identity(sample)){catherine_walking_invalidate(sample->owner);return;}
    if(sample->tick&&!finite){catherine_walking_invalidate(sample->owner);return;}
    unsigned matching=history->valid&&history->owner==sample->owner&&history->root_class==sample->root_class&&history->child_class==sample->child_class&&history->wrapper==sample->wrapper&&history->wrapper_class==sample->wrapper_class&&
        (uint32_t)(sample->epoch-history->epoch)<=1u&&!memcmp(history->current,sample->before,sizeof sample->before);
    if(!matching){memcpy(history->previous,sample->before,sizeof sample->before);memcpy(history->current,sample->before,sizeof sample->before);history->owner=sample->owner;history->root_class=sample->root_class;history->child_class=sample->child_class;history->wrapper=sample->wrapper;history->wrapper_class=sample->wrapper_class;history->model=0;history->model_valid=0;history->valid=1;}
    if(sample->tick){if(!matching||history->epoch!=sample->epoch)memcpy(history->previous,sample->before,sizeof sample->before);memcpy(history->current,next,sizeof next);}
    history->epoch=sample->epoch;
    for(unsigned i=0;i<3;i++){
        float a,n,v;memcpy(&a,&history->previous[i],4);memcpy(&n,&history->current[i],4);
        if(sample->phase<=0.f)v=a;else if(sample->phase>=1.f)v=n;else v=(float)((double)a+((double)n-a)*sample->phase);
        memcpy(&sample->displayed[i],&v,4);ST32(sample->owner+0x30+4*i,sample->authoritative[i]);
    }
    sample->staged=1;
}
static void catherine_walking_model_input(CatherineWalkingSample *sample,Cpu *c) {
    if(!sample->staged)return;
    if(!catherine_walking_identity(sample)){catherine_walking_invalidate(sample->owner);return;}
    for(unsigned i=0;i<3;i++)if(LD32(sample->owner+0x30+4*i)!=sample->authoritative[i]){catherine_walking_invalidate(sample->owner);return;}
    /* The exact 9FA20/X2=1 setter reads only +10/+14/+18 before replacing X1.
       Keep actor coordinates authoritative even inside native model services. */
    if(c->x[1]!=sample->owner+0x20ull||(uint32_t)c->x[2]!=1u){catherine_walking_invalidate(sample->owner);return;}
    CatherineWalkingHistory *history=sample->history;
    if(!c->x[0]||history->owner!=sample->owner||!history->valid){catherine_walking_invalidate(sample->owner);return;}
    unsigned same_model=history->model_valid&&history->model==c->x[0];
    if(same_model)for(unsigned i=0;i<3;i++)if(LD32(c->x[0]+0x5d0+4*i)!=history->last_model_position[i])same_model=0;
    if(!same_model){
        memcpy(history->previous,sample->authoritative,sizeof sample->authoritative);memcpy(history->current,sample->authoritative,sizeof sample->authoritative);
        memcpy(sample->displayed,sample->authoritative,sizeof sample->authoritative);history->model=c->x[0];history->model_valid=0;
    }
    memcpy(sample->descriptor,(const void *)(uintptr_t)c->x[1],sizeof sample->descriptor);
    for(unsigned i=0;i<3;i++)memcpy(sample->descriptor+0x10+4*i,&sample->displayed[i],4);
    c->x[1]=(uint64_t)(uintptr_t)sample->descriptor;
    sample->resolved_model=c->x[0];sample->prepared=1;
}
static void catherine_walking_model_published(CatherineWalkingSample *sample) {
    if(!sample->prepared)return;
    CatherineWalkingHistory *history=sample->history;
    if(!catherine_walking_identity(sample)||!history->valid||history->owner!=sample->owner||history->model!=sample->resolved_model){catherine_walking_invalidate(sample->owner);return;}
    for(unsigned i=0;i<3;i++){
        uint32_t actual=LD32(sample->resolved_model+0x5d0+4*i);
        if(actual!=sample->displayed[i]){catherine_walking_invalidate(sample->owner);return;}
        history->last_model_position[i]=actual;
    }
    history->model_valid=1;
}
