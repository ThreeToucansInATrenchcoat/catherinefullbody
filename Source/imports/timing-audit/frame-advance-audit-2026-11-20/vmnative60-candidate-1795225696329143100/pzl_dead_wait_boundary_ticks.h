#pragma once
#include "timing_clock.h"
#include "pzl_dead_wait_profiles.h"
/* Exact165 authored Story flows whose root2 reaches pzl_normal_dead.
   Positive literal WAIT first/interior/expiry use the frozen native boundary epilogues.
   No runtime code/local/branch normalization; every complete BF keeps its own fingerprint. */
static inline int catherine_hold_death_boundary_wait(uint64_t vm){
 if(!catherine_high_refresh_active()||catherine_timing_tick()||!vm||LD32(vm+0x208)!=2)return 0;
 const char rootname[]="pzl_00_00_dead",callee_name[]="pzl_normal_dead";
 for(unsigned i=0;i<sizeof rootname;i++)if(LD8(vm+i)!=(uint8_t)rootname[i])return 0;
 const uint64_t flow=LD64(vm+0x1d0);if(!flow)return 0;
 if(LD32(flow+4)<208||LD32(flow+0x28)<3||LD32(flow+8)!=0x30574c46u||LD32(flow+0x10)!=5||
 LD32(flow+0x20)!=0||LD32(flow+0x24)!=32||LD32(flow+0x2c)!=112||
 LD32(flow+0x30)!=1||LD32(flow+0x34)!=32||LD32(flow+0x40)!=2||LD32(flow+0x44)!=4||
 LD32(flow+0x50)!=3||LD32(flow+0x54)!=1||LD32(flow+0x60)!=4||LD32(flow+0x64)!=1||LD32(flow+0x68)!=240||
 LD64(vm+0x1e0)!=flow+112)return 0;
 for(unsigned i=0;i<sizeof rootname;i++)if(LD8(flow+112+64+i)!=(uint8_t)rootname[i])return 0;
 const uint32_t pc=LD32(vm+0x18);if(!pc)return 0;
 uint64_t cached_hash=0;int hashed=0;
 for(unsigned j=0;j<sizeof catherine_death_wait_profiles/sizeof catherine_death_wait_profiles[0];j++){
  const CatherineDeathWaitProfile*p=&catherine_death_wait_profiles[j];
  if(LD32(flow+4)!=p->declared||LD32(flow+0x28)!=p->procs||LD32(flow+0x38)!=p->labels||
   LD32(flow+0x3c)!=p->labeloff||LD32(flow+0x48)!=p->words||LD32(flow+0x4c)!=p->code||
   LD32(flow+0x58)!=p->messagebytes||LD32(flow+0x5c)!=p->messageoffset||LD32(flow+0x6c)!=p->declared||
   LD64(vm+0x1f0)!=flow+p->code||LD32(flow+112+64+24)!=p->rootstart||pc>=p->words||
   pc<p->calleestart||pc>=p->calleeend)continue;
  unsigned k=0;while(k<8&&pc!=p->calleestart+catherine_death_wait_relative_pc[k])k++;
  if(k==8)continue;
  const uint64_t at=flow+p->code+4u*pc;
  if(LD32(at)!=0x00090008u||LD32(at-4)!=((catherine_death_wait_duration[k]<<16)|0x1du))continue;
  const uint64_t callee=flow+112+32u*p->callee;
  if(LD32(callee+24)!=p->calleestart)continue;
  unsigned i=0;while(i<sizeof callee_name&&LD8(callee+i)==(uint8_t)callee_name[i])i++;
  if(i!=sizeof callee_name)continue;
  /* Fixed profile bounds guarantee complete bounded message/full-flow reads.
     Native f686b0 changes this marker0->1; complete57812dump proved only that byte differs. */
  const uint64_t message=flow+p->messageoffset;
  if(p->messagebytes<12||p->messageoffset+p->messagebytes!=p->declared||p->size!=p->declared+240||
   LD32(message)!=0x12345678u||LD32(message+4)!=0x00010000u||LD8(message+8)||LD16(message+10)||LD8(message+9)>1)continue;
  if(!hashed){cached_hash=0xcbf29ce484222325ull;for(uint32_t n=0;n<p->size;n++){cached_hash^=n==p->messageoffset+9?0:LD8(flow+n);cached_hash*=0x100000001b3ull;}hashed=1;}
  if(cached_hash==p->fnv)return 1;
 }
 return 0;
}
