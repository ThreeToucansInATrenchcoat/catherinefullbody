#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <cairo/cairo.h>
#include <vulkan/vulkan.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <nx/game.h>
#include "platform.h"
#include "gui.h"
#include "window_linux.h"
#include "window_icon_linux.h"
#include "guest_host.h"
#include "keyboard_mouse_policy.h"
#include "player_input_policy.h"
#include "catherine_settings_win.h"
#include "catherine_resolution_live.h"
#include "catherine_refresh.h"
#include "window_area_title.h"
#include "config_pc_host.h"
#include "controller_tutorial_audio.h"
#include "controller_prompt_policy.h"
extern "C" int catherine_dev_overlay_visible();
extern "C" void catherine_linux_freeze_saves();


namespace {
using namespace nx::plat;
SDL_Window* window;
const char* surface_extension;
std::atomic<uint64_t> drawable_size{0};
std::atomic<bool> minimized{false};
std::atomic<uint64_t> presented_frames{0};
struct WindowMode {
    bool fullscreen;
    int height;
    bool operator==(const WindowMode&) const=default;
};
struct WindowPreview {
    WindowMode original,requested;
    uint64_t deadline,generation=0;
    int state=1;
};
std::mutex window_lock;
WindowMode active_mode{};
std::optional<WindowMode> draft_mode, queued_mode;
std::optional<WindowPreview> preview;
constexpr int window_heights[]{0,720,1080,1440,2160};

catherine::HostDisplay display_mode(int index) {
    SDL_DisplayMode mode{};
    if (SDL_GetCurrentDisplayMode(index,&mode)!=0) return {};
    return {mode.w,mode.h,mode.refresh_rate>0?mode.refresh_rate:60};
}

int launch_display() {
    int x=0,y=0;
    SDL_GetGlobalMouseState(&x,&y);
    const SDL_Point pointer{x,y};
    const int display=SDL_GetPointDisplayIndex(&pointer);
    return display>=0?display:0;
}

int auto_window_height(int display) {
    SDL_Rect bounds{};
    if (SDL_GetDisplayBounds(display,&bounds)!=0) return 720;
    return std::min(bounds.h,bounds.w*9/16)*3/4;
}

void center_window(int display) {
    SDL_Rect bounds{};
    if (SDL_GetDisplayBounds(display,&bounds)!=0) return;
    int width,height,top=0,left=0,bottom=0,right=0;
    SDL_GetWindowSize(window,&width,&height);
    SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right);
    SDL_SetWindowPosition(window,bounds.x+(bounds.w-width-left-right)/2,
                                bounds.y+(bounds.h-height-top-bottom)/2);
}


void update_window_title(uint64_t now) {
    static uint64_t last=now, last_frames=presented_frames.load(std::memory_order_relaxed);
    if (now-last<1000) return;
    const uint64_t frames=presented_frames.load(std::memory_order_relaxed);
    const double fps=double(frames-last_frames)*1000/double(now-last);
    last=now;
    last_frames=frames;

    const auto reader=[](uint64_t address,void* out,size_t size) {
        return address>=0x10000 && address<=0x7fffffffffffull-size &&
               catherine_host_read(reinterpret_cast<const void*>(address),out,size);
    };
    const auto wide_area=catherine::window_area::label(catherine::window_area::current(reader));
    const std::string area(wide_area.begin(),wide_area.end());
    const int limit=catherine_requested_refresh_rate();
    const std::string cap=limit?std::to_string(limit):"Unlimited";
    char title[256];
    std::snprintf(title,sizeof title,"%s%s%s  -  %.1f FPS  -  Cap %s",nx::game::kInfo.title,
                  area.empty()?"":"  -  ",area.c_str(),fps,cap.c_str());
    SDL_SetWindowTitle(window,title);
}

