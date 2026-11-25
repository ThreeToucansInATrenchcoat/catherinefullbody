#pragma once
#include "cpu.h"
#include "timing_clock.h"
#include "puzzle_follow_sample.h"
uint64_t catherine_camera_render_clock(void);
/* Native orbit state remains in camera+A0. This cache is presentation only. */
static _Thread_local CatherinePuzzleFollowSample catherine_puzzle_orbit_history;
static void catherine_puzzle_orbit_reset(uint64_t owner) {
 if(catherine_puzzle_orbit_history.owner==owner)
  memset(&catherine_puzzle_orbit_history,0,sizeof catherine_puzzle_orbit_history);
}
static uint64_t catherine_puzzle_orbit_stage(uint64_t owner,uint64_t caller) {
 if(!catherine_high_refresh_active()||(LD8(0x7000d06360ull)&1u)||
    caller!=0x7000130580ull||!owner)return 0;
 uint64_t cell=LD64(0x7000ccaca0ull);
 if(!cell||LD64(cell)!=owner||LD64(owner)!=0x7000abff40ull||
    LD32(owner+0x34ull)!=0u||(LD32(owner+0x10ull)&0x800002u))return 0;
 uint64_t holder=LD64(0x7000cca5d8ull),manager=holder?LD64(holder):0;
 uint64_t stage=manager?LD64(manager+0x668ull):0;
 return stage&&(uint32_t)(LD32(stage+0x18ull)-1u)<10u&&LD8(stage+0x28ull)==0u?stage:0;
}
static void catherine_puzzle_orbit_begin(uint64_t owner,uint64_t stage) {
 CatherinePuzzleFollowSample*s=&catherine_puzzle_orbit_history;
 if(!stage){catherine_puzzle_orbit_reset(owner);return;}
 if(s->valid&&s->owner==owner) {
  uint32_t previous;memcpy(&previous,&s->current[0],4);
  /* External writes are discontinuities even when they occur on a tick. */
  if(s->stage!=stage||LD32(owner+0xa0ull)!=previous)
   catherine_puzzle_orbit_reset(owner);
 }
}
static unsigned catherine_puzzle_orbit_hold(uint64_t owner,uint64_t stage) {
 return stage&&catherine_puzzle_orbit_stage(owner,0x7000130580ull)==stage&&
  !catherine_timing_tick();
}
static uint32_t catherine_puzzle_orbit_present(uint64_t owner,uint64_t stage,unsigned*seen) {
 uint32_t row[3]={LD32(owner+0xa0ull),0,0};*seen=0;
 if(!stage||catherine_puzzle_orbit_stage(owner,0x7000130580ull)!=stage) {
  catherine_puzzle_orbit_reset(owner);return row[0];
 }
 if(catherine_puzzle_follow_sample(&catherine_puzzle_orbit_history,owner,stage,0,
     catherine_timing_tick(),catherine_camera_render_clock(),1,0,12.01f,row))*seen=1;
 return row[0];
}
