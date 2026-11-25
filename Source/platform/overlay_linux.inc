#include <atomic>
#include <deque>
#include <mutex>
#include <string_view>
#include <filesystem>
#include <SDL2/SDL.h>
#include "C:/catherine/recomp/Source/platform/window_linux.h"
#include "gui.h"
#include "keyboard_mouse_policy.h"
static bool catherine_linux_checkpoint_available() {
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::symlink_status("dev-checkpoints.pak",error));
}
#include "dev-start/runtime/dev_level_overlay.h"

extern "C" int catherine_dev_enabled(void);

namespace {
std::atomic<bool> overlay_open{false};
std::atomic<bool> pause_supported{false};
std::atomic<unsigned> generation{0}, actions{0};
std::atomic<uint64_t> release_until{0};
std::atomic<unsigned> mouse[7]{};
std::mutex text_lock;
std::deque<unsigned> text_queue;

void open_overlay() {
    if (!catherine_dev_enabled()) {
        catherine_dev_loader_set_startup_menu(0);
        return;
    }
    std::lock_guard lock(text_lock);
    text_queue.clear();
    actions.store(0);
    generation.fetch_add(1);
    overlay_open.store(true);
}
void add_character(unsigned character) {
    std::lock_guard lock(text_lock);
    if (text_queue.size() < 128) text_queue.push_back(character);
}
}

extern "C" unsigned catherine_dev_overlay_generation() { return generation.load(); }
extern "C" int catherine_dev_overlay_visible() { return overlay_open.load(); }
extern "C" void catherine_dev_overlay_close() {
    if (!overlay_open.exchange(false)) return;
    release_until.store(SDL_GetTicks64() + 180);
    catherine_dev_loader_set_startup_menu(0);
}
extern "C" unsigned catherine_dev_overlay_actions() { return actions.exchange(0); }
extern "C" unsigned catherine_dev_overlay_character() {
    std::lock_guard lock(text_lock);
    if (text_queue.empty()) return 0;
    const unsigned character = text_queue.front();
    text_queue.pop_front();
    return character;
}
extern "C" void catherine_dev_overlay_pad(unsigned held) {
    static std::atomic<unsigned> previous{0};
    const unsigned edge = held & ~previous.exchange(held);
    if (overlay_open.load()) actions.fetch_or(edge);
}
extern "C" int catherine_dev_overlay_mouse(unsigned field) {
    return field < 7 ? static_cast<int>(mouse[field].load()) : 0;
}
extern "C" int catherine_dev_gui_pause_requested() {
    return overlay_open.load() && pause_supported.load();
}
extern "C" int catherine_dev_gui_input_blocked() {
    return overlay_open.load() || SDL_GetTicks64() < release_until.load();
}
extern "C" void catherine_dev_gui_set_pause_supported(int supported) {
    pause_supported.store(supported != 0);
}
extern "C" void catherine_dev_gui_set_paused(int) {}
extern "C" void catherine_dev_gui_set_choices(unsigned, const char *const *, const int *, unsigned) {}

void catherine_linux_overlay_startup() {
    if (catherine_dev_loader_startup_menu()) open_overlay();
}

void catherine_linux_overlay_point(int x, int y, int width, int height) {
    const auto point=nx::plat::content_pointer(x,y,width,height);
    mouse[0].store(point.x);
    mouse[1].store(point.y);
    mouse[4].store(point.inside);
    mouse[2].fetch_add(1);
}

bool catherine_linux_overlay_event(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F8 && !event.key.repeat) {
        if (overlay_open.load()) catherine_dev_overlay_close();
        else open_overlay();
        return true;
    }
    const bool opened=overlay_open.load();
    if (event.type==SDL_MOUSEBUTTONDOWN) {
        if (event.button.button==SDL_BUTTON_LEFT) mouse[3].fetch_add(1);
        if (event.button.button==SDL_BUTTON_RIGHT) mouse[6].fetch_add(1);
        return opened;
    }
    if (event.type==SDL_MOUSEWHEEL) {
        const int delta=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
        mouse[5].fetch_add(delta);
        return opened;
    }
    if (!opened) return false;
    switch (event.type) {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
        case SDLK_UP: actions.fetch_or(1); break;
        case SDLK_DOWN: actions.fetch_or(2); break;
        case SDLK_LEFT: actions.fetch_or(4); break;
        case SDLK_RIGHT: actions.fetch_or(8); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: actions.fetch_or(16); break;
        case SDLK_ESCAPE: actions.fetch_or(32); break;
        case SDLK_PAGEUP: actions.fetch_or(64); break;
        case SDLK_PAGEDOWN: actions.fetch_or(128); break;
        case SDLK_TAB: actions.fetch_or(event.key.keysym.mod & KMOD_SHIFT ? 512 : 256); break;
        case SDLK_BACKSPACE: add_character(8); break;
        default: break;
        }
        return true;
    case SDL_TEXTINPUT:
        for (unsigned char c : std::string_view(event.text.text))
            if (c >= 32 && c <= 126) add_character(c);
        return true;
    default: return false;
    }
}
