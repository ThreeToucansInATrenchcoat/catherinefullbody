#pragma once
#include "dev_loader_native.h"
void f_70000b3ba0(Cpu*); void f_70000ace60(Cpu*);
/* Guest callback only. Exact active field singleton and constructed state identities;
   no host/UI invocation and no direct scene teardown or root creation. */
static uint64_t catherine_dev_world_identity(uint64_t owner){
 const uint64_t h=LD64(0x7000cca2c8ull),field=h?LD64(h):0;
 if(!field||owner!=field+0x80||LD64(owner)!=0x70009f74b0ull||
    LD64(owner+0x1f0)!=0x70009f7090ull||LD64(owner+0x210)!=0x70009f70d8ull)return 0;
 return field;
}
/* Insert only at native idle fB3950 L_B3b14, immediately before its Start input
   branch. Native queue action3 is the same path as ordinary Start/Escape. */
static int catherine_dev_world_queue_pause(Cpu*c,uint64_t owner){
 if(!0 /* direct root loader owns requests */||!catherine_dev_world_identity(owner))return 0;
 const uint64_t first=LD64(owner+0x588),end=LD64(owner+0x590),limit=LD64(owner+0x598);
 if(!first||end!=first+4||end>limit||LD32(first)!=1||LD64(owner+0x5a0)!=owner+0x1f0)return 0;
 const Cpu saved=*c;c->x[0]=owner+0x568;c->x[1]=3;f_70000b3ba0(c);*c=saved;
 catherine_dev_loader_progress(CATHERINE_DEV_WAIT_SAFE,"Returning through the native world Pause menu.");return 1;
}
/* Insert only at native Pause fB3f40 L_B3f7c. DEV Load itself authorizes leaving
   the current scene. Clear the ordinary Pause resources using its exact native
   destructor, then enter the existing accepted-selection state1. Fade, queued
   action22, world cleanup and fC0000's native root2 request remain unchanged.
   Existing Config/Help/confirmation child states are allowed to finish normally. */
static int catherine_dev_world_accept_return(Cpu*c,uint64_t pause,uint64_t owner){
 if(!0 /* direct root loader owns requests */)return 0;
 const uint64_t field=catherine_dev_world_identity(owner);
 if(!field||pause!=owner+0x210||LD32(pause+8)!=0||LD64(owner+0x5a0)!=pause)return 0;
 const uint64_t menu=field+0x7688;
 if(LD32(menu+8)!=0||LD32(menu+0x28)!=1)return 0;
 const Cpu saved=*c;
 ST32(menu+0x290,0);c->x[0]=menu;f_70000ace60(c);*c=saved;
 ST64(pause+8,1);
 catherine_dev_loader_progress(CATHERINE_DEV_WAIT_SAFE,"Native Return Title accepted; waiting for world cleanup.");return 1;
}

/* Native seated interaction state7/substate5 has its own Start cleanup. This
   predicate is used only after its ordinary interaction poll and updates reach
   L_B4ddc; it enters the exact native accepted-Start branch, not a direct push. */
static int catherine_dev_world_seated_pause_ready(uint64_t state,uint64_t owner){
 if(!0 /* direct root loader owns requests */||!catherine_dev_world_identity(owner))return 0;
 if(state!=owner+0x318||LD64(state)!=0x70009f7168ull||LD32(state+8)!=5||LD64(owner+0x5a0)!=state)return 0;
 const uint64_t first=LD64(owner+0x588),end=LD64(owner+0x590),limit=LD64(owner+0x598);
 return first&&end==first+4&&end<limit&&LD32(first)==7;
}
