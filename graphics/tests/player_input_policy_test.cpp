#include "../src/player_input_policy.h"
#include "../src/keyboard_mouse_policy.h"
#include <cassert>
#include <iostream>
using catherine::assign_input_device;
int main(){
    const std::vector<std::string> devices{"controller-A","controller-B"};
    std::array<std::string,2> owners{"auto","none"};
    assign_input_device(owners,1,"controller-A",devices);
    assert(owners[0]=="keyboard"&&owners[1]=="controller-A");
    assign_input_device(owners,0,"auto",devices);
    assert(owners[0]=="auto"&&owners[1]=="none");
    assign_input_device(owners,1,"keyboard",devices);
    assert(owners[0]=="controller-A"&&owners[1]=="keyboard");
    assign_input_device(owners,0,"keyboard",devices);
    assert(owners[0]=="keyboard"&&owners[1]=="none");
    assign_input_device(owners,1,"keyboard",{});
    assert(owners[0]=="keyboard"&&owners[1]=="none");
    assign_input_device(owners,0,"controller-B",devices);
    assign_input_device(owners,1,"keyboard",devices);
    assert(owners[0]=="controller-B"&&owners[1]=="keyboard");
    assign_input_device(owners,1,"controller-A",devices);
    assert(owners[0]=="keyboard"&&owners[1]=="controller-A");
    assign_input_device(owners,0,"controller-A",devices);
    assert(owners[0]=="controller-A"&&owners[1]=="none");
    auto live=catherine::effective_input_devices({"controller-A","keyboard"},{});
    assert(live[0]=="keyboard"&&live[1]=="none");
    live=catherine::effective_input_devices({"keyboard","controller-A"},{});
    assert(live[0]=="keyboard"&&live[1]=="none");
    live=catherine::effective_input_devices({"controller-A","keyboard"},devices);
    assert(live[0]=="controller-A"&&live[1]=="keyboard");
    // All menu devices use P1 controls, even when assigned to P2 for gameplay.
    using namespace nx::plat;
    const uint64_t identity=0xfedcba9876543210ull;
    const uint64_t p2_swapped=identity^1ull^(1ull<<4);
    PadState p2_pad{};p2_pad.buttons=kA;p2_pad.lx=1234;
    assert(catherine::remap_input_buttons(p2_pad.buttons,p2_swapped)==kB);
    auto menu=catherine::menu_input_pad({}, {p2_pad},identity,false);
    assert(menu.buttons==kA&&menu.lx==1234);
    PadState first{};first.buttons=kX;
    menu=catherine::menu_input_pad({}, {first,p2_pad},identity,false);
    assert(menu.buttons==(kA|kX));
    // Capture always sees physical positional codes from every controller, not its owner's map.
    assert(catherine::raw_capture_buttons({first,p2_pad})==unsigned(kA|kX));
    assert(catherine::raw_capture_buttons({p2_pad})==unsigned(kA));
    auto p1_keys=catherine::default_keyboard();auto p2_keys=p1_keys;
    assert(catherine::rebind_keyboard(p1_keys,20,'G',0));
    assert(catherine::rebind_keyboard(p2_keys,20,'F',0));
    PadState keyboard_menu{};
    keyboard_mouse_pad(keyboard_menu,p1_keys,[](unsigned key){return key=='F';},0,0);
    assert(!(keyboard_menu.buttons&kA));
    keyboard_mouse_pad(keyboard_menu,p1_keys,[](unsigned key){return key=='G';},0,0);
    menu=catherine::menu_input_pad(keyboard_menu,{},identity,false);
    assert(menu.buttons&kA);
    assert(catherine::keyboard_glyph_owner(true,{"controller-A","keyboard"})==0);
    assert(catherine::keyboard_glyph_owner(false,{"controller-A","keyboard"})==1);
    assert(catherine::input_binding_profile(true,0)==0&&catherine::input_binding_profile(true,1)==0);
    assert(catherine::input_binding_profile(false,0)==0&&catherine::input_binding_profile(false,1)==1);
    std::cout<<"Player assignment ownership and fallback tests passed\n";
}
