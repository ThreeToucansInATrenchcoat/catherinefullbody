#include "catherine_settings_win.h"
#include "player_input_policy.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==2);auto expected=std::strtoull(argv[1],nullptr,16);auto s=catherine::host_settings();
 assert(s.bindings==expected&&s.player2.bindings==expected);
 assert(s.keyboard==catherine::kDefaultKeyboard&&s.player2.keyboard==catherine::kDefaultKeyboard);
 for(unsigned bit=0;bit<20;++bit){uint64_t input=1ull<<bit;assert(catherine::remap_pad_buttons(input)==catherine::remap_input_buttons(input,expected));}
 assert(catherine::remap_input_buttons(1,catherine::kDefaultBindings)==2);
 assert(catherine::remap_input_buttons(2,catherine::kDefaultBindings)==1);
 nx::plat::PadState keyboard{};keyboard.buttons=1;assert(catherine::menu_input_pad(keyboard,{},catherine::kDefaultBindings,false).buttons==1);
 nx::plat::PadState pad{};pad.buttons=2;assert(catherine::menu_input_pad({}, {pad},catherine::kDefaultBindings,false).buttons==1);
 std::cout<<"Both players, controller remapping, keyboard separation and settings load passed.\n";
}
