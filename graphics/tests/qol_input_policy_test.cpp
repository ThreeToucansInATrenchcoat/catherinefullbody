#include "../src/keyboard_mouse_policy.h"
#include <cassert>
#include <iostream>
using namespace catherine;
using namespace nx::plat;
static void check_reserved(const std::array<uint16_t,kKeyboardBindings>& keys,int preset){
    assert(valid_keyboard(keys,preset));
    for(unsigned i=0;i<keys.size();++i)if(keyboard_binding_editable(i))assert(!movement_key(preset,keys[i]));
}
int main(){
    auto keys=default_keyboard();check_reserved(keys,0);
    assert(change_movement_preset(keys,0,1));assert(keys[8]=='W'&&keys[7]=='R');check_reserved(keys,1);
    assert(change_movement_preset(keys,1,2));assert(keys[8]=='Q'&&keys[7]=='E');check_reserved(keys,2);
    assert(change_movement_preset(keys,2,0));assert(keys==default_keyboard());
    assert(rebind_keyboard(keys,0,'F',0));assert(rebind_keyboard(keys,1,'H',0));
    assert(rebind_keyboard(keys,2,'R',0)); // Occupies the ESDF next-tab default.
    const auto custom_h=keys[1];
    assert(change_movement_preset(keys,0,1));check_reserved(keys,1);
    assert(keys[0]==32);assert(keys[1]==custom_h);assert(keys[2]=='R');
    assert(keys[7]!='R'&&keys[7]!='E'); // Deterministic free key when context default is occupied.
    const auto esdf=keys;
    assert(!rebind_keyboard(keys,0,'F',1));assert(keys==esdf);
    assert(!rebind_keyboard(keys,12,'H',1));assert(!rebind_keyboard(keys,16,'H',1));
    for(int cycle=0;cycle<20;++cycle){
        assert(change_movement_preset(keys,1,2));check_reserved(keys,2);
        assert(change_movement_preset(keys,2,0));check_reserved(keys,0);
        assert(change_movement_preset(keys,0,1));check_reserved(keys,1);
    }
    keys=default_keyboard();assert(rebind_keyboard(keys,0,37,0));
    assert(change_movement_preset(keys,0,2));check_reserved(keys,2);assert(keys[0]==32);
    PadState p{};keys=default_keyboard();
    keyboard_mouse_pad(p,keys,[](unsigned key){return key==37||key=='J'||key=='I'||key=='L'||key=='K';},123,-456);
    assert(!p.buttons&&p.lx==0&&p.ly==0&&p.rx==123&&p.ry==-456);
    p={};keys=default_keyboard(2);
    keyboard_mouse_pad(p,keys,[](unsigned key){return key==37;},0,0);
    assert((p.buttons&kLeft)&&p.lx==-kStickMax);
    PadState controller{};controller.lx=111;controller.ly=222;controller.rx=333;controller.ry=444;
    swap_controller_sticks(controller,true);assert(controller.lx==333&&controller.ly==444&&controller.rx==111&&controller.ry==222);
    PadState keyboard{};keyboard_mouse_pad(keyboard,default_keyboard(),[](unsigned key){return key=='A';},555,666);
    assert(keyboard.lx==-kStickMax&&keyboard.rx==555&&keyboard.ry==666);
    auto point=content_pointer(960,540,1920,1080);assert(point.inside&&point.x==960&&point.y==540);
    point=content_pointer(1920,1080,3840,2160);assert(point.inside&&point.x==960&&point.y==540);
    point=content_pointer(500,500,1000,1000);assert(point.inside&&point.x==960&&point.y==540);
    assert(!content_pointer(500,100,1000,1000).inside);assert(!content_pointer(1920,1080,1920,1080).inside);
    point=content_pointer(1000,500,2000,1000);assert(point.inside&&point.x==960&&point.y==540);
    assert(!content_pointer(1,500,2000,1000).inside);assert(!content_pointer(0,0,0,0).inside);
    std::cout<<"QoL input policy tests passed\n";
}
