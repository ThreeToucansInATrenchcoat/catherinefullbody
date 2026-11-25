#pragma once
#include "dualsense_win.h"
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <memory>
#include <vector>
#pragma comment(lib,"windowsapp")

namespace nx::plat {
struct WindowsController {
    std::string identity,name;
    PadState state{};
    InputLayout layout=InputLayout::Xbox;
    bool direct_dualsense=false;
};
inline std::vector<WindowsController> windows_controllers(bool& available){
    using namespace winrt::Windows::Gaming::Input;
    std::vector<WindowsController> result;
    available=false;
    try {
        static thread_local const bool initialized=[](){winrt::init_apartment(winrt::apartment_type::multi_threaded);return true;}();
        (void)initialized;
        const auto devices=RawGameController::RawGameControllers();
        available=true;
        for(const auto& raw:devices){
            WindowsController device;
            device.identity="wgi:"+winrt::to_string(raw.NonRoamableId());
            device.name=winrt::to_string(raw.DisplayName());
            const auto vid=raw.HardwareVendorId(),pid=raw.HardwareProductId();
            device.layout=vid==0x054c?InputLayout::PlayStation:InputLayout::Xbox;
            device.direct_dualsense=vid==0x054c&&(pid==0x0ce6||pid==0x0df2);
            if(device.name.empty())device.name=vid==0x054c?"PlayStation Controller":"Game Controller";
            if(const auto gamepad=Gamepad::FromGameController(raw)){
                const auto reading=gamepad.GetCurrentReading();
                const auto down=[&](GamepadButtons button){return (uint32_t(reading.Buttons)&uint32_t(button))!=0;};
                constexpr std::pair<GamepadButtons,uint64_t> buttons[]={
                    {GamepadButtons::A,kB},{GamepadButtons::B,kA},{GamepadButtons::X,kY},{GamepadButtons::Y,kX},
                    {GamepadButtons::LeftShoulder,kL},{GamepadButtons::RightShoulder,kR},
                    {GamepadButtons::LeftThumbstick,kStickL},{GamepadButtons::RightThumbstick,kStickR},
                    {GamepadButtons::Menu,kPlus},{GamepadButtons::View,kMinus},
                    {GamepadButtons::DPadUp,kUp},{GamepadButtons::DPadDown,kDown},
                    {GamepadButtons::DPadLeft,kLeft},{GamepadButtons::DPadRight,kRight}};
                for(const auto&[button,logical]:buttons)if(down(button))device.state.buttons|=logical;
                if(reading.LeftTrigger>100./255)device.state.buttons|=kZL;
                if(reading.RightTrigger>100./255)device.state.buttons|=kZR;
                device.state.lx=pad_stick_dead_zone(int32_t(reading.LeftThumbstickX*kStickMax));
                device.state.ly=pad_stick_dead_zone(int32_t(reading.LeftThumbstickY*kStickMax));
                device.state.rx=pad_stick_dead_zone(int32_t(reading.RightThumbstickX*kStickMax));
                device.state.ry=pad_stick_dead_zone(int32_t(reading.RightThumbstickY*kStickMax));
            }else{
                const unsigned count=raw.ButtonCount();
                auto buttons=std::make_unique<bool[]>(count);
                std::vector<GameControllerSwitchPosition> switches(raw.SwitchCount());
                std::vector<double> axes(raw.AxisCount());
                raw.GetCurrentReading(winrt::array_view<bool>(buttons.get(),count),switches,axes);
                const auto logical=[vid](GameControllerButtonLabel label)->uint64_t{
                    using L=GameControllerButtonLabel;
                    switch(label){
                    case L::XboxA:case L::Cross:return kB;
                    case L::XboxB:case L::Circle:return kA;
                    case L::XboxX:case L::Square:return kY;
                    case L::XboxY:case L::Triangle:return kX;
                    case L::LetterA:return vid==0x057e?kA:kB;case L::LetterB:return vid==0x057e?kB:kA;
                    case L::LetterX:return vid==0x057e?kX:kY;case L::LetterY:return vid==0x057e?kY:kX;
                    case L::XboxLeftBumper:case L::LeftBumper:case L::Left1:case L::LetterL:return kL;
                    case L::XboxRightBumper:case L::RightBumper:case L::Right1:case L::LetterR:return kR;
                    case L::XboxLeftTrigger:case L::LeftTrigger:case L::Left2:return kZL;
                    case L::XboxRightTrigger:case L::RightTrigger:case L::Right2:return kZR;
                    case L::XboxLeftStickButton:case L::LeftStickButton:case L::Left3:return kStickL;
                    case L::XboxRightStickButton:case L::RightStickButton:case L::Right3:return kStickR;
                    case L::XboxMenu:case L::XboxStart:case L::Menu:case L::Start:case L::Options:case L::Plus:return kPlus;
                    case L::XboxBack:case L::XboxView:case L::View:case L::Back:case L::Select:case L::Share:case L::Minus:return kMinus;
                    case L::XboxUp:case L::Up:return kUp;case L::XboxDown:case L::Down:return kDown;
                    case L::XboxLeft:case L::Left:return kLeft;case L::XboxRight:case L::Right:return kRight;
                    default:return 0;
                    }
                };
                constexpr uint64_t fallback[]={kB,kA,kY,kX,kL,kR,kZL,kZR,kMinus,kPlus,kStickL,kStickR};
                for(unsigned i=0;i<count;++i)if(buttons[i]){
                    const auto label=raw.GetButtonLabel(i);const auto mapped=logical(label);
                    device.state.buttons|=mapped?mapped:label==GameControllerButtonLabel::None&&i<std::size(fallback)?fallback[i]:0;
                }
                constexpr uint64_t hat[]={0,kUp,kUp|kRight,kRight,kRight|kDown,kDown,kDown|kLeft,kLeft,kLeft|kUp};
                for(const auto position:switches)if(unsigned(position)<std::size(hat))device.state.buttons|=hat[unsigned(position)];
                // Raw devices expose normalized axes. The first four are the usual two sticks.
                const auto axis=[&](unsigned i,bool invert){return i<axes.size()?pad_stick_dead_zone(int32_t((axes[i]*2-1)*kStickMax)*(invert?-1:1)):0;};
                device.state.lx=axis(0,false);device.state.ly=axis(1,true);
                device.state.rx=axis(2,false);device.state.ry=axis(3,true);
            }
            result.push_back(std::move(device));
        }
    }catch(const winrt::hresult_error&){/* XInput/direct HID remain available on older hosts. */}
    return result;
}
}
