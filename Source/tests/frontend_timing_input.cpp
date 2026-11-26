#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <iostream>
#include "config_pc_host.h"
static bool high=true;
int catherine_high_refresh_active(){return high;}
#include "menu_timing.h"
struct Cpu {uint64_t x[32]{},pc=0;};
static std::unordered_map<uint64_t,uint64_t> memory;
#define LD64(a) memory[uint64_t(a)]
#define LD32(a) uint32_t(memory[uint64_t(a)])
#define ST64(a,v) (memory[uint64_t(a)]=v)
void rt_dispatch(Cpu*);
static int mouse[8]{};
int catherine_pc_mouse(unsigned f){return mouse[f];}
int catherine_multiplayer_available(){return 1;}
int catherine_config_setting(unsigned,int){return 0;}
#include "piano_mouse_pc.h"
static uint32_t bits(float v){uint32_t b;std::memcpy(&b,&v,4);return b;}
int main(){
 for(unsigned rate:{30u,60u,120u,240u,1000u,2000u,10000u}){
  high=rate>30;int scaled=0;float total=0;
  for(unsigned i=0;i<rate;++i)total+=catherine_menu_frame_step(bits(1.f/rate),&scaled);
  assert(std::abs(total-30.f)<.003f);
 }
 int scaled=0;high=true;assert(catherine_menu_frame_step(bits(0),&scaled)==0);
 assert(catherine_menu_frame_step(bits(-1),&scaled)==1);
 constexpr uint32_t confirm=1u<<13,back=1u<<14;
 memory[0x7000cc9070]=0x2000;mouse[4]=1;
 auto poll=[&](unsigned page,uint32_t held,uint32_t pressed){memory[0x2000]=held;catherine_piano_mouse_input(0x1000,page==0xb4?3:6,page,1,&pressed);return pressed;};
 memory[0x2000]=0;catherine_piano_mouse_begin(0x1000);poll(0xb0,0,0);
 assert(poll(0xb0,confirm,confirm)&confirm); // First controller/keyboard press.
 assert(!(poll(0xb4,confirm,confirm)&confirm)); // Same hold on destination.
 assert(!(poll(0xb4,confirm,confirm)&confirm)); // Native repeat still blocked.
 assert(!(poll(0xb4,0,confirm)&confirm)); // Release frame is consumed.
 assert(poll(0xb4,confirm,confirm)&confirm); // Fresh controller/keyboard press.
 assert(!(poll(0xb0,back,back)&back));
 poll(0xb0,0,0);assert(poll(0xb0,back,back)&back);
 memory[0x2000]=0;catherine_piano_mouse_begin(0x1000);poll(0xb0,0,0);
 mouse[0]=300;mouse[1]=400;mouse[7]=1;++mouse[2];++mouse[3];
 assert(poll(0xb0,confirm,confirm)&confirm); // New mouse click confirms once.
 assert(!(poll(0xb4,confirm,confirm)&confirm));
 assert(!(poll(0xb4,confirm,confirm)&confirm));
 mouse[7]=0;poll(0xb4,0,0);mouse[7]=1;mouse[1]=540;++mouse[2];++mouse[3];
 assert(poll(0xb4,confirm,confirm)&confirm);
 std::cout<<"30-10000 FPS menu timing and fresh mouse/keyboard/controller inputs passed.\n";
}
