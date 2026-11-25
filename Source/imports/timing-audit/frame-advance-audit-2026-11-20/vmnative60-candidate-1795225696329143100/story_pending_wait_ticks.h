#pragma once
#include "timing_clock.h"
#include <stdint.h>
// Scratch only: exact actual-pak story09/story10 identities; native pending WAIT only.
static inline int catherine_story_pending_bounds(uint32_t pending,int32_t duration){
 return duration>0 && pending>0 && pending<(uint32_t)duration;
}
static inline int catherine_hold_story_pending_wait(uint64_t vm){
 if(!catherine_high_refresh_active() || catherine_timing_tick() || !vm)return 0;
 if(LD32(vm+0x208)!=0)return 0;
 const uint64_t flow=LD64(vm+0x1d0);
 if(!flow || LD32(flow+8)!=0x30574c46u || LD32(flow+0x10)!=5 ||
    LD32(flow+0x20)!=0 || LD32(flow+0x24)!=32 || LD32(flow+0x28)!=1 || LD32(flow+0x2c)!=0x70 ||
    LD32(flow+0x40)!=2 || LD32(flow+0x44)!=4 || LD32(flow+0x88)!=0)return 0;
 unsigned kind=2;
 const char *names[2]={"story_day_09","story_day_10"};
 for(unsigned k=0;k<2;k++){
  unsigned i=0;while(i<13 && LD8(vm+i)==(uint8_t)names[k][i] && LD8(flow+0x70+i)==(uint8_t)names[k][i])i++;
  if(i==13){kind=k;break;}
 }
 if(kind==2)return 0;
 static const uint32_t declared[2]={0x1d9c,0xd84},offsets[2]={0x830,0x370},words[2]={0x55b,0x285};
 if(LD32(flow+4)!=declared[kind] || LD32(flow+0x48)!=words[kind] || LD32(flow+0x4c)!=offsets[kind] ||
    LD64(vm+0x1e0)!=flow+0x70 || LD64(vm+0x1f0)!=flow+offsets[kind])return 0;
 const uint32_t pc=LD32(vm+0x18);
 if(!pc || pc>=words[kind])return 0;
 const uint64_t instruction=flow+offsets[kind]+4u*pc;
 if(LD32(instruction)!=0x00090008u || (LD32(instruction-4)&0xffffu)!=0x1du)return 0;
 const int32_t duration=(int16_t)(LD32(instruction-4)>>16);
 if(!catherine_story_pending_bounds(LD32(vm+0x214),duration))return 0;
 // Flow data is immutable native BF content, including its trailing0xf0 string bytes.
 // Bounded content fingerprint prevents a same-name/layout edited script from entering.
 static const uint64_t fingerprints[2]={0x9bd37dd542581ec8ull,0x50de60dae913c2a0ull};
 uint64_t hash=0xcbf29ce484222325ull;
 for(uint32_t i=0;i<declared[kind]+0xf0;i++){hash^=LD8(flow+i);hash*=0x100000001b3ull;}
 return hash==fingerprints[kind];
}