bool set_window_mode(WindowMode mode,uint64_t& generation) {
    if (mode.fullscreen!=active_mode.fullscreen &&
        SDL_SetWindowFullscreen(window,mode.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0) return false;
    if (!mode.fullscreen) {
        const int height=mode.height?mode.height:auto_window_height(SDL_GetWindowDisplayIndex(window));
        SDL_SetWindowSize(window,height*16/9,height);
        if (!mode.height) center_window(SDL_GetWindowDisplayIndex(window));
    }
    active_mode=mode;
    const auto display=display_mode(SDL_GetWindowDisplayIndex(window));
    catherine::update_display_mode(display);
    const unsigned target=catherine::display_render_height(display,mode.fullscreen,mode.height);
    if (target!=catherine_resolution_applied_height())
        generation=catherine_resolution_request(target);
    return true;
}

void service_window_mode() {
    std::lock_guard lock(window_lock);
    if (queued_mode) {
        uint64_t ignored=0;
        set_window_mode(*queued_mode,ignored);
        queued_mode.reset();
    }
    if (!preview || preview->state!=1) return;
    if (!set_window_mode(preview->requested,preview->generation)) preview->state=-1;
    else preview->state=preview->generation?3:2;
}

int window_setting(unsigned field,int change,catherine::HostSettings config) {
    std::lock_guard lock(window_lock);
    const WindowMode selected=draft_mode.value_or(active_mode);
    switch (field) {
    case PC_DISCARD_DRAFT: draft_mode.reset();return 1;
    case PC_MODE:
        if (!change) return selected.fullscreen;
        draft_mode=WindowMode{!selected.fullscreen,selected.height};
        return 1;
    case PC_RESOLUTION: {
        if (selected.fullscreen) return change?0:5;
        int index=(std::find(std::begin(window_heights),std::end(window_heights),selected.height)-std::begin(window_heights))%5;
        if (!change) return index;
        index=change>=100?std::clamp(change-100,0,4):(index+(change<0?4:1))%5;
        draft_mode=WindowMode{false,window_heights[index]};
        return 1;
    }
    case PC_APPLY:
        if (!change) return selected!=active_mode;
        if (preview) return 0;
        if (selected==active_mode) return 1;
        preview=WindowPreview{active_mode,selected,SDL_GetTicks64()+15000};
        draft_mode.reset();
        return 1;
    default: break;
    }
    if (!preview) return 0;
    if (preview->state==3 && catherine_resolution_completed_generation()>=preview->generation)
        preview->state=catherine_resolution_status()==CATHERINE_RESOLUTION_APPLIED?2:-1;
    const bool ready=preview->state==2 && SDL_GetTicks64()<preview->deadline;
    const bool expired=SDL_GetTicks64()>=preview->deadline || preview->state<0;
    if (field==PC_PREVIEW && !expired) return ready?2:1;
    bool saved=false;
    if (field==PC_KEEP && ready) {
        config.fullscreen=preview->requested.fullscreen;
        config.window_height=preview->requested.height;
        saved=catherine::store_host_settings(config);
    }
    if (!saved) queued_mode=preview->original;
    preview.reset();
    return saved || field==PC_REVERT;
}
std::atomic<bool> hide_cursor{false};
std::atomic<unsigned> binding_player{0}, input_layout{0}, raw_key{0}, keyboard_glyph_player{0};
std::atomic<uint64_t> raw_buttons[2]{};
std::atomic<bool> input_menu{false};
struct ControllerIdentity {
    std::string id;
    std::string name;
    InputLayout layout=InputLayout::Xbox;
};
std::array<ControllerIdentity,2> controller_identity;

ControllerIdentity identify_controller(SDL_GameController* controller, unsigned slot) {
    char guid[33];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controller)),guid,sizeof guid);
    const char* serial=SDL_GameControllerGetSerial(controller);
    const char* name=SDL_GameControllerName(controller);
    ControllerIdentity identity;
    identity.id=std::string("sdl:")+guid+":"+(serial?serial:std::to_string(slot));
    identity.name=name?name:"Game controller";
    switch (SDL_GameControllerGetType(controller)) {
    case SDL_CONTROLLER_TYPE_PS4: identity.layout=InputLayout::PS4; break;
    case SDL_CONTROLLER_TYPE_PS5: identity.layout=InputLayout::PlayStation; break;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: identity.layout=InputLayout::Switch; break;
    default: break;
    }
    return identity;
}
std::vector<std::string> controller_ids() {
    std::vector<std::string> ids;
    for (const auto& device:controller_identity)
        if (!device.id.empty()) ids.push_back(device.id);
    return ids;
}
std::mutex input_lock;
PadState pad_state[2]{};
bool pad_connected[2]{true,false};
SDL_GameController* controllers[2]{};
std::array<bool, SDL_NUM_SCANCODES> keys{};
unsigned mouse_buttons=0, wheel_key=0;
uint64_t wheel_until=0;
int mouse_dx=0,mouse_dy=0;
MouseLookFilter mouse_look;

bool key_held(unsigned key) {
    if (key==catherine::kWheelUp || key==catherine::kWheelDown)
        return wheel_key==key && SDL_GetTicks64()<wheel_until;
    if (key==1) return mouse_buttons&SDL_BUTTON_LMASK;
    if (key==2) return mouse_buttons&SDL_BUTTON_RMASK;
    if (key==4) return mouse_buttons&SDL_BUTTON_MMASK;
    if (key==5) return mouse_buttons&SDL_BUTTON_X1MASK;
    if (key==6) return mouse_buttons&SDL_BUTTON_X2MASK;
    if (key>='A' && key<='Z') return keys[SDL_SCANCODE_A+key-'A'];
    if (key>='1' && key<='9') return keys[SDL_SCANCODE_1+key-'1'];
    if (key>=112 && key<=123) return keys[SDL_SCANCODE_F1+key-112];
    switch(key) {
    case '0': return keys[SDL_SCANCODE_0];
    case 8: return keys[SDL_SCANCODE_BACKSPACE];
    case 9: return keys[SDL_SCANCODE_TAB];
    case 13: return keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
    case 16: return keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    case 17: return keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    case 18: return keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    case 27: return keys[SDL_SCANCODE_ESCAPE];
    case 32: return keys[SDL_SCANCODE_SPACE];
    case 33: return keys[SDL_SCANCODE_PAGEUP];
    case 34: return keys[SDL_SCANCODE_PAGEDOWN];
    case 35: return keys[SDL_SCANCODE_END];
    case 36: return keys[SDL_SCANCODE_HOME];
    case 37: return keys[SDL_SCANCODE_LEFT];
    case 38: return keys[SDL_SCANCODE_UP];
    case 39: return keys[SDL_SCANCODE_RIGHT];
    case 40: return keys[SDL_SCANCODE_DOWN];
    case 45: return keys[SDL_SCANCODE_INSERT];
    case 46: return keys[SDL_SCANCODE_DELETE];
    default: return false;
    }
}

constexpr std::pair<SDL_GameControllerButton,uint64_t> controller_buttons[] = {
    {SDL_CONTROLLER_BUTTON_A,kB}, {SDL_CONTROLLER_BUTTON_B,kA},
    {SDL_CONTROLLER_BUTTON_Y,kX}, {SDL_CONTROLLER_BUTTON_X,kY},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK,kStickL}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK,kStickR},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,kL}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,kR},
    {SDL_CONTROLLER_BUTTON_START,kPlus}, {SDL_CONTROLLER_BUTTON_BACK,kMinus},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT,kLeft}, {SDL_CONTROLLER_BUTTON_DPAD_UP,kUp},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT,kRight}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN,kDown}
};

