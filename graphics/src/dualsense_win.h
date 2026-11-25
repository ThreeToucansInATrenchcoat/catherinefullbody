#pragma once

#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "platform.h"

namespace nx::plat {

inline int32_t pad_stick_dead_zone(int32_t value) {
    return value > -7849 && value < 7849 ? 0 : (value < -kStickMax ? -kStickMax : value);
}

inline bool decode_dualsense_report(const uint8_t* report, size_t size, PadState& state) {
    if (!size) return false;

    const uint8_t* axes;
    const uint8_t* buttons;
    const uint8_t* triggers;
    if (report[0] == 0x01 && size == 64) {  // USB
        axes = report + 1;
        buttons = axes + 7;
        triggers = axes + 4;
    } else if (report[0] == 0x31 && size == 78) {  // Bluetooth extended
        axes = report + 2;
        buttons = axes + 7;
        triggers = axes + 4;
    } else if (report[0] == 0x01 && (size == 10 || size == 78)) {  // Bluetooth basic, sometimes padded
        axes = report + 1;
        buttons = axes + 4;
        triggers = axes + 7;
    } else {
        return false;
    }

    auto stick = [](uint8_t raw) {
        const int32_t centered = int32_t(raw) - 128;
        return pad_stick_dead_zone(centered * kStickMax / (centered < 0 ? 128 : 127));
    };
    state = {};
    state.lx = stick(axes[0]);
    state.ly = -stick(axes[1]);
    state.rx = stick(axes[2]);
    state.ry = -stick(axes[3]);

    // Match XInput's positional mapping to the Nintendo button labels.
    if (buttons[0] & 0x10) state.buttons |= kY;  // Square
    if (buttons[0] & 0x20) state.buttons |= kB;  // Cross
    if (buttons[0] & 0x40) state.buttons |= kA;  // Circle
    if (buttons[0] & 0x80) state.buttons |= kX;  // Triangle
    if (buttons[1] & 0x01) state.buttons |= kL;
    if (buttons[1] & 0x02) state.buttons |= kR;
    if (buttons[1] & 0x10) state.buttons |= kMinus;  // Create
    if (buttons[1] & 0x20) state.buttons |= kPlus;   // Options
    if (buttons[1] & 0x40) state.buttons |= kStickL;
    if (buttons[1] & 0x80) state.buttons |= kStickR;
    if (triggers[0] > 100) state.buttons |= kZL;
    if (triggers[1] > 100) state.buttons |= kZR;
    constexpr uint64_t directions[] = {
        kUp, kUp | kRight, kRight, kRight | kDown, kDown, kDown | kLeft, kLeft, kLeft | kUp,
    };
    const unsigned hat = buttons[0] & 0x0f;
    if (hat < 8) state.buttons |= directions[hat];
    return true;
}

// Device paths remain stable across a reconnect; HANDLE values do not.
struct DirectController {
    HANDLE handle=nullptr;
    std::string identity;
    std::string name;
    PadState state{};
};
class DualSenseInput {
public:
    bool register_window(HWND window) {
        constexpr DWORD flags=RIDEV_INPUTSINK|RIDEV_DEVNOTIFY;
        RAWINPUTDEVICE devices[]={{1,4,flags,window},{1,5,flags,window}};
        if(!RegisterRawInputDevices(devices,ARRAYSIZE(devices),sizeof devices[0]))return false;
        refresh_connection();return true;
    }
    bool connected() const noexcept{return connected_.load(std::memory_order_relaxed);}
    void refresh_connection(HANDLE removed=nullptr) {
        UINT count=0;
        if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1))return;
        std::vector<RAWINPUTDEVICELIST> raw(count);
        if(count&&GetRawInputDeviceList(raw.data(),&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1))return;
        std::vector<DirectController> next;
        for(UINT i=0;i<count;++i){
            if(raw[i].dwType!=RIM_TYPEHID||raw[i].hDevice==removed||!is_dualsense(raw[i].hDevice))continue;
            UINT length=0;GetRawInputDeviceInfoA(raw[i].hDevice,RIDI_DEVICENAME,nullptr,&length);
            std::string path(length,'\0');
            if(!length||GetRawInputDeviceInfoA(raw[i].hDevice,RIDI_DEVICENAME,path.data(),&length)==UINT(-1))continue;
            while(!path.empty()&&!path.back())path.pop_back();
            std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return char(std::tolower(c));});
            next.push_back({raw[i].hDevice,"hid:"+path,"DualSense",{}});
        }
        std::sort(next.begin(),next.end(),[](const auto&a,const auto&b){return a.identity<b.identity;});
        AcquireSRWLockExclusive(&lock_);
        for(auto& device:next)for(const auto& old:devices_)if(device.identity==old.identity)device.state=old.state;
        devices_=std::move(next);connected_.store(!devices_.empty(),std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&lock_);
    }
    void remove(HANDLE handle) {
        AcquireSRWLockExclusive(&lock_);
        std::erase_if(devices_,[&](const auto& device){return device.handle==handle;});
        connected_.store(!devices_.empty(),std::memory_order_relaxed);ReleaseSRWLockExclusive(&lock_);
    }
    bool input(HRAWINPUT message) {
        UINT length=0;
        if(GetRawInputData(message,RID_INPUT,nullptr,&length,sizeof(RAWINPUTHEADER))==UINT(-1))return false;
        std::vector<uint8_t> data(length);
        constexpr size_t payload=offsetof(RAWINPUT,data.hid.bRawData);
        if(length<payload||GetRawInputData(message,RID_INPUT,data.data(),&length,sizeof(RAWINPUTHEADER))==UINT(-1))return false;
        const auto&raw=*reinterpret_cast<const RAWINPUT*>(data.data());
        if(raw.header.dwType!=RIM_TYPEHID)return false;
        const auto&hid=raw.data.hid;
        if(!hid.dwSizeHid||!hid.dwCount||hid.dwCount>(length-payload)/hid.dwSizeHid)return false;
        PadState latest{};
        if(!decode_dualsense_report(data.data()+payload+(hid.dwCount-1)*hid.dwSizeHid,hid.dwSizeHid,latest))return false;
        AcquireSRWLockExclusive(&lock_);
        bool found=false;
        for(auto&device:devices_)if(device.handle==raw.header.hDevice){device.state=latest;found=true;break;}
        ReleaseSRWLockExclusive(&lock_);return found;
    }
    std::vector<DirectController> snapshots() {
        AcquireSRWLockShared(&lock_);auto result=devices_;ReleaseSRWLockShared(&lock_);return result;
    }
    PadState snapshot(){auto devices=snapshots();return devices.empty()?PadState{}:devices.front().state;}
private:
    static bool is_dualsense(HANDLE source) {
        RID_DEVICE_INFO info{};info.cbSize=sizeof info;UINT size=sizeof info;
        return GetRawInputDeviceInfoW(source,RIDI_DEVICEINFO,&info,&size)!=UINT(-1)&&info.dwType==RIM_TYPEHID&&
            info.hid.dwVendorId==0x054c&&(info.hid.dwProductId==0x0ce6||info.hid.dwProductId==0x0df2)&&
            info.hid.usUsagePage==1&&(info.hid.usUsage==4||info.hid.usUsage==5);
    }
    std::atomic<bool> connected_{false};
    SRWLOCK lock_=SRWLOCK_INIT;
    std::vector<DirectController> devices_;
};
} // namespace nx::plat
