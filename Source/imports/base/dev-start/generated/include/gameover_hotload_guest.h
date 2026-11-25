#pragma once
#include "dev_loader_native.h"
static int catherine_dev_gameover_ptr(uint64_t p){return p>=0x10000ull&&p<0x800000000000ull&&!(p&7);}
/* Called only after the native game-over panel's readiness tests. No request
   changes ordinary input; the native close/confirmation/result path stays live. */
static int catherine_dev_gameover_identity(uint64_t panel,unsigned phase){
 if(!0 /* direct root loader owns requests */||!catherine_dev_gameover_ptr(panel)||LD64(panel)!=0x7000ac8580ull||LD32(panel+0x30)!=phase)return 0;
 const uint64_t h=LD64(0x7000ccb640ull),task=h?LD64(h):0,vt=LD64(0x7000ccb638ull);
 if(!task||!vt||LD64(task)!=vt+0x10||LD32(task+8)!=46)return 0;
 const uint64_t wh=LD64(0x7000cca5d8ull),world=wh?LD64(wh):0,scene=world?LD64(world+0x668):0;
 if(!scene||LD64(scene+0x420)!=0x70002245a0ull||LD64(scene+0x428)!=task)return 0;
 // Ordinary story table only: native row1 is Return Title action6. Do not
 // interpret optional-mode Retry/StageSelect rows using Story semantics.
 if(LD32(panel+0x3c)!=0||(LD32(panel+0x38)&4)||LD16(0x70007cc704ull+12)!=6)return 0;
 const uint64_t context=LD64(0x7000cced98ull);if(!context)return 0;
 uint64_t node=LD64(context+0x1eb0);unsigned matches=0,steps=0;
 while(node&&steps++<512){if(!catherine_dev_gameover_ptr(node))return 0;
  if(LD64(node+0x30)==0x700009dfc0ull){const uint64_t wrapper=LD64(node+0x48);
   if(catherine_dev_gameover_ptr(wrapper)&&LD64(wrapper+0x150)==0x70001e8860ull&&LD64(wrapper+0x160)==0x70001e9890ull&&LD64(wrapper+0x10)==panel)++matches;}
  node=LD64(node+0x78);
 }
 return !node&&matches==1;
}
static int catherine_dev_gameover_choose_title(uint64_t panel){
 if(!catherine_dev_gameover_identity(panel,3))return 0;
 ST32(panel+0x54,1);ST32(panel+0x58,0);
 catherine_dev_loader_progress(CATHERINE_DEV_WAIT_SAFE,"Selecting native Game Over Return Title.");return 1;
}
static int catherine_dev_gameover_accept_title(uint64_t panel){
 if(!catherine_dev_gameover_identity(panel,5)||LD32(panel+0x54)!=1||LD32(panel+0x58)!=0)return 0;
 // Native ReturnTitle Yes/No uses80+84. Zero is Yes; nativeL91b8 handles
 // its selection sound, close animation, fade, cleanup and actual result.
 ST32(panel+0x80,0);ST32(panel+0x84,0);
 catherine_dev_loader_progress(CATHERINE_DEV_WAIT_SAFE,"Native Game Over Return Title confirmed; waiting for cleanup.");return 1;
}