void update_input() {
    const auto config=catherine::host_settings();
    unsigned pressed_key=0;
    for (unsigned key=1;key<=catherine::kWheelDown;++key)
        if (key_held(key)) pressed_key=key;
    raw_key.store(pressed_key);
    std::lock_guard lock(input_lock);
    PadState raw[2]{};
    for (unsigned i=0;i<2;++i) {
        auto& pad=raw[i];
        auto* controller=controllers[i];
        if (!controller) continue;
        for (auto [button,mask]:controller_buttons)
            if (SDL_GameControllerGetButton(controller,button)) pad.buttons|=mask;
        const auto axis=[&](SDL_GameControllerAxis index) {
            return std::clamp<int32_t>(SDL_GameControllerGetAxis(controller,index),-32767,32767);
        };
        pad.lx=axis(SDL_CONTROLLER_AXIS_LEFTX);
        pad.ly=-axis(SDL_CONTROLLER_AXIS_LEFTY);
        pad.rx=axis(SDL_CONTROLLER_AXIS_RIGHTX);
        pad.ry=-axis(SDL_CONTROLLER_AXIS_RIGHTY);
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT)>4096) pad.buttons|=kZL;
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>4096) pad.buttons|=kZR;
    }
    static auto previous=std::chrono::steady_clock::now();
    const auto now=std::chrono::steady_clock::now();
    mouse_look.sample(mouse_dx,-mouse_dy,std::chrono::duration<double>(now-previous).count());
    previous=now;
    mouse_dx=mouse_dy=0;
    const auto owners=catherine::effective_input_devices(config.input_devices,controller_ids());
    const bool menus=input_menu.load();
    const unsigned key_owner=catherine::keyboard_glyph_owner(menus,owners);
    keyboard_glyph_player.store(0,std::memory_order_release);
    PadState keyboard;
    keyboard_mouse_pad(keyboard,catherine::input_profile(config,key_owner).keyboard,key_held,
                       MouseLookFilter::axis(mouse_look.x),MouseLookFilter::axis(mouse_look.y));
    std::vector<PadState> live;
    static std::array<uint64_t,2> previous_buttons{};
    for (unsigned i=0;i<2;++i) {
        if (controllers[i]) live.push_back(raw[i]);
        if (raw[i].buttons&~previous_buttons[i]) input_layout.store(unsigned(controller_identity[i].layout));
        previous_buttons[i]=raw[i].buttons;
    }
    if (keyboard.buttons || keyboard.rx || keyboard.ry) input_layout.store(unsigned(InputLayout::KeyboardMouse));
    const PadState menu_pad=catherine::menu_input_pad({},live,config.bindings,config.swap_sticks);
    const auto all_buttons=catherine::raw_capture_buttons(live);
    for (unsigned player=0;player<2;++player) {
        PadState& pad=pad_state[player];
        const auto& owner=owners[player];
        const auto& profile=catherine::input_profile(config,catherine::input_binding_profile(menus,player));
        pad={};
        pad_connected[player]=player==0;
        raw_buttons[player].store(all_buttons);
        if (owner=="auto") pad=catherine::menu_input_pad(keyboard,live,profile.bindings,profile.swap_sticks);
        else if (owner=="keyboard") {pad=keyboard;pad_connected[player]=true;}
        else if (owner!="none") {
            for (unsigned i=0;i<2;++i) if (controller_identity[i].id==owner) {
                pad=raw[i];
                pad.buttons=catherine::remap_input_buttons(pad.buttons,profile.bindings);
                swap_controller_sticks(pad,profile.swap_sticks);
                pad_connected[player]=true;
                break;
            }
        }
    }
    if (menus) pad_state[0]=catherine::menu_input_pad(keyboard,live,config.bindings,config.swap_sticks);
    if (owners[0] == "keyboard") input_layout.store(unsigned(InputLayout::KeyboardMouse));
    else if (owners[0] != "auto") {
        for (unsigned i=0;i<2;++i) if (controller_identity[i].id==owners[0])
            input_layout.store(unsigned(controller_identity[i].layout));
    }
    unsigned overlay=0;
    const uint64_t buttons=menu_pad.buttons;
    for (auto [mask,action]:std::array<std::pair<uint64_t,unsigned>,6>{
            {{kUp,1},{kDown,2},{kLeft,4},{kRight,8},{kA,16},{kB,32}}})
        if (buttons&mask) overlay|=action;
    catherine_dev_overlay_pad(overlay);
    if (catherine_dev_gui_input_blocked()) pad_state[0]=pad_state[1]={};
}

[[noreturn]] void close_game(int result) {
    if (window) SDL_HideWindow(window);
    catherine_linux_freeze_saves();
    // The guest owns process-lifetime render and audio threads. Running C++
    // static destructors while they still use those objects races shutdown.
    std::_Exit(result);
}

void update_window() {
    int width=0,height=0;
    SDL_Vulkan_GetDrawableSize(window,&width,&height);
    drawable_size.store((uint64_t(uint32_t(width))<<32)|uint32_t(height));
    minimized.store((SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)!=0);
}
}

