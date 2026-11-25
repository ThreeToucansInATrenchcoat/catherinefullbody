#pragma once
#include "platform.h"
#include <cstdint>
#include <map>
#include <string>

namespace nx::plat {
struct P1InputSelection {
    std::string identity="keyboard";
    InputLayout layout=InputLayout::KeyboardMouse;
    uint64_t generation=0;
};

// Shared by the native and diagnostic readers. Only a fresh key/click serial
// or physical controller button edge chooses Auto's gameplay/camera source.
// Stick directions (bits 16+) and all axis values are deliberately excluded.
class P1InputOwner {
    std::map<std::string,uint64_t> previous_buttons_;
    uint64_t keyboard_serial_=0;
    std::string automatic_="keyboard";
    P1InputSelection published_;
    unsigned backend_=~0u;
public:
    template<class Controllers>
    P1InputSelection select(const std::string& configured,const Controllers& controllers,
                            uint64_t keyboard_serial,unsigned backend=0) {
        const bool keyboard_edge=keyboard_serial!=keyboard_serial_;
        keyboard_serial_=keyboard_serial;
        std::string controller_edge;
        for(const auto& device:controllers){
            const uint64_t buttons=device.state.buttons&0xffffu;
            const auto it=previous_buttons_.find(device.identity);
            const uint64_t previous=it==previous_buttons_.end()?0:it->second;
            if(buttons&~previous)controller_edge=device.identity;
            previous_buttons_[device.identity]=buttons;
        }
        // Retain sampled button history across reader switches/disconnects.
        // Returning from diagnostics must not turn an already-held button
        // into another press. A click observed in this poll wins over an
        // older, previously unsampled controller edge in the same poll.
        if(configured=="auto"){
            if(!controller_edge.empty())automatic_=controller_edge;
            if(keyboard_edge)automatic_="keyboard";
        }else automatic_=configured;

        P1InputSelection result;
        result.identity=automatic_;
        if(result.identity!="keyboard"){
            bool found=false;
            for(const auto& device:controllers)if(device.identity==result.identity){
                result.layout=device.layout;found=true;break;
            }
            if(!found){result.identity="keyboard";automatic_="keyboard";}
        }
        if(result.identity!=published_.identity||result.layout!=published_.layout||backend!=backend_)
            ++published_.generation;
        backend_=backend;
        result.generation=published_.generation;
        published_=result;
        return result;
    }
};
} // namespace nx::plat
