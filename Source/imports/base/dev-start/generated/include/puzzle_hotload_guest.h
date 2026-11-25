#pragma once
#include "dev_loader_native.h"
void f_700022dcd0(Cpu*);void f_7000278010(Cpu*);
static uint64_t catherine_dev_puzzle_hotload_scene(uint64_t *dispatcher){
 const uint64_t holder=LD64(0x7000ccb640ull),task=holder?LD64(holder):0;
 const uint64_t expected=LD64(0x7000ccb638ull);
 if(!task||!expected||LD64(task)!=expected+0x10)return 0;
 const uint64_t wh=LD64(0x7000cca5d8ull),world=wh?LD64(wh):0;
 const uint64_t scene=world?LD64(world+0x668):0;
 if(!scene||LD64(scene+0x420)!=0x70002245a0ull||LD64(scene+0x428)!=task)return 0;
 if(dispatcher)*dispatcher=task;return scene;
}
/* Called only from the registered puzzle dispatcher f2216c0. Native f22dcd0
   owns its PauseAllowed/dead-actor/modal checks, actor pause and timer selector,
   then invokes the registered request callback: phase34 -> native preparation33. */
static void catherine_dev_puzzle_hotload_open(Cpu*c,uint64_t task){
 if(!0 /* direct root loader owns requests */)return;
 uint64_t actual=0;const uint64_t scene=catherine_dev_puzzle_hotload_scene(&actual);
 if(!scene||task!=actual||LD32(task+8)!=32)return;
 const Cpu saved=*c;c->x[0]=scene;f_700022dcd0(c);*c=saved;
 if(LD32(task+8)==34)catherine_dev_loader_progress(CATHERINE_DEV_WAIT_SAFE,"Opening the native puzzle Pause menu before loading.");
}
/* Called only at f21ab70 input entry. Match the scene's retained native Pause
   wrapper, a fully opened main menu, and absence of a child modal. DEV Load is
   the explicit accept operation. Exact native L21b17c stores request close3,
   style6; the unchanged animation/real result getter then emits ReturnTitle6.
   Story uses native row4; optional scene menus use logical action row6. */
static int catherine_dev_puzzle_hotload_accept(Cpu*c,uint64_t widget){
 if(!0 /* direct root loader owns requests */)return 0;
 uint64_t task=0;const uint64_t scene=catherine_dev_puzzle_hotload_scene(&task);
 if(!scene||LD32(task+8)!=33)return 0;
 const uint64_t h=LD64(scene+0x138);
 if(!h||LD64(h+0x10)!=widget||LD64(h+0x150)!=0x700021c940ull||LD64(h+0x160)!=0x700021c9b0ull)return 0;
 if(LD8(widget+0x34c)!=3||LD8(widget+0x34d)!=1||LD8(widget+0x34e)!=0xff||LD8(widget+0x34f)!=0xff||LD8(widget+0x340)!=0)return 0;
 const Cpu saved=*c;
 ST8(widget+0x18,LD8(widget+0x11)?6:4);
 ST8(widget+0x34f,3);ST8(widget+0x35c,6);
 c->x[0]=0xbb8;c->x[1]=0;c->x[2]=0;f_7000278010(c);*c=saved;
 catherine_dev_loader_progress(CATHERINE_DEV_WAIT_SAFE,"Native puzzle Return Title accepted; waiting for its close animation.");return 1;
}

/* Used only at the native actor Start-input test after its own mode/scene gates.
   f22dcd0 retains all native alive/PauseAllowed/modal checks and side effects. */
static int catherine_dev_puzzle_hotload_native_start(void){
 if(!0 /* direct root loader owns requests */)return 0;
 uint64_t task=0;if(!catherine_dev_puzzle_hotload_scene(&task)||LD32(task+8)!=32)return 0;
 const uint64_t registry=LD64(0x7000ccadf0ull),actor=registry?LD64(registry):0;
 return actor && !(LD32(actor+0x18c)&0x80000001u);
}