extern "C" int catherine_linux_run(int (*guest)()) {
    // XWayland accepts initial window positions. Native Wayland leaves placement
    // to the compositor. An explicitly selected SDL backend still takes precedence.
    catherine_linux_video_driver();
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE,"0");
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr,"SDL initialization failed: %s\n",SDL_GetError());
        return 2;
    }
    const bool hidden = headless();
    const auto config=catherine::host_settings();
    const char* fullscreen_override=std::getenv("NX_FULLSCREEN");
    const bool fullscreen=fullscreen_override?fullscreen_override[0]=='1':config.fullscreen;
    const int display=launch_display();
    const int height=config.window_height>0?config.window_height:auto_window_height(display);
    const int position=SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    window=SDL_CreateWindow(nx::game::kInfo.title,position,position,
        height*16/9,height,SDL_WINDOW_VULKAN|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_RESIZABLE|
        (hidden?SDL_WINDOW_HIDDEN:fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:SDL_WINDOW_SHOWN));
    if (!window) {
        std::fprintf(stderr,"SDL Vulkan window creation failed: %s\n",SDL_GetError());
        SDL_Quit();
        return 2;
    }
    unsigned count=0;
    if (SDL_Vulkan_GetInstanceExtensions(window,&count,nullptr)) {
        std::vector<const char*> extensions(count);
        if (SDL_Vulkan_GetInstanceExtensions(window,&count,extensions.data()))
            for (const char* extension:extensions)
                if (std::strcmp(extension,VK_KHR_SURFACE_EXTENSION_NAME)) surface_extension=extension;
    }
    if (!surface_extension) {
        std::fprintf(stderr,"SDL Vulkan surface extension unavailable: %s\n",SDL_GetError());
        SDL_Quit();
        return 2;
    }
    active_mode={fullscreen,config.window_height};
    if (!hidden && !fullscreen) center_window(display);
    bool centered_after_show=hidden || fullscreen;
    const int initial_display=SDL_GetWindowDisplayIndex(window);
    catherine::initialize_display_mode(display_mode(initial_display),fullscreen,config.window_height);
    cairo_surface_t* icon=catherine_game_icon_png();
    catherine_set_window_icon(window,icon);
    cairo_surface_destroy(icon);
    std::fprintf(stderr,"Catherine startup window %dx%d on %s (%s), internal render height %u\n",
                 height*16/9,height,SDL_GetDisplayName(initial_display),
                 SDL_GetCurrentVideoDriver(),catherine_internal_render_height());
    update_window();
    bool text_active=false,mouse_captured=false;
    const auto gameplay_hides_pointer=[] {
        return hide_cursor.load() && !catherine_fullscreen_movie_active();
    };
    const auto release_mouse=[&] {
        if (!mouse_captured) return;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        mouse_captured=false;
        mouse_buttons=0;
        mouse_dx=mouse_dy=0;
        mouse_look={};
    };
    catherine_linux_overlay_startup();
    std::atomic<bool> finished{false};
    int result=0;
    std::thread game([&] {result=guest();finished.store(true,std::memory_order_release);});
    while (!finished.load(std::memory_order_acquire)) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event,8)) do {
            if (event.type==SDL_QUIT ||
                (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID==SDL_GetWindowID(window))) close_game(0);
            if (event.type==SDL_CONTROLLERDEVICEADDED && !hidden && SDL_IsGameController(event.cdevice.which)) {
                for (auto& controller:controllers)
                    if (!controller) {
                        controller=SDL_GameControllerOpen(event.cdevice.which);
                        if (controller) {
                            std::lock_guard lock(input_lock);
                            const unsigned slot=&controller-controllers;
                            controller_identity[slot]=identify_controller(controller,slot);
                        }
                        break;
                    }
            }
            if (event.type==SDL_CONTROLLERDEVICEREMOVED)
                for (auto& controller:controllers)
                    if (controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))==event.cdevice.which) {
                        {
                            std::lock_guard lock(input_lock);
                            controller_identity[&controller-controllers]={};
                        }
                        SDL_GameControllerClose(controller);controller=nullptr;
                    }
            if (event.type==SDL_WINDOWEVENT) {
                if (event.window.event==SDL_WINDOWEVENT_SHOWN && !centered_after_show) {
                    center_window(display);
                    centered_after_show=true;
                }
                if (event.window.windowID==SDL_GetWindowID(window) &&
                    (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST ||
                     event.window.event==SDL_WINDOWEVENT_MINIMIZED ||
                     event.window.event==SDL_WINDOWEVENT_HIDDEN)) {
                    release_mouse();
                    keys.fill(false);
                    mouse_buttons=0;
                    wheel_until=0;
                }
                if (event.window.event==SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                    std::lock_guard lock(window_lock);
                    const auto display=display_mode(event.window.data1);
                    catherine::update_display_mode(display);
                    const unsigned target=catherine::display_render_height(display,active_mode.fullscreen,active_mode.height);
                    if (!preview && target!=catherine_resolution_applied_height())
                        catherine_resolution_request(target);
                }
                update_window();
            }
            if (event.type==SDL_MOUSEMOTION || event.type==SDL_MOUSEBUTTONDOWN) {
                int width,height;
                SDL_GetWindowSize(window,&width,&height);
                const int x=event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x;
                const int y=event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y;
                catherine_linux_overlay_point(x,y,width,height);
            }
            const bool consumed=catherine_linux_overlay_event(event);
            if (!gameplay_hides_pointer() || catherine_dev_overlay_visible() ||
                (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE && !event.key.repeat))
                release_mouse();
            if (event.type==SDL_MOUSEBUTTONUP) mouse_buttons&=~SDL_BUTTON(event.button.button);
            if (!consumed && !catherine_dev_gui_input_blocked()) {
                if (event.type==SDL_MOUSEMOTION) {mouse_dx+=event.motion.xrel;mouse_dy+=event.motion.yrel;}
                if (event.type==SDL_MOUSEBUTTONDOWN) {
                    if (!hidden && !mouse_captured && gameplay_hides_pointer() && event.button.button==SDL_BUTTON_LEFT &&
                        event.button.windowID==SDL_GetWindowID(window) &&
                        (SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)) {
                        // SDL relative mode hides and confines the pointer together.
                        mouse_captured=SDL_SetRelativeMouseMode(SDL_TRUE)==0;
                        if (mouse_captured) {
                            mouse_dx=mouse_dy=0;
                            mouse_look={};
                        }
                    }
                    mouse_buttons|=SDL_BUTTON(event.button.button);
                }
                if (event.type==SDL_MOUSEWHEEL && event.wheel.y) {
                    const int delta=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
                    wheel_key=delta>0?catherine::kWheelUp:catherine::kWheelDown;
                    wheel_until=SDL_GetTicks64()+150;
                }
            }
            if (event.type==SDL_KEYUP) keys[event.key.keysym.scancode]=false;
            if (event.type==SDL_KEYDOWN && !consumed) {
                const auto sym=event.key.keysym.sym;
                if (sym==SDLK_F9 || sym==SDLK_F10) {
                    if (event.key.repeat) continue;
                    std::lock_guard lock(window_lock);
                    if (preview) continue;
                    auto settings=catherine::host_settings();
                    const int effective=catherine_requested_refresh_rate();
                    settings.fps=catherine::step_fps_limit(effective,sym==SDLK_F10?1:-1);
                    if (settings.fps!=effective) catherine::store_host_settings(settings,true);
                } else if (sym==SDLK_F11 || (sym==SDLK_RETURN && (event.key.keysym.mod&KMOD_ALT))) {
                    if (event.key.repeat || hidden) continue;
                    std::lock_guard lock(window_lock);
                    if (preview) continue;
                    auto settings=catherine::host_settings();
                    uint64_t generation=0;
                    const WindowMode requested{!active_mode.fullscreen,active_mode.height};
                    if (set_window_mode(requested,generation)) {
                        settings.fullscreen=requested.fullscreen;
                        catherine::store_host_settings(settings);
                    }
                } else keys[event.key.keysym.scancode]=true;
            }
        } while (SDL_PollEvent(&event));
        service_window_mode();
        update_window();
        const bool want_text=catherine_dev_overlay_visible();
        if (want_text!=text_active) {
            if (want_text) { keys.fill(false);mouse_buttons=0;wheel_until=0;SDL_StartTextInput(); }
            else SDL_StopTextInput();
            text_active=want_text;
        }
        if (mouse_captured && (!gameplay_hides_pointer() || want_text)) release_mouse();
        update_input();
        update_window_title(SDL_GetTicks64());
    }
    game.join();
    close_game(result);
}

