// Native menu-scope regression: the selectors exist before a puzzle world.
#include <cstdint>
#include <unordered_map>
#include <cassert>
#include <iostream>
static std::unordered_map<uint64_t,uint64_t> memory;
#define LD64(a) memory[uint64_t(a)]
#define LD32(a) uint32_t(memory[uint64_t(a)])
#include "menu_input_scope.h"
int catherine_config_pc_is_open(void){return 0;}
int catherine_config_setting(unsigned,int){return 0;}
int main(){
 // Empty application root: native optional selectors, not a loaded world.
 memory[0x7000cc9f98]=0x1000;memory[0x1000]=0x2000;
 memory[0x2048]=0x3000;
 assert(catherine_input_main_menu_active());
 memory[0x7000cced98]=0x4000;memory[0x5eb0]=0x6000;
 memory[0x6030]=0x700009dfc0;memory[0x6048]=0x7000;
 memory[0x6078]=0x6000; // Native cyclic list must terminate.
 memory[0x7000]=0x70009f6ab8;memory[0x7010]=0x8000;
 memory[0x8000]=0x7000aa5be8;
 for(unsigned mode: {1u,4u}){
  memory[0x8650]=mode;
  assert(catherine_input_two_player_selector_active());
  assert(!catherine_input_main_menu_active());
 }
 memory[0x8650]=0;assert(catherine_input_main_menu_active()); // Babel Solo
 memory[0x8000]=0;memory[0x8650]=1;
 assert(catherine_input_main_menu_active()); // Reject other coroutine owners.
 memory[0x5eb0]=0;assert(catherine_input_main_menu_active()); // Exit selector.
 memory[0x5eb0]=0x6000;memory[0x6078]=0x6100;memory[0x6178]=0x6100;
 assert(catherine_input_main_menu_active()); // Bounded malformed list.
 std::cout<<"Pair/Colosseum ownership, Solo, menu return and bounded traversal passed.\n";
}
