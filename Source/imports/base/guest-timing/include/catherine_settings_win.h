#pragma once

#include <array>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif
#include "catherine_keyboard.h"

namespace catherine {

#ifdef _WIN32
using HostDisplay = HMONITOR;
#else
struct HostDisplay {
    int width = 1280;
    int height = 720;
    int hz = 60;
};
#endif

inline constexpr int kAutoFps = -1;
inline constexpr int kUnlimitedFps = 0;
inline constexpr uint64_t kIdentityBindings = 0xfedcba9876543210ull;
// Swap the east/south face-button actions on every gamepad, for both players.
inline constexpr uint64_t kDefaultBindings = 0xfedcba9876543201ull;

struct InputProfile {
    uint64_t bindings = kDefaultBindings;
    std::array<uint16_t,kKeyboardBindings> keyboard = kDefaultKeyboard;
    int movement_preset = 0;
    bool swap_sticks = false;
    bool operator==(const InputProfile&) const = default;
};

// Inherit P1's legacy fields so existing graphics/settings clients retain their ABI.
struct HostSettings : InputProfile {
    int fps = 60;
    bool fullscreen = false;
    int window_height = 0;
    int anti_aliasing = 0;
    int movie_interpolation = 0;
    InputProfile player2;
    // "auto"/"keyboard"/backend identity; P2 uses "none" instead of "auto".
    std::array<std::string,2> input_devices{"auto","none"};
    int button_prompts = 0; // Auto / Xbox / Switch / PS4 / PS5; gamepads only.
    bool vsync = false;
    bool triple_buffering = false;
    bool alt_build_mode = false;
    int internal_upscaling = 1; // Off / Auto / 1080p / 1440p / 2160p
};
inline InputProfile& input_profile(HostSettings& settings,unsigned player) {
    return player ? settings.player2 : static_cast<InputProfile&>(settings);
}
inline const InputProfile& input_profile(const HostSettings& settings,unsigned player) {
    return player ? settings.player2 : static_cast<const InputProfile&>(settings);
}

HostSettings host_settings();
HostSettings bindings_settings();
void begin_bindings_settings(unsigned player=0);
bool store_bindings_draft(const HostSettings& value);
bool bindings_settings_dirty();
bool bindings_settings_active();
bool save_bindings_settings();
void discard_bindings_settings();
bool controller_swap_sticks();
void begin_config_settings();
bool save_config_settings();
bool end_config_settings();
int requested_anti_aliasing();
int button_prompt_style();
bool store_host_settings(const HostSettings& value, bool explicit_fps = false);
inline unsigned internal_upscaling_height(int mode, int width, int height, bool fullscreen, int window_height) {
    int available = 720;
    for (int candidate : {1080, 1440, 2160})
        if (height >= candidate && width >= candidate * 16 / 9 &&
            (fullscreen || !window_height || window_height >= candidate)) available = candidate;
    const int requested = mode == 1 ? available : mode == 2 ? 1080 : mode == 3 ? 1440 : mode == 4 ? 2160 : 720;
    return unsigned(requested < available ? requested : available);
}
unsigned display_render_height(HostDisplay monitor, bool fullscreen, int window_height);
void update_display_mode(HostDisplay monitor);
#ifndef _WIN32
void initialize_display_mode(HostDisplay monitor, bool fullscreen, int window_height);
#endif
int step_fps_limit(int effective, int direction);
uint64_t remap_pad_buttons(uint64_t buttons);
uint64_t pad_binding_map();
unsigned keyboard_binding(unsigned index,unsigned player=0);
uint64_t keyboard_binding_version(unsigned player=0);

}  // namespace catherine