namespace nx::plat {
bool reserve(uint64_t addr,uint64_t size) {return VirtualAlloc(reinterpret_cast<void*>(addr),size,MEM_RESERVE,PAGE_NOACCESS)!=nullptr;}
bool commit(uint64_t addr,uint64_t size) {
    const uint64_t first=addr&~4095ull,end=(addr+size+4095)&~4095ull;
    return VirtualAlloc(reinterpret_cast<void*>(first),end-first,MEM_COMMIT,PAGE_READWRITE)!=nullptr;
}
void decommit(uint64_t addr,uint64_t size) {VirtualFree(reinterpret_cast<void*>(addr),size,MEM_DECOMMIT);}
void start_thread(ThreadFn function,void* argument,size_t stack_size) {
    struct Start {ThreadFn function;void* argument;};
    auto* start=new Start{function,argument};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    int error=stack_size?pthread_attr_setstacksize(&attr,std::max(size_t(PTHREAD_STACK_MIN),stack_size)):0;
    pthread_t thread;
    if (!error) error=pthread_create(&thread,&attr,[](void* data)->void* {
        Start value=*static_cast<Start*>(data);
        delete static_cast<Start*>(data);
        value.function(value.argument);
        return nullptr;
    },start);
    pthread_attr_destroy(&attr);
    if (error) {delete start;std::fprintf(stderr,"Could not start host thread: %s\n",std::strerror(error));std::abort();}
}
void exit_thread() {pthread_exit(nullptr);}
void exit_process(int code) {std::exit(code);}
void sleep_ns(int64_t ns) {if(ns>0)std::this_thread::sleep_for(std::chrono::nanoseconds(ns));}
void yield() {std::this_thread::yield();}
void set_thread_name(const char* name) {
    char short_name[16];
    std::snprintf(short_name,sizeof short_name,"%.15s",name);
    pthread_setname_np(pthread_self(),short_name);
}
int64_t current_thread_id() {return syscall(SYS_gettid);}
bool profile(const int64_t*,const uint64_t* const*,size_t,int,const std::string&) {return false;}
uint64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
FILE* fopen_utf8(const std::string& path,const char* mode) {return std::fopen(path.c_str(),mode);}
int64_t ftell64(FILE* file) {return ftello(file);}
int fseek64(FILE* file,int64_t offset,int origin) {return fseeko(file,offset,origin);}
uint64_t read_at(FILE* file,int64_t offset,void* buffer,uint64_t size) {
    uint64_t total=0;
    while(total<size) {
        ssize_t count=pread(fileno(file),static_cast<char*>(buffer)+total,size-total,offset+total);
        if(count<0 && errno==EINTR)continue;
        if(count<=0)break;
        total+=count;
    }
    return total;
}
bool make_dirs(const std::string& path) {
    std::error_code error;
    std::filesystem::create_directories(path,error);
    return !error;
}
void log_line(const char* line) {std::fprintf(stderr,"%s\n",line);}
std::string data_root() {const char* path=std::getenv("NX_DATA_ROOT");return path?path:nx::game::kInfo.pc_data_root;}
std::string setting(const std::string& key) {
    if(const char* value=std::getenv(("NX_"+key).c_str()))return value;
    return {};
}
float default_render_scale() {return 1.f;}
#ifdef CATHERINE_HIGH_REFRESH_BUILD
int anti_aliasing() {return catherine::requested_anti_aliasing();}
InputLayout active_input_layout() noexcept {return InputLayout(input_layout.load(std::memory_order_relaxed));}
InputLayout prompt_input_layout() noexcept {
    return InputLayout(catherine::resolve_button_prompt_layout(unsigned(active_input_layout()),catherine::button_prompt_style()));
}
unsigned keyboard_binding(unsigned i) noexcept {
    return catherine::keyboard_binding(i,keyboard_glyph_player.load(std::memory_order_acquire));
}
uint64_t keyboard_binding_version() noexcept {
    const unsigned player=keyboard_glyph_player.load(std::memory_order_acquire);
    return (catherine::keyboard_binding_version(player)<<1)|player;
}
uint64_t controller_binding_map() {return catherine::pad_binding_map();}
#endif
void* create_window() {return window;}
void release_window(void*) {}
uint64_t window_generation() {return 1;}
bool window_minimized() {return minimized.load();}
void window_size(uint32_t& width,uint32_t& height) {
    const uint64_t size=drawable_size.load();
    width=size>>32;height=uint32_t(size);
}
void screen_size(uint32_t& width,uint32_t& height) {window_size(width,height);}
bool headless() {const char* value=std::getenv("NX_HEADLESS");return value && !std::strcmp(value,"1");}
void read_player_pads(PadState (&pads)[2],bool (&connected)[2]) {
    std::lock_guard lock(input_lock);
    std::copy_n(pad_state,2,pads);
    std::copy_n(pad_connected,2,connected);
}
unsigned connected_player_count() noexcept {
    std::lock_guard lock(input_lock);
    return unsigned(pad_connected[0])+unsigned(pad_connected[1]);
}
void read_pad(PadState& pad) {
    PadState players[2];
    bool connected[2];
    read_player_pads(players,connected);
    pad=players[0];
}
void frame_presented() {presented_frames.fetch_add(1,std::memory_order_relaxed);}
void* vulkan_get_instance_proc_addr() {return SDL_Vulkan_GetVkGetInstanceProcAddr();}
const char* vulkan_surface_extension() {return surface_extension;}
int vulkan_create_surface(void* instance,void* native_window,uint64_t* surface,void*) {
    VkSurfaceKHR value{};
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(native_window),
                                  static_cast<VkInstance>(instance),&value)) {
        std::fprintf(stderr,"SDL Vulkan surface creation failed: %s\n",SDL_GetError());
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *surface=reinterpret_cast<uint64_t>(value);
    return VK_SUCCESS;
}
}

