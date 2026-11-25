#pragma once
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdlib>
#include "platform.h"

namespace catherine {
inline unsigned input_binding_profile(bool menus,unsigned player){return menus?0:player;}
inline unsigned keyboard_glyph_owner(bool menus,const std::array<std::string,2>& owners){
    return !menus&&owners[1]=="keyboard"?1:0;
}
inline uint64_t remap_input_buttons(uint64_t buttons,uint64_t mapping){
    uint64_t result=buttons&~uint64_t{0xffff};
    for(unsigned i=0;i<16;++i)result|=((buttons>>((mapping>>(i*4))&15))&1ull)<<i;
    return result;
}
inline unsigned raw_capture_buttons(const std::vector<nx::plat::PadState>& controllers){
    unsigned buttons=0;for(const auto&pad:controllers)buttons|=unsigned(pad.buttons)&0xffff;
    return buttons;
}
inline nx::plat::PadState menu_input_pad(nx::plat::PadState keyboard,
                                        const std::vector<nx::plat::PadState>& controllers,
                                        uint64_t player1_mapping,bool swap_sticks){
    for(auto pad:controllers){
        pad.buttons=remap_input_buttons(pad.buttons,player1_mapping);
        if(swap_sticks){std::swap(pad.lx,pad.rx);std::swap(pad.ly,pad.ry);}
        keyboard.buttons|=pad.buttons;
        if(std::abs(pad.lx)>std::abs(keyboard.lx))keyboard.lx=pad.lx;
        if(std::abs(pad.ly)>std::abs(keyboard.ly))keyboard.ly=pad.ly;
        if(std::abs(pad.rx)>std::abs(keyboard.rx))keyboard.rx=pad.rx;
        if(std::abs(pad.ry)>std::abs(keyboard.ry))keyboard.ry=pad.ry;
    }
    return keyboard;
}
inline std::array<std::string,2> effective_input_devices(std::array<std::string,2> owners,
                                                       const std::vector<std::string>& controllers) {
    const auto live=[&](const std::string&id){return id=="keyboard"||std::find(controllers.begin(),controllers.end(),id)!=controllers.end();};
    if(owners[0]=="auto"){owners[1]="none";return owners;}
    if(!live(owners[0]))owners[0]="keyboard";
    if(!live(owners[1])||owners[1]==owners[0])owners[1]="none";
    return owners;
}
// Keyboard/mouse is one exclusive physical source. The caller supplies controller identities.
inline void assign_input_device(std::array<std::string,2>& owners,unsigned player,
                                const std::string& target,const std::vector<std::string>& controllers) {
    if(player>1)return;
    if(player==0&&target=="auto"){owners={"auto","none"};return;}
    if(player==1&&target=="none"){owners[1]="none";return;}
    if(target=="none"||target=="auto")return;
    owners[player]=target;
    if(player==0){if(owners[1]==target)owners[1]="none";return;}
    // Assigning P2 explicitly chooses a different source for P1, even from Auto.
    if(target!="keyboard")owners[0]="keyboard";
    else {
        if(owners[0]!="auto"&&owners[0]!="keyboard"&&
           std::find(controllers.begin(),controllers.end(),owners[0])!=controllers.end())return;
        for(const auto& id:controllers)if(id!=target){owners[0]=id;return;}
        owners[0]=target;owners[1]="none";
    }
}
}
