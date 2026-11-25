#pragma once
#include "puzzle_recipe_host.h"
#include "dev_loader_native.h"
#include "entry-descriptors.h"
#include "recipe_flow_fingerprint.h"
#include <stdint.h>
#include <stdio.h>
void f_70000779e0(Cpu *);
void f_7000077a20(Cpu *);
static inline int catherine_dev_puzzle_name(uint64_t p,const char *name){
 for(unsigned i=0;i<13;i++)if(LD8(p+i)!=(uint8_t)name[i])return 0;
 return 1;
}
/* Stage10 is a supplied Rin fixture, not fabricated ordinary progression.
   Ordinary recipes retain their original flag6-clear rule. */
static inline int catherine_dev_puzzle_route_matches(uint64_t config,const CatherineDevPuzzleEntryProfile *p){
 const int rin=(LD8(config+0x9818)&64)!=0;
 return p->stage==10 ? rin&&LD32(config+0x998c)==60 : !rin;
}
/* Called on the guest main thread, after the coherent earned config/karma copy.
   Exact native SET_TIME and GET_TIME are synchronous and do not yield. */
static int catherine_dev_puzzle_restore(Cpu *c,uint64_t config){
 catherine_dev_puzzle_cancel();
 uint32_t request=0;int32_t recipe=0;
 if(!catherine_dev_loader_recipe(&request,&recipe))return -40;
 if(recipe==0)return 1;
 const CatherineDevPuzzleEntryProfile *profile=catherine_dev_puzzle_first_profile((uint32_t)recipe);
 if(!profile||!config||LD32(config+0x9b60)!=profile->stage||
    !catherine_dev_puzzle_route_matches(config,profile)||LD32(config+0x9b58)>3||LD32(config+0x9b5c)>1||
    (LD8(config+0xce5c)&&LD32(config+0xce64)!=LD32(config+0x9b5c)))return -41;
 const uint64_t holder=LD64(0x7000cc9f60ull);
 if(!holder||LD64(holder)!=config)return -41;
 const Cpu saved=*c;
 const uint64_t vm=(saved.sp-0x280ull)&~15ull;
 const uint64_t old_current=LD64(0x7003195c88ull);
 /* A private native argument frame; no bytecode is dispatched or resources made. */
 memset((void*)(uintptr_t)vm,0,0x250);
 ST32(vm+0x1c,1);ST64(vm+0x50,profile->time);ST64(0x7003195c88ull,vm);
 c->sp=vm-0x20;f_70000779e0(c);f_7000077a20(c);
 const int valid=(uint32_t)c->x[0]==1&&LD64(vm+0x1c8)==profile->time&&LD32(config+0x9b64)==profile->time;
 ST64(0x7003195c88ull,old_current);*c=saved;
 if(!valid)return -42;
 if(!catherine_dev_puzzle_arm(request,config))return -43;
 return 1;
}
/* Main17's sole added hook. A single closed atomic read is the ordinary cost.
   Native root11 retains the script task handle at d01060 in f78700. */