namespace {
struct AudioBuffer {
    std::vector<int16_t> pcm;
    size_t offset=0;
    void* tag;
};
std::mutex audio_mutex;
std::deque<AudioBuffer> audio_queue;
SDL_AudioDeviceID audio_device=0;
AudioDone audio_done=nullptr;
unsigned audio_channels=0;
uint64_t samples_played=0;
float audio_gain=1.f;

void audio_callback(void*,Uint8* output,int bytes) {
    std::memset(output,0,bytes);
    auto* samples=reinterpret_cast<int16_t*>(output);
    size_t remaining=bytes/sizeof(int16_t);
    std::array<void*,32> completed;
    size_t done=0;
    {
        std::lock_guard lock(audio_mutex);
        while (remaining && !audio_queue.empty()) {
            auto& front=audio_queue.front();
            const size_t count=std::min(remaining,front.pcm.size()-front.offset);
            for (size_t i=0;i<count;++i)
                samples[i]=int16_t(std::clamp(front.pcm[front.offset+i]*audio_gain,-32768.f,32767.f));
            samples+=count;
            remaining-=count;
            samples_played+=count/audio_channels;
            front.offset+=count;
            if (front.offset==front.pcm.size()) {
                completed[done++]=front.tag;
                audio_queue.pop_front();
            }
        }
    }
    for (size_t i=0;i<done;++i) audio_done(completed[i]);
}
}
namespace nx::plat {
bool audio_open(AudioDone done,uint32_t channels) {
    if (audio_device) return done==audio_done && channels==audio_channels;
    if (headless() || std::getenv("NX_MUTE")) SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) return false;
    SDL_AudioSpec desired{};
    desired.freq=48000;
    desired.format=AUDIO_S16SYS;
    desired.channels=channels;
    desired.samples=512;
    desired.callback=audio_callback;
    audio_device=SDL_OpenAudioDevice(nullptr,0,&desired,nullptr,0);
    if (!audio_device) return false;
    audio_channels=channels;
    audio_done=done;
    SDL_PauseAudioDevice(audio_device,0);
    return true;
}
bool audio_submit(const void* data,size_t bytes,void* ctx) {
    if (!audio_device || !bytes || bytes%(sizeof(int16_t)*audio_channels)) return false;
    AudioBuffer buffer{std::vector<int16_t>(bytes/sizeof(int16_t)),0,ctx};
    std::memcpy(buffer.pcm.data(),data,bytes);
    std::lock_guard lock(audio_mutex);
    if (audio_queue.size()>=32) return false;
    audio_queue.push_back(std::move(buffer));
    return true;
}
uint32_t audio_queued() {std::lock_guard lock(audio_mutex);return audio_queue.size();}
void audio_pause(bool paused) {if (audio_device) SDL_PauseAudioDevice(audio_device,paused);}
}
extern "C" int host_audio_open(void (*done)(void*),uint32_t channels) {return audio_open(done,channels);}
extern "C" int host_audio_submit(const void* data,size_t bytes,void* tag) {return audio_submit(data,bytes,tag);}
extern "C" void host_audio_set_running(int running) {audio_pause(!running);}
extern "C" uint64_t host_audio_played_samples() {std::lock_guard lock(audio_mutex);return samples_played;}
extern "C" int host_audio_flush() {
    std::deque<AudioBuffer> removed;
    {
        std::lock_guard lock(audio_mutex);
        removed.swap(audio_queue);
    }
    for(const auto& buffer:removed)audio_done(buffer.tag);
    return !removed.empty();
}
extern "C" void host_audio_set_volume(float volume) {
    std::lock_guard lock(audio_mutex);
    audio_gain=volume;
}
extern "C" int catherine_guest_precise_sleep(int64_t ns) {
    if(ns<=0)return 0;
    sleep_ns(ns);
    return 1;
}
extern "C" int catherine_multiplayer_available() {return connected_player_count()>=2;}
extern "C" void catherine_pc_exit_game() {
    SDL_Event event{};
    event.type=SDL_QUIT;
    SDL_PushEvent(&event);
}

