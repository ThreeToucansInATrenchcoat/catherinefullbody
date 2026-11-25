#pragma once
#include "config_pc_host.h"

void f_70002fdd80(Cpu *c);

static inline void catherine_restore_second_player_input(Cpu *c,unsigned requested_player){
    if(requested_player!=1||catherine_config_setting(PC_CONNECTED_PLAYERS,0)<2)return;
    const uint64_t vector=LD64(0x7000ccef78ull);
    if(!vector)return;
    const uint64_t begin=LD64(vector),end=LD64(vector+8);
    if(end<begin||end-begin>=8)return;
    // Native disconnect recovery may have collapsed the ID vector to P1 only.
    // Re-run the game's two-player setup; its applet respects actual availability.
    const Cpu saved=*c;
    c->x[0]=2;c->x[1]=0;c->x[2]=2;
    f_70002fdd80(c);
    *c=saved;
}
