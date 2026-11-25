#pragma once
#include "dev_loader_native.h"
#include "karma-native.h"
#include "difficulty-native.h"
#include "puzzle_recipe_guest.h"
#include "supplemental_dev_levels.h"
#include "dev_world_guest.h"
#include "dev_movie_guest.h"
#include <stdio.h>
void f_700009d360(Cpu*); void f_700009bed0(Cpu*);
void f_700009c1a0(Cpu*); void f_7000026530(Cpu*);
void f_700009a380(Cpu*); void f_70005c8060(Cpu*);
void f_700009eba0(Cpu*); void f_700009dad0(Cpu*); void f_700009ec00(Cpu*);
/* One guest scheduler thread owns this transaction. Read and validate before
   retiring the old root; apply only after its destructor has finished. */
extern int catherine_dev_movie_active(void);
extern int catherine_dev_title_owned(void);
extern int catherine_dev_sleep_active(void);
static unsigned char catherine_dev_direct_record[0x8000];
static int catherine_dev_direct_staged;
static uint32_t catherine_dev_direct_target;
static int catherine_dev_direct_waiting;
static unsigned catherine_dev_direct_ticks;
static unsigned long long catherine_dev_direct_deadline;
static int catherine_dev_direct_prepare(Cpu *cpu) {
 if(catherine_dev_direct_staged)return 1;
 if(!catherine_dev_loader_pending())return 0;
 CatherineDevLoadStatus status={0};status.version=1;catherine_dev_loader_status(&status);
 if(!catherine_dev_optional_available(status.target_id)){catherine_dev_loader_finish(-48,"Two assigned input devices are required for local multiplayer.");return 0;}
 catherine_dev_direct_target=status.target_id;
 if(status.target_id!=40001 && catherine_dev_loader_target_kind(status.target_id)==CATHERINE_DEV_MOVIE){
  if(catherine_dev_loader_begin_movie(status.target_id)!=1)return 0;
  catherine_dev_direct_staged=1;
  return 1;
 }
 const int result=catherine_dev_loader_read_record(catherine_dev_direct_record,sizeof catherine_dev_direct_record);
 if(result!=1)return 0;
 Cpu scratch=*cpu;
 const uint64_t buffer=(scratch.sp-0x9000ull)&~15ull;
 memcpy((void*)(uintptr_t)buffer,catherine_dev_direct_record,sizeof catherine_dev_direct_record);
 scratch.sp=buffer-0x100;scratch.x[0]=buffer+0x48;f_7000026530(&scratch);
 if(!(scratch.x[0]&1)){
  catherine_dev_loader_finish(-14,"The native content entitlement check rejected this checkpoint.");return 0;
 }
 catherine_dev_direct_staged=1;return 1;
}
static void catherine_dev_direct_commit(Cpu *cpu) {
 if(!catherine_dev_direct_staged && !dev_standalone_retire_story)return;
 const int applying=catherine_dev_direct_staged;
 catherine_dev_direct_staged=0;
 dev_standalone_retire_story=0;
 Cpu scratch=*cpu;Cpu *c=&scratch;
 const uint64_t buffer=(c->sp-0x9000ull)&~15ull;
 c->sp=buffer-0x100;
 /* A puzzle/field root owns its scene, while the story VM remains suspended
    independently at d01060. Retire that native task too: otherwise the engine
    deliberately resumes it when the next destination uses the same BF file. */
 const uint64_t script=LD64(0x7000d01060ull),scheduler=LD64(0x7000cc9058ull);
 if(script&&scheduler){
  const uint64_t pair=buffer-0x40;
  ST64(pair,scheduler+0x10);ST64(pair+8,script);
  c->x[0]=pair;f_700009ec00(c);
  if(c->x[0]&1){c->x[0]=pair;f_700009eba0(c);}
  c->x[0]=pair;f_700009dad0(c);
 }
 if(!applying)return;
 f_700009bed0(c);const uint64_t config=c->x[0];
 catherine_dev_direct_waiting=0;
 if(catherine_dev_direct_target!=40001 && catherine_dev_loader_target_kind(catherine_dev_direct_target)==CATHERINE_DEV_MOVIE){
  catherine_dev_optional_apply(c,config,catherine_dev_direct_target);
  dev_standalone_start(catherine_dev_direct_target);
  return;
 }
 dev_standalone_target=0;
 memcpy((void*)(uintptr_t)buffer,catherine_dev_direct_record,sizeof catherine_dev_direct_record);
 ST8(config+0xce85,1);
 const uint64_t ui=LD64(0x7000cc9060ull);if(ui)ST8(ui+0x29,1);
 c->x[0]=config+0x9818;c->x[1]=buffer+0x48;c->x[2]=0x3634;f_70005c8060(c);
 int32_t enabled=0,karma=0,readback=0,difficulty=-1;
 int result=-30;
 if(!catherine_dev_loader_karma(&enabled,&karma))goto error;
 result=catherine_dev_karma_apply(c,config,enabled,karma,&readback);if(result<0)goto error;
 result=-40;if(!catherine_dev_loader_difficulty(&difficulty))goto error;
 result=catherine_dev_difficulty_apply(config,difficulty,&readback);if(result<0)goto error;
 result=catherine_dev_puzzle_restore(c,config);if(result<0)goto error;
 catherine_dev_optional_apply(c,config,catherine_dev_direct_target);
 const uint32_t elapsed=LD32(config+0xce40);
 f_700009c1a0(c);ST32(c->x[0]+0x7c,elapsed);
 c->x[0]=config;c->x[1]=0x201;c->x[2]=1;f_700009a380(c);
 if(catherine_dev_direct_target==40001){dev_standalone_start(40001);return;}
 catherine_dev_direct_waiting=1;catherine_dev_direct_ticks=0;catherine_dev_direct_deadline=GetTickCount64()+120000;
 catherine_dev_loader_progress(CATHERINE_DEV_TRANSITION,"Checkpoint restored directly; starting the requested scene.");
 fprintf(stderr,"[dev-direct] committed checkpoint, stage=%u time=%u\n",LD32(config+0x9b60),LD32(config+0x9b64));
 return;
error:
 catherine_dev_loader_finish(result,"Direct checkpoint restore failed native validation.");
}
static void catherine_dev_direct_ready(uint64_t root){
 if(!catherine_dev_direct_waiting||catherine_dev_optional_root(catherine_dev_direct_target)!=11)return;
 CatherineDevLoadStatus status={0};status.version=1;catherine_dev_loader_status(&status);
 if(!status.busy||status.phase!=CATHERINE_DEV_TRANSITION||status.target_id!=catherine_dev_direct_target){catherine_dev_direct_waiting=0;return;}
 if(GetTickCount64()>=catherine_dev_direct_deadline){catherine_dev_direct_waiting=0;catherine_dev_loader_finish(-51,"The requested scene did not become ready within two minutes.");return;}
 ++catherine_dev_direct_ticks;
 const int32_t current=(int32_t)LD32(root+4);
 if(LD8(root)&1)return;
 const uint64_t holder=LD64(0x7000ccb640ull),task=holder?LD64(holder):0;
 int ready=!catherine_dev_movie_active()&&(current==3||current==5)&&task&&LD32(task+8)==32;
 if(!ready)ready=catherine_dev_world_ready(catherine_dev_direct_target,current);
 if(ready){catherine_dev_direct_waiting=0;catherine_dev_loader_finish(0,"Requested level is ready.");fprintf(stderr,"[dev-direct] ready root=%d ticks=%u\n",current,catherine_dev_direct_ticks);}
}
static void catherine_dev_direct_enqueue(Cpu *cpu,uint32_t target) {
 if(target!=40001 && catherine_dev_loader_target_kind(target)==CATHERINE_DEV_MOVIE)dev_standalone_enqueue(cpu,target);
 else catherine_dev_optional_enqueue(cpu,target);
}
static int catherine_dev_direct_tick(Cpu *cpu,uint64_t root) {
 if(!catherine_dev_direct_staged && !(LD8(root)&1) && catherine_dev_loader_adopt_next()){
  catherine_dev_direct_waiting=0;
  catherine_optional_target=0;
 }
 if(dev_standalone_tick(cpu,root))return 1;
 catherine_dev_optional_tick(cpu,root);
 catherine_dev_direct_ready(root);
 if(!catherine_dev_loader_is_bootstrapped()||!catherine_dev_loader_pending()||(LD8(root)&1))return 0;
 /* Movie shutdown must finish through the native decoder/task owner. */
 const int32_t current=(int32_t)LD32(root+4);
 if(current==1||current==13||catherine_dev_movie_active()||catherine_dev_sleep_active())return 0;
 if(current==2&&catherine_dev_title_owned())return 0;
 if(!catherine_dev_direct_prepare(cpu))return 0;
 Cpu scratch=*cpu;
 fprintf(stderr,"[dev-direct] leaving root %d for game sequence\n",current);
 catherine_dev_direct_enqueue(&scratch,catherine_dev_direct_target);return 1;
}

int catherine_dev_direct_boot_ready(Cpu *c){
 if(!catherine_dev_loader_bootstrap_ready())return 0;
 if(catherine_dev_loader_pending()&&!catherine_dev_direct_prepare(c))return !catherine_dev_loader_pending();
 return 1;
}
int catherine_dev_direct_title_prepare(Cpu *c){
 return catherine_dev_loader_is_bootstrapped()&&catherine_dev_direct_prepare(c);
}
int catherine_dev_direct_title_dispatch(Cpu *c){
 if(!catherine_dev_direct_staged)return 0;
 catherine_dev_direct_enqueue(c,catherine_dev_direct_target);return 1;
}