extern "C" int catherine_dev_overlay_mouse(unsigned field);
extern "C" int catherine_pc_mouse(unsigned field) {
    if (catherine_dev_gui_input_blocked()) return 0;
    if (field==7) return key_held(1);
    const int value=catherine_dev_overlay_mouse(field);
    return field==5?int(unsigned(value)*120):value;
}
extern "C" const char* catherine_keyboard_label(unsigned index) {
    const auto config=catherine::bindings_settings();
    const auto& profile=catherine::input_profile(config,binding_player.load());
    return catherine::keyboard_key_name(index<profile.keyboard.size()?profile.keyboard[index]:0);
}
extern "C" const char* catherine_input_device_label(unsigned player,unsigned index) {
    if (!index) return player?"None":"Auto";
    if (index==1) return "Keyboard & Mouse";
    std::lock_guard lock(input_lock);
    static thread_local std::array<std::string,2> labels;
    unsigned current=2;
    for (const auto& device:controller_identity) {
        if (device.id.empty()) continue;
        if (current++==index) {
            labels[index-2]=device.name;
            return labels[index-2].c_str();
        }
    }
    return "Disconnected";
}

extern "C" int catherine_menu_cursor_visible(void);
extern "C" int catherine_config_setting(unsigned field,int change) {
    using namespace catherine;
    auto config=host_settings();
    if (field==PC_BINDING_PLAYER) {
        if (change==1 || change==2) binding_player.store(change-1);
        return binding_player.load();
    }
    if (field==PC_INPUT_MENU_MODE) {
        if (change) input_menu.store(change==2);
        return input_menu.load();
    }
    if (field==PC_CURSOR_HIDDEN) {change = change && !catherine_menu_cursor_visible();hide_cursor.store(change!=0);return change!=0;}
    if (field==PC_MENU_KEYBOARD_ALLOWED) return 1;
    if (field==PC_CONNECTED_PLAYERS) return connected_player_count();
    if (field==PC_INPUT_LAYOUT || field==PC_CAMERA_INPUT_LAYOUT) return input_layout.load();
    if (field==PC_DUALSENSE) return prompt_input_layout()==InputLayout::PlayStation || prompt_input_layout()==InputLayout::PS4;
    if (field==PC_RAW_KEY) return catherine_dev_gui_input_blocked()?0:raw_key.load();
    if (field==PC_APPLY_BUTTON || field==PC_RAW_PAD) {
        std::lock_guard lock(input_lock);
        if (catherine_dev_gui_input_blocked()) return 0;
        const unsigned player=binding_player.load();
        return field==PC_APPLY_BUTTON?int((pad_state[player].buttons&kX)!=0):int(raw_buttons[player].load()&0xffff);
    }
    if (field==PC_INPUT_DEVICE_COUNT || field==PC_PLAYER1_DEVICE || field==PC_PLAYER2_DEVICE) {
        std::vector<std::string> ids;
        {std::lock_guard lock(input_lock);ids=controller_ids();}
        if (field==PC_INPUT_DEVICE_COUNT) return ids.size();
        const unsigned player=field-PC_PLAYER1_DEVICE;
        if (!change) {
            const auto& owner=config.input_devices[player];
            if (owner=="keyboard") return 1;
            const auto found=std::find(ids.begin(),ids.end(),owner);
            return found==ids.end()?0:2+int(found-ids.begin());
        }
        const unsigned index=unsigned(change-1);
        if (index>=ids.size()+2) return 0;
        const std::string target=index==0?(player?"none":"auto"):index==1?"keyboard":ids[index-2];
        assign_input_device(config.input_devices,player,target,ids);
        return store_host_settings(config);
    }
    if (field==PC_BINDINGS_BEGIN) {begin_bindings_settings(binding_player.load());return 1;}
    if (field==PC_BINDINGS_DIRTY) return bindings_settings_dirty();
    if (field==PC_BINDINGS_ACTIVE) return bindings_settings_active();
    if (field==PC_BINDINGS_SAVE) return save_bindings_settings();
    if (field==PC_BINDINGS_DISCARD) {discard_bindings_settings();return 1;}
    if (field==PC_CONFIG_BEGIN) {begin_config_settings();return 1;}
    if (field==PC_CONFIG_SAVE) return save_config_settings();
    if (field==PC_CONFIG_END) {
        if (end_config_settings()) {
            config=host_settings();
            std::lock_guard lock(window_lock);
            const WindowMode saved{config.fullscreen,config.window_height};
            if (active_mode!=saved || preview) queued_mode=saved;
            draft_mode.reset();
            preview.reset();
        }
        return 1;
    }

    if (field==PC_FPS) {
        constexpr int values[]{-1,30,60,120,240,0};
        if (!change) return std::find(std::begin(values),std::end(values),config.fps)-std::begin(values);
        config.fps=change>=100?values[std::clamp(change-100,0,5)]:step_fps_limit(config.fps,change);
        return store_host_settings(config,true);
    }
    if (field==PC_AA || field==PC_MOVIES || field==PC_BUTTON_PROMPTS) {
        constexpr int aa[]{0,1,2,4,8},movies[]{0,60,120,240,-1},prompts[]{0,1,2,3,4};
        const int* values=field==PC_AA?aa:field==PC_MOVIES?movies:prompts;
        auto& setting=field==PC_AA?config.anti_aliasing:field==PC_MOVIES?config.movie_interpolation:config.button_prompts;
        int index=std::find(values,values+5,setting)-values;
        if (!change) return index%5;
        index=change>=100?std::clamp(change-100,0,4):(index+(change<0?4:1))%5;
        setting=values[index];
        return store_host_settings(config);
    }
    if (field==PC_MODE || field==PC_RESOLUTION || field==PC_APPLY ||
        field==PC_DISCARD_DRAFT || field==PC_PREVIEW || field==PC_KEEP || field==PC_REVERT)
        return window_setting(field,change,config);
    auto draft=bindings_settings();
    auto& profile=input_profile(draft,binding_player.load());
    if (field>=PC_BINDING && field<PC_RESET_BINDINGS) {
        const unsigned shift=(field-PC_BINDING)*4;
        if (!change) return (profile.bindings>>shift)&15;
        if (field-PC_BINDING>=12 || change<1 || change>12) return 0;
        const unsigned old=(profile.bindings>>shift)&15, next=unsigned(change-1);
        for (unsigned bit=0;bit<12;++bit)
            if (((profile.bindings>>(bit*4))&15)==next) {
                profile.bindings=(profile.bindings&~(15ull<<(bit*4)))|(uint64_t(old)<<(bit*4));
                break;
            }
        profile.bindings=(profile.bindings&~(15ull<<shift))|(uint64_t(next)<<shift);
        return store_bindings_draft(draft);
    }
    if (field>=PC_KEY_BINDING && field<PC_RESET_KEYBOARD) {
        const unsigned index=field-PC_KEY_BINDING;
        if (!change) return profile.keyboard[index];
        return rebind_keyboard(profile.keyboard,index,change,profile.movement_preset) && store_bindings_draft(draft);
    }
    if (field==PC_SWAP_STICKS) {
        if (!change) return profile.swap_sticks;
        profile.swap_sticks=change>=100?change!=100:!profile.swap_sticks;
        return store_bindings_draft(draft);
    }
    if (field==PC_MOVEMENT_PRESET) {
        if (!change) return profile.movement_preset;
        const int next=change>=100?std::clamp(change-100,0,2):(profile.movement_preset+(change<0?2:1))%3;
        if (!change_movement_preset(profile.keyboard,profile.movement_preset,next)) return 0;
        profile.movement_preset=next;
        return store_bindings_draft(draft);
    }
    if (field==PC_RESET_BINDINGS || field==PC_RESET_KEYBOARD) {
        if (!change) return 0;
        if(field==PC_RESET_BINDINGS){profile.bindings=kDefaultBindings;profile.swap_sticks=false;}
        else profile.keyboard=default_keyboard(profile.movement_preset);
        return store_bindings_draft(draft);
    }
    return 0;
}

