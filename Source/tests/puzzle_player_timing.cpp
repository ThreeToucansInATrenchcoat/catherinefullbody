#include <cstdint>
#include <unordered_map>
#include <cassert>
#include <iostream>
static std::unordered_map<uint64_t,uint64_t> memory;
#define LD64(a) memory[uint64_t(a)]
#define LD32(a) uint32_t(memory[uint64_t(a)])
#define LD8(a) uint8_t(memory[uint64_t(a)])
#include "puzzle_action_ticks.h"
static unsigned tick=0;
unsigned catherine_timing_tick(){return tick;}
int main(){
 constexpr uint64_t p1=0x10000,p2=0x20000,npc=0x30000,registry=0x40000;
 memory[0x7000ccadf0]=registry;memory[registry]=p1;memory[registry+8]=p2;
 for(auto a:{p1,p2,npc}){memory[a]=0x7000abff90;memory[a+0x18c]=0x10001;}
 memory[p2+0xe0]=memory[npc+0xe0]=1;
 for(auto a:{p1,p2}){
  assert(catherine_puzzle_player_timing(a));
  assert(catherine_hold_puzzle_action_step(a,1));
  assert(!catherine_hold_puzzle_action_step(a,2));
  tick=1;assert(!catherine_hold_puzzle_action_step(a,1));tick=0;
  memory[a+0x190]=0x10000000;memory[a+0x18c]=0x281;
  assert(catherine_hold_puzzle_action_step(a,1));
 }
 assert(!catherine_puzzle_player_timing(npc));assert(!catherine_hold_puzzle_action_step(npc,1));
 memory[registry+8]=0;assert(!catherine_puzzle_player_timing(p2));
 memory[registry+8]=p2;memory[p2]=0;assert(!catherine_puzzle_player_timing(p2));
 memory[p2]=0x7000abff90;memory[p2+0xe0]=2;assert(!catherine_puzzle_player_timing(p2));
 std::cout<<"Both puzzle players share authored movement ticks; unrelated NPCs and stale slots excluded.\n";
}
