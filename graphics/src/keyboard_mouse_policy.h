#pragma once
#include "platform.h"
#include "catherine_keyboard.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace nx::plat {
struct MouseLookFilter {
    double x=0,y=0;
    void sample(int dx,int dy,double dt){
        if(!std::isfinite(dt)||dt<=0||dt>.1){x=y=0;return;}
        const double decay=std::exp(-dt/.018);
        x=x*decay+double(dx)*(1-decay)/dt;
        y=y*decay+double(dy)*(1-decay)/dt;
    }
    static int axis(double v){return int(std::clamp(v*(0.35/900.),-1.,1.)*kStickMax);}
};
template<class Held> inline void keyboard_mouse_pad(PadState& p,const std::array<uint16_t,catherine::kKeyboardBindings>& keys,Held held,int mouse_x,int mouse_y){
    for(unsigned i=0;i<16;++i)if(held(keys[i]))p.buttons|=uint64_t{1}<<i;
    if(held(keys[20])||held(keys[26]))p.buttons|=kA;
    if(held(keys[21]))p.buttons|=kB;
    // Movement is owned by the selected preset; legacy arrow aliases are inactive.
    const auto direction=[&](unsigned i){return (p.buttons&(kLeft<<i))!=0;};
    p.lx=(int(direction(2))-int(direction(0)))*kStickMax;
    p.ly=(int(direction(1))-int(direction(3)))*kStickMax;
    // Keyboard camera-direction bindings are retired; mouse controls look.
    p.rx=mouse_x;p.ry=mouse_y;
}
inline void swap_controller_sticks(PadState& p,bool swap) {
    if(swap){std::swap(p.lx,p.rx);std::swap(p.ly,p.ry);}
}
struct ContentPointer {int x=-1,y=-1;bool inside=false;};
inline ContentPointer content_pointer(int x,int y,int width,int height) {
    if(width<=0||height<=0)return {};
    const double scale=std::min(double(width)/1920.,double(height)/1080.);
    const double left=(width-1920.*scale)*.5,top=(height-1080.*scale)*.5;
    const double px=(x-left)/scale,py=(y-top)/scale;
    if(px<0||px>=1920||py<0||py>=1080)return {};
    return {int(px),int(py),true};
}
} // namespace nx::plat