namespace { catherine::tutorial::TutorialAudio tutorial_audio; }
extern "C" void catherine_tutorial_audio_header(uint64_t decoder,uint64_t a,unsigned na,uint64_t b,unsigned nb) {
    tutorial_audio.header(decoder,reinterpret_cast<const void*>(a),na,reinterpret_cast<const void*>(b),nb);
}
extern "C" unsigned catherine_tutorial_audio_pcm(uint64_t decoder,uint64_t planes,unsigned frames) {
    return tutorial_audio.pcm(decoder,reinterpret_cast<const void*>(planes),frames);
}
extern "C" uint64_t catherine_tutorial_message(uint64_t raw) {
    const auto* source=reinterpret_cast<const uint16_t*>(raw);
    if (!source || source[0]!=0xd841 || source[1]!=0xda61 ||
        catherine::tutorial::topic(source[2])==catherine::tutorial::Topic::None) return raw;
    catherine::tutorial::Input input;
    input.layout=unsigned(prompt_input_layout());
    input.mapping=controller_binding_map();
    for (unsigned i=0;i<input.keys.size();++i) input.keys[i]=uint16_t(keyboard_binding(i));
    auto text=catherine::tutorial::replace(source,input);
    if (text.empty()) return raw;
    tutorial_audio.snapshot(catherine::tutorial::topic(source[2]),input);
    static thread_local std::array<std::u16string,8> buffers;
    static thread_local unsigned next=0;
    auto& buffer=buffers[next++%buffers.size()];
    buffer=std::move(text);
    return reinterpret_cast<uint64_t>(buffer.data());
}