static inline void catherine_dev_puzzle_before_vm(Cpu *c,uint64_t vm){
 (void)c;
 if(!catherine_dev_puzzle_pending())return;
 catherine_dev_puzzle_poll();
 const uint32_t recipe=catherine_dev_puzzle_armed_recipe();
 const CatherineDevPuzzleEntryProfile *first=catherine_dev_puzzle_first_profile(recipe);
 if(!first||!vm||!catherine_dev_puzzle_name(vm,first->vm))return;
 const uint64_t handle=LD64(vm+0x238),retained=LD64(0x7000d01060ull);
 if(!handle||handle!=retained||LD64(handle+0x10)!=vm||
    LD64(handle+0x150)!=0x70002729b0ull||LD64(handle+0x160)!=0x7000272a10ull){
  catherine_dev_puzzle_error(-44,"Puzzle story task does not match the native retained owner.");return;
 }
 const uint64_t holder=LD64(0x7000cc9f60ull),config=holder?LD64(holder):0;
 if(!config||LD32(config+0x9b60)!=first->stage||LD32(config+0x9b64)!=first->time||
    !catherine_dev_puzzle_route_matches(config,first)||LD32(config+0x9b58)>3||LD32(config+0x9b5c)>1||
    (LD8(config+0xce5c)&&LD32(config+0xce64)!=LD32(config+0x9b5c))||
    LD32(vm+0x208)!=0||LD32(vm+0x18)!=0||LD32(vm+0x1c)!=0||LD32(vm+0x214)!=0){
  catherine_dev_puzzle_error(-44,"Puzzle entry is not a fresh matching native story procedure and route state.");return;
 }
 const uint64_t flow=LD64(vm+0x1d0);
 if(!flow||LD32(flow+8)!=0x30574c46u||LD32(flow+0x10)!=5||
    LD32(flow+0x20)!=0||LD32(flow+0x24)!=32||LD32(flow+0x28)!=1||
    LD32(flow+0x2c)!=0x70||LD32(flow+0x30)!=1||LD32(flow+0x34)!=32||
    LD32(flow+0x40)!=2||LD32(flow+0x44)!=4||LD64(vm+0x1e0)!=flow+0x70||
    !catherine_dev_puzzle_name(flow+0x70,first->vm)||LD32(flow+0x88)!=0){
  catherine_dev_puzzle_error(-44,"Puzzle story script layout differs from the verified native assets.");return;
 }
 const CatherineDevPuzzleEntryProfile *matched=0;
 const uint32_t canonical=catherine_dev_puzzle_canonical_recipe(recipe);
 for(unsigned k=0;k<sizeof(catherine_dev_puzzle_entry_profiles)/sizeof(catherine_dev_puzzle_entry_profiles[0]);k++){
  const CatherineDevPuzzleEntryProfile *p=&catherine_dev_puzzle_entry_profiles[k];
  if(p->recipe!=canonical||LD32(flow+4)!=p->declared||LD32(flow+0x38)!=p->labels||
     LD32(flow+0x3c)!=p->labeloff||LD32(flow+0x48)!=p->words||LD32(flow+0x4c)!=p->code||
     LD64(vm+0x1e8)!=flow+p->labeloff||LD64(vm+0x1f0)!=flow+p->code)continue;
  uint64_t hash=0;
  if(catherine_dev_recipe_flow_fingerprint(flow,p->size,p->declared,p->code,p->words,&hash)&&hash==p->fnv){matched=p;break;}
 }
 if(!matched){
  catherine_dev_puzzle_error(-44,"Puzzle story script differs from every verified current asset profile.");return;
 }
 uint32_t entry=matched->entry;
 int first_nightmare=0;
 if(recipe==10101){
  CatherineDevLoadStatus status={0};
  status.version=CATHERINE_DEV_LOADER_VERSION;catherine_dev_loader_status(&status);
  first_nightmare=status.target_id==40001;
  if(first_nightmare){
   /* All five fingerprinted story_day_01 files enter CALL_FALLASLEEP at PC8
      before supplying the arguments to CALL_PUZZLE at PC9..12. */
   if(entry!=9||LD32(flow+matched->code+8*4)!=0x30010008u){
    catherine_dev_puzzle_error(-44,"The first nightmare opening differs from the verified script.");return;
   }
   entry=8;
  }
 }
 /* Claim consumes the exact host request before changing the VM; never rearm. */
 if(!catherine_dev_puzzle_claim(config))return;
 ST32(vm+0x18,entry);
 char message[160];
 if(first_nightmare)snprintf(message,sizeof message,"Playing the first nightmare opening.");
 else snprintf(message,sizeof message,"Native stage %u floor %u story entry accepted; gameplay is initializing.",matched->stage,matched->floor);
 catherine_dev_loader_progress(5,message);
 /* The root monitor completes only once native gameplay accepts input. */
}
