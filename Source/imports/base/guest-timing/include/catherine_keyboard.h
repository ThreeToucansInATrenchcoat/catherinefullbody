#pragma once
#include <array>
#include <cstdint>
namespace catherine {
inline constexpr unsigned kKeyboardBindings = 27;
inline constexpr uint16_t kWheelUp = 256, kWheelDown = 257;
// Logical Npad buttons 0..15, right-stick directions 16..19, then mouse/arrow/Enter aliases.
inline constexpr std::array<uint16_t, kKeyboardBindings> kDefaultKeyboard = {
    32,16,'Z',17,'C','V',9,'E','Q',kWheelUp,27,8,
    'A','W','D','S','J','I','L','K',1,2,37,38,39,40,13
};
inline constexpr bool keyboard_binding_editable(unsigned index) {
    return index < 12 || index == 20 || index == 21 || index == 26;
}
inline constexpr bool valid_movement_preset(int preset) { return preset >= 0 && preset <= 2; }
inline constexpr std::array<uint16_t,4> movement_keys(int preset) {
    return preset == 1 ? std::array<uint16_t,4>{'S','E','F','D'} :
           preset == 2 ? std::array<uint16_t,4>{37,38,39,40} :
                         std::array<uint16_t,4>{'A','W','D','S'};
}
inline bool movement_key(int preset,unsigned key) {
    for (auto reserved : movement_keys(preset)) if (reserved == key) return true;
    return false;
}
inline void set_owned_movement_keys(std::array<uint16_t,kKeyboardBindings>& keys,int preset) {
    const auto movement = movement_keys(preset);
    for(unsigned i=0;i<4;++i) keys[12+i] = movement[i];
}
inline std::array<uint16_t,kKeyboardBindings> default_keyboard(int preset=0) {
    auto keys=kDefaultKeyboard;
    set_owned_movement_keys(keys,preset);
    if(preset==1){keys[7]='R';keys[8]='W';}
    return keys;
}
inline bool valid_keyboard(const std::array<uint16_t,kKeyboardBindings>& keys,int preset=0) {
    if(!valid_movement_preset(preset))return false;
    const auto movement=movement_keys(preset);
    for(unsigned i=0;i<4;++i)if(keys[12+i]!=movement[i])return false;
    for (unsigned i=0;i<keys.size();++i) {
        if(!keyboard_binding_editable(i))continue;
        if (!keys[i] || keys[i]>kWheelDown || movement_key(preset,keys[i])) return false;
        for (unsigned j=0;j<i;++j)
            if(keyboard_binding_editable(j)&&keys[j]==keys[i])return false;
    }
    return true;
}
inline bool rebind_keyboard(std::array<uint16_t,kKeyboardBindings>& keys,unsigned index,unsigned key,int preset) {
    if(!keyboard_binding_editable(index)||!key||key>kWheelDown||movement_key(preset,key))return false;
    auto next=keys;const auto old=next[index];
    for(unsigned i=0;i<next.size();++i)if(keyboard_binding_editable(i)&&next[i]==key)next[i]=old;
    next[index]=uint16_t(key);
    if(!valid_keyboard(next,preset))return false;
    keys=next;return true;
}
inline bool change_movement_preset(std::array<uint16_t,kKeyboardBindings>& keys,int old_preset,int next_preset) {
    if(!valid_movement_preset(old_preset)||!valid_movement_preset(next_preset))return false;
    auto next=keys;const auto defaults=default_keyboard(next_preset);
    std::array<bool,kKeyboardBindings> remap{};
    const uint16_t old_previous=old_preset==1?'W':'Q',old_next=old_preset==1?'R':'E';
    remap[8]=next[8]==old_previous&&next[8]!=defaults[8];
    remap[7]=next[7]==old_next&&next[7]!=defaults[7];
    set_owned_movement_keys(next,next_preset);
    for(unsigned i=0;i<next.size();++i)
        if(keyboard_binding_editable(i)&&movement_key(next_preset,next[i]))remap[i]=true;
    const auto occupied=[&](unsigned key,unsigned ignored){
        for(unsigned i=0;i<next.size();++i)
            if(i!=ignored&&keyboard_binding_editable(i)&&!remap[i]&&next[i]==key)return true;
        return false;
    };
    for(unsigned i=0;i<next.size();++i) {
        if(!keyboard_binding_editable(i)||!remap[i])continue;
        unsigned preferred=defaults[i];
        if(movement_key(next_preset,preferred)||occupied(preferred,i)) {
            for(preferred='A';preferred<='Z';++preferred)
                if(!movement_key(next_preset,preferred)&&!occupied(preferred,i))break;
            if(preferred>'Z')return false;
        }
        next[i]=uint16_t(preferred);remap[i]=false;
    }
    if(!valid_keyboard(next,next_preset))return false;
    keys=next;return true;
}
inline const char* keyboard_key_name(unsigned key) {
    switch(key) {
    case 1:return "Left Mouse";case 2:return "Right Mouse";case 4:return "Middle Mouse";
    case 5:return "Mouse 4";case 6:return "Mouse 5";
    case 8:return "Backspace";case 9:return "Tab";case 13:return "Enter";
    case 16:return "Shift";case 17:return "Ctrl";case 18:return "Alt";
    case 27:return "Escape";case 32:return "Space";case 33:return "Page Up";case 34:return "Page Down";
    case 35:return "End";case 36:return "Home";case 37:return "Left Arrow";
    case 38:return "Up Arrow";case 39:return "Right Arrow";case 40:return "Down Arrow";
    case 45:return "Insert";case 46:return "Delete";
    case 256:return "Wheel Up";case 257:return "Wheel Down";
    }
    // Thread-local buffers would be overwritten between multiple draw calls;
    // use a stable single-character table for letter/number keys instead.
    static constexpr const char* letters[]={"A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z"};
    static constexpr const char* digits[]={"0","1","2","3","4","5","6","7","8","9"};
    static constexpr const char* fn[]={"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    if(key>='A'&&key<='Z')return letters[key-'A'];
    if(key>='0'&&key<='9')return digits[key-'0'];
    if(key>=112&&key<=123)return fn[key-112];
    return "Key";
}
inline const char* keyboard_badge(unsigned key) {
    switch(key){case 1:return "LMB";case 2:return "RMB";case 4:return "MMB";
    case 8:return "BKSP";case 13:return "ENT";case 27:return "ESC";case 17:return "CTRL";case 16:return "SHFT";
    case 32:return "SPC";case 9:return "TAB";case 256:return "WU";case 257:return "WD";
    case 37:return "LEFT";case 38:return "UP";case 39:return "RGHT";case 40:return "DOWN";}
    return keyboard_key_name(key);
}
} // namespace catherine
