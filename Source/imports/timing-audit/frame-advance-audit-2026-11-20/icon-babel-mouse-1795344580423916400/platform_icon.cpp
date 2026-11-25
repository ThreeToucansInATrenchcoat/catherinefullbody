#include "perf_background.h"
// Windows host: memory, threads, time, files, the game window (F11 / Alt+Enter fullscreen), gamepad + keyboard,
// XAudio2, the Vulkan loader and win32 surface, crash handler, hardware watchpoints (NX_WATCH) and main().
#include <algorithm>
#include <cctype>
#include <climits>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <mutex>
#include <map>

#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <xaudio2.h>
#include <xinput.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <nx/game.h>
#include <nx/runtime.h>

#include "dualsense_win.h"
#include "controller_prompt_policy.h"
#include "controller_tutorial_audio.h"
#include "kernel.h"
#include "platform.h"
#include "periodic_render_log.h"
#include "window_area_title.h"

#ifdef CATHERINE_HIGH_REFRESH_BUILD
#include "catherine_refresh.h"
#include "catherine_resolution_live.h"
#include "catherine_settings_win.h"
#include "config_pc_host.h"
#include "keyboard_mouse_policy.h"
#include "player_input_policy.h"
#include "camera_input_policy.h"
#include "gui.h"
#include "windows_gamecontrollers.h"
static_assert(PC_RESET_KEYBOARD-PC_KEY_BINDING==catherine::kKeyboardBindings);
#endif

namespace nx::nvn {
void start_frame_dump_request_worker();
#ifdef CATHERINE_HIGH_REFRESH_BUILD
unsigned msaa_sample_mask();
#endif
}

namespace nx {
HWND g_window = nullptr;
void boot();
}  // namespace nx

namespace nx::plat {

// ---- memory ---------------------------------------------------------------------------------------------------

bool reserve(uint64_t addr, uint64_t size) {
    return VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_RESERVE, PAGE_NOACCESS) != nullptr;
}

bool commit(uint64_t addr, uint64_t size) {
    uint64_t a = addr & ~0xFFFull, e = (addr + size + 0xFFF) & ~0xFFFull;
    return VirtualAlloc(reinterpret_cast<void*>(a), e - a, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void decommit(uint64_t addr, uint64_t size) { VirtualFree(reinterpret_cast<void*>(addr), size, MEM_DECOMMIT); }

// ---- threads and time -----------------------------------------------------------------------------------------

struct ThreadStart {
    ThreadFn fn;
    void* arg;
};

static DWORD WINAPI thread_entry(void* p) {
    ThreadStart s = *static_cast<ThreadStart*>(p);
    delete static_cast<ThreadStart*>(p);
    s.fn(s.arg);
    return 0;
}

void start_thread(ThreadFn fn, void* arg, size_t stack_size) {
    CreateThread(nullptr, stack_size, thread_entry, new ThreadStart{fn, arg}, STACK_SIZE_PARAM_IS_A_RESERVATION,
                 nullptr);
}

void exit_thread() { ExitThread(0); }
void exit_process(int code) { ExitProcess(UINT(code)); }
void sleep_ns(int64_t ns) {
    if (ns <= 0) return;
    // Preserve the present deadline even when Windows uses its coarse scheduler tick.
    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                               TIMER_MODIFY_STATE | SYNCHRONIZE);
        ~Timer() { if (handle) CloseHandle(handle); }
    };
    static thread_local Timer timer;
    LARGE_INTEGER due;
    due.QuadPart = -(ns / 100 + (ns % 100 != 0));
    if (timer.handle && SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE) &&
        WaitForSingleObject(timer.handle, INFINITE) == WAIT_OBJECT_0) return;
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
void yield() { SwitchToThread(); }
void set_thread_name(const char*) {}
float default_render_scale() { return 1.0f; }
void release_window(void*) {}
uint64_t window_generation() { return 1; }
void audio_pause(bool) {}

std::string setting(const std::string& key) {
    std::string env = "NX_";
    for (char ch : key) env += char(std::toupper(static_cast<unsigned char>(ch)));
    const char* v = std::getenv(env.c_str());
    return v ? v : "";
}
int64_t current_thread_id() { return int64_t(GetCurrentThreadId()); }
bool profile(const int64_t*, const uint64_t* const*, size_t, int, const std::string&) { return false; }

uint64_t monotonic_ns() {
    // 32.32 nanoseconds per QPC tick. Keep division out of the replay hot path.
    static const uint64_t ns_per_tick = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return uint64_t((static_cast<unsigned __int128>(1'000'000'000) << 32) / uint64_t(frequency.QuadPart));
    }();
    LARGE_INTEGER tick;
    QueryPerformanceCounter(&tick);
    return uint64_t((static_cast<unsigned __int128>(uint64_t(tick.QuadPart)) * ns_per_tick) >> 32);
}

// ---- files and output -----------------------------------------------------------------------------------------

FILE* fopen_utf8(const std::string& path, const char* mode) {
    std::wstring wp = std::filesystem::u8path(path).wstring();
    std::wstring wm(mode, mode + std::strlen(mode));
    return _wfopen(wp.c_str(), wm.c_str());
}
int64_t ftell64(FILE* f) { return _ftelli64(f); }
int fseek64(FILE* f, int64_t off, int origin) { return _fseeki64(f, off, origin); }
uint64_t read_at(FILE* f, int64_t off, void* buf, uint64_t size) {
    _lock_file(f);  // seek + read as one step for this FILE
    _fseeki64_nolock(f, off, SEEK_SET);
    uint64_t n = _fread_nolock(buf, 1, size, f);
    _unlock_file(f);
    return n;
}
bool make_dirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path), ec);
    return !ec;
}

void log_line(const char* line) { std::fprintf(stderr, "%s\n", line); }

std::string data_root() {
    const char* r = std::getenv("NX_DATA_ROOT");
    return r ? r : nx::game::kInfo.pc_data_root;
}

// ---- window ---------------------------------------------------------------------------------------------------

static std::wstring widen(const char* s) { return std::wstring(s, s + std::strlen(s)); }  // ASCII titles
// The window sets the swapchain extent. The renderer scales from the independent guest render size.

static constexpr DWORD kWindowed = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN;
static bool g_fullscreen = false;
static WINDOWPLACEMENT g_placement{sizeof(WINDOWPLACEMENT)};
static DualSenseInput g_dualsense;
static std::atomic<uint64_t> g_presented_frames{0};
static constexpr UINT_PTR kFpsTimer = 1;

bool dualsense_connected() noexcept { return g_dualsense.connected(); }

void frame_presented() { g_presented_frames.fetch_add(1, std::memory_order_relaxed); }

static void update_window_fps(HWND window) {
    static uint64_t previous_time = monotonic_ns();
    static uint64_t previous_frames = g_presented_frames.load(std::memory_order_relaxed);
    const uint64_t now = monotonic_ns();
    const uint64_t frames = g_presented_frames.load(std::memory_order_relaxed);
    if (now == previous_time) return;
    const double fps = double(frames - previous_frames) * 1e9 / double(now - previous_time);
    previous_time = now;
    previous_frames = frames;

    struct GuestReader { bool operator()(uint64_t address,void* destination,size_t size) const {
        if(address<0x10000ull||address>0x7fffffffffffull-size)return false;
        SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),destination,size,&copied)&&copied==size;
    }} reader;
    const std::wstring area=catherine::window_area::label(catherine::window_area::current(reader));
    const std::wstring location=area.empty()?L"":L"  -  "+area;
    wchar_t caption[256];
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    wchar_t limit[32];
    const int target = catherine_requested_refresh_rate();
    if (target) swprintf_s(limit, L"%d", target);
    else wcscpy_s(limit, L"Unlimited");
    swprintf_s(caption, L"%ls%ls  -  %.1f FPS  -  Cap %ls",
               widen(nx::game::kInfo.title).c_str(), location.c_str(), fps, limit);
#else
    swprintf_s(caption, L"%ls%ls  -  %.1f FPS  -  F11: fullscreen", widen(nx::game::kInfo.title).c_str(), location.c_str(), fps);
#endif
    SetWindowTextW(window, caption);
}

static void toggle_fullscreen(HWND h) {
    const DWORD visibility = IsWindowVisible(h) ? WS_VISIBLE : 0;
    if (!g_fullscreen) {
        MONITORINFO mi{sizeof mi};
        GetWindowPlacement(h, &g_placement);
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(h, GWL_STYLE, WS_POPUP | visibility | WS_CLIPCHILDREN);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, (SWP_FRAMECHANGED | (perf_background_window()?SWP_NOACTIVATE:0)));
    } else {
        SetWindowLongW(h, GWL_STYLE, (kWindowed & ~WS_VISIBLE) | visibility);
        SetWindowPlacement(h, &g_placement);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | (SWP_FRAMECHANGED | (perf_background_window()?SWP_NOACTIVATE:0)));
    }
    g_fullscreen = !g_fullscreen;
}

#ifdef CATHERINE_HIGH_REFRESH_BUILD
static constexpr UINT kConfigSetting = WM_APP + 101;
static constexpr UINT kExitGame = WM_APP + 102;
static constexpr UINT kCursorPolicy = WM_APP + 103;
static HMONITOR g_fps_monitor = nullptr;
static bool g_display_ready = false;
static std::atomic<bool> g_block_return{false};
static int g_window_height = 0;
static unsigned g_render_height = 0;
static uint64_t g_render_generation = 0;
static std::atomic<unsigned> g_raw_pad{0};
static std::atomic<bool> g_apply_button{false};
// Zero means the display rows show the active configuration.
static std::atomic<unsigned> g_display_draft{0};
static ULONGLONG g_preview_deadline = 0;
static std::atomic<bool> g_pc_preview{false};

struct DisplayPreview {
    catherine::HostSettings proposed{};
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    WINDOWPLACEMENT windowed_placement{sizeof(WINDOWPLACEMENT)};
    bool was_fullscreen = false;
    int old_window_height = 0;
};
static DisplayPreview g_preview;

static int max_window_height(HWND window) {
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &mi)) return 0;
    RECT border{};
    AdjustWindowRectExForDpi(&border, kWindowed, FALSE, 0, GetDpiForWindow(window));
    const int width = mi.rcWork.right - mi.rcWork.left - (border.right - border.left);
    const int height = mi.rcWork.bottom - mi.rcWork.top - (border.bottom - border.top);
    return std::max(0, std::min(height, width * 9 / 16));
}

static bool resize_window(HWND window, int height) {
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &mi)) return false;
    RECT size{};
    if (height) {
        if (height > max_window_height(window)) return false;
        size.right = height * 16 / 9;
        size.bottom = height;
    } else {
        const int work_height = mi.rcWork.bottom - mi.rcWork.top;
        const int work_width = mi.rcWork.right - mi.rcWork.left;
        size.bottom = std::min(work_height, work_width * 9 / 16) * 8 / 10;
        size.right = size.bottom * 16 / 9;
    }
    if (!AdjustWindowRectExForDpi(&size, kWindowed, FALSE, 0, GetDpiForWindow(window))) return false;
    const int width = size.right - size.left;
    const int outer_height = size.bottom - size.top;
    if (IsZoomed(window)) ShowWindow(window, perf_background_window()?SW_SHOWNOACTIVATE:SW_RESTORE);
    return SetWindowPos(window, nullptr, (mi.rcWork.left + mi.rcWork.right - width) / 2,
                        (mi.rcWork.top + mi.rcWork.bottom - outer_height) / 2, width, outer_height,
                        SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

static void update_render_resolution(HWND window, bool retry = false) {
    const unsigned desired = catherine::display_render_height(
        MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), g_fullscreen, g_window_height);
    if (desired == g_render_height &&
        !(retry && catherine_resolution_status() == CATHERINE_RESOLUTION_REJECTED)) return;
    const bool pending = g_render_generation && catherine_resolution_completed_generation() < g_render_generation;
    g_render_height = desired;
    if (pending || catherine_resolution_applied_height() != desired)
        g_render_generation = catherine_resolution_request(desired);
}

static void refresh_window_monitor(HWND window, bool force = false) {
    if (!g_display_ready) return;
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (force || monitor != g_fps_monitor) {
        g_fps_monitor = monitor;
        catherine::update_display_mode(monitor);
        update_render_resolution(window);
    }
}

int anti_aliasing() { return catherine::requested_anti_aliasing(); }
uint64_t controller_binding_map() { return catherine::pad_binding_map(); }
static catherine::tutorial::TutorialAudio tutorial_audio;

extern "C" void catherine_tutorial_audio_header(uint64_t decoder,uint64_t a,unsigned na,uint64_t b,unsigned nb) {
    tutorial_audio.header(decoder,reinterpret_cast<const void*>(a),na,reinterpret_cast<const void*>(b),nb);
}

extern "C" unsigned catherine_tutorial_audio_pcm(uint64_t decoder,uint64_t planes,unsigned frames) {
    return tutorial_audio.pcm(decoder,reinterpret_cast<const void*>(planes),frames);
}

extern "C" uint64_t catherine_tutorial_message(uint64_t raw) {
    const auto* source=reinterpret_cast<const uint16_t*>(raw);
    static unsigned traces=0;
    if(source&&getenv("CATHERINE_TUTORIAL_TRACE")&&traces++<80)fprintf(stderr,"Tutorial builder trace: %04x %04x %04x %04x %04x %04x\n",source[0],source[1],source[2],source[3],source[4],source[5]);
    if(!source||source[0]!=0xd841||source[1]!=0xda61||catherine::tutorial::topic(source[2])==catherine::tutorial::Topic::None)return raw;
    catherine::tutorial::Input input;input.layout=unsigned(prompt_input_layout());input.mapping=controller_binding_map();
    for(unsigned i=0;i<input.keys.size();++i)input.keys[i]=uint16_t(keyboard_binding(i));
    auto text=catherine::tutorial::replace(source,input);if(text.empty())return raw;
    tutorial_audio.snapshot(catherine::tutorial::topic(source[2]),input);
    // The native builder immediately copies this immutable snapshot into its
    // message allocation. Later input/config changes never rewrite that copy.
    static thread_local std::array<std::u16string,8> buffers;static thread_local unsigned next=0;
    auto& buffer=buffers[next++%buffers.size()];buffer=std::move(text);
    auto decoded=catherine::tutorial::plain(reinterpret_cast<const uint16_t*>(buffer.data()));std::string printable;for(auto c:decoded)printable+=c==u'\n'?' ':c<128?char(c):'?';
    fprintf(stderr,"Tutorial input snapshot: source=%u voice=%u layout=%u text=%s\n",source[2],unsigned(buffer[2]),input.layout,printable.c_str());
    return reinterpret_cast<uint64_t>(buffer.data());
}

static bool finish_display_preview(bool keep) {
    if (keep && catherine::store_host_settings(g_preview.proposed)) return true;
    if (g_fullscreen != g_preview.was_fullscreen) toggle_fullscreen(g_window);
    g_placement = g_preview.windowed_placement;
    if (!g_fullscreen) SetWindowPlacement(g_window, &g_preview.placement);
    g_window_height = g_preview.old_window_height;
    update_render_resolution(g_window);
    return !keep;
}

enum class PreviewStatus { Pending, Ready, Failed };
enum class ConfigApply { Failed, Saved, Preview };

static PreviewStatus read_display_preview() {
    if (catherine_resolution_completed_generation() < g_render_generation)
        return PreviewStatus::Pending;
    return catherine_resolution_applied_height() == g_render_height ?
        PreviewStatus::Ready : PreviewStatus::Failed;
}

static ConfigApply apply_window_settings(const catherine::HostSettings& config) {
    using Result = ConfigApply;
    if (g_fullscreen == config.fullscreen && g_window_height == config.window_height)
        return catherine::store_host_settings(config) ? Result::Saved : Result::Failed;

    g_preview = {};
    g_preview.proposed = config;
    g_preview.windowed_placement = g_placement;
    g_preview.was_fullscreen = g_fullscreen;
    g_preview.old_window_height = g_window_height;
    if (!GetWindowPlacement(g_window, &g_preview.placement)) return Result::Failed;
    if (g_fullscreen != config.fullscreen) toggle_fullscreen(g_window);
    if (!g_fullscreen && !resize_window(g_window, config.window_height)) {
        finish_display_preview(false);
        return Result::Failed;
    }
    g_window_height = config.window_height;
    update_render_resolution(g_window, true);
    return Result::Preview;
}

static std::atomic<bool> g_hide_cursor{false};
struct QaInput {uint64_t buttons;int32_t lx,ly,rx,ry;uint32_t family,key;int32_t mouse_x,mouse_y;uint32_t serial,pad;uint64_t output_buttons;int32_t output_lx,output_ly,output_rx,output_ry;uint32_t output_layout,output_raw;int32_t pointer_x,pointer_y;uint32_t pointer_move,pointer_click,pointer_valid;uint64_t second_buttons;int32_t second_lx,second_ly,second_rx,second_ry;uint32_t second_family,second_connected;uint64_t output_second_buttons;int32_t output_second_lx,output_second_ly,output_second_rx,output_second_ry;uint32_t output_second_connected,output_first_connected;};
static volatile QaInput* qa_input(){
 static auto* p=[](){wchar_t name[80];swprintf_s(name,L"Local\\CatherineBenchPad-%lu",GetCurrentProcessId());HANDLE m=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(QaInput),name);return static_cast<volatile QaInput*>(MapViewOfFile(m,FILE_MAP_ALL_ACCESS,0,0,sizeof(QaInput)));}();return p;
}

static_assert(offsetof(QaInput,second_buttons)==104);
static_assert(sizeof(QaInput)==168);

static std::atomic<unsigned> g_input_layout{unsigned(InputLayout::KeyboardMouse)};
static std::atomic<unsigned> g_keyboard_glyph_player{0};
static std::atomic<unsigned> g_raw_key{0},g_wheel_key{0};
static std::atomic<uint64_t> g_wheel_until{0};
static std::atomic<int> g_mouse_dx{0},g_mouse_dy{0};
static std::atomic<unsigned> g_pointer_move_serial{0},g_pointer_click_serial{0};
static std::atomic<unsigned> g_pointer_right_click_serial{0};
static std::atomic<int> g_pointer_wheel_delta{0};
static int g_pointer_last_x=INT_MIN,g_pointer_last_y=INT_MIN;
InputLayout active_input_layout() noexcept{return InputLayout(g_input_layout.load(std::memory_order_relaxed));}
InputLayout prompt_input_layout() noexcept{
    return InputLayout(catherine::resolve_button_prompt_layout(unsigned(active_input_layout()),catherine::button_prompt_style()));
}
unsigned keyboard_binding(unsigned i) noexcept{return catherine::keyboard_binding(i,g_keyboard_glyph_player.load(std::memory_order_acquire));}
uint64_t keyboard_binding_version() noexcept{
    const auto player=g_keyboard_glyph_player.load(std::memory_order_acquire);
    return (catherine::keyboard_binding_version(player)<<1)|player;
}
static std::atomic<unsigned> g_binding_player{0};
static std::atomic<uint64_t> g_keyboard_press_serial{0};
static std::atomic<uint64_t> g_camera_keyboard_serial{0};
static std::atomic<unsigned> g_p1_camera_layout{unsigned(InputLayout::KeyboardMouse)};
static P1InputOwner g_p1_input_owner;
static std::atomic<bool> g_input_menu_mode{true};
static std::atomic<unsigned> g_raw_player_pad[2]{};
static std::atomic<unsigned> g_connected_players{1};
unsigned connected_player_count() noexcept{return g_connected_players.load(std::memory_order_acquire);}
extern "C" int catherine_multiplayer_available(){return connected_player_count()>=2;}
struct ControllerSnapshot {std::string identity,name;PadState state{};InputLayout layout=InputLayout::Xbox;};
static std::mutex g_controllers_mutex;
static std::vector<ControllerSnapshot> g_controllers;
static std::vector<ControllerSnapshot> controller_snapshots(){std::lock_guard lock(g_controllers_mutex);return g_controllers;}
static int player_device_setting(unsigned player,int change){
    auto config=catherine::host_settings();auto devices=controller_snapshots();
    if(change){
        const int index=change-1;
        if(index<0||index>=int(devices.size())+2)return 0;
        const std::string target=index==0?(player?"none":"auto"):index==1?"keyboard":devices[index-2].identity;
        std::vector<std::string> ids;for(const auto&device:devices)ids.push_back(device.identity);
        catherine::assign_input_device(config.input_devices,player,target,ids);
        return catherine::store_host_settings(config);
    }
    const auto&id=config.input_devices[player];
    if(id=="keyboard")return 1;
    for(unsigned i=0;i<devices.size();++i)if(id==devices[i].identity)return int(i+2);
    return 0;
}
extern "C" const char* catherine_input_device_label(unsigned player,unsigned index){
    if(index==0)return player?"None":"Auto";
    if(index==1)return "Keyboard & Mouse";
    thread_local std::string label;
    const auto devices=controller_snapshots();
    if(index-2>=devices.size())return "Disconnected";
    label=devices[index-2].name;return label.c_str();
}
static int config_setting(unsigned field, int change) {
    if (field == PC_CONFIG_BEGIN) {
        catherine::begin_config_settings();
        return 1;
    }
    if (field == PC_CONFIG_SAVE) return catherine::save_config_settings();
    if (field == PC_CONFIG_END) {
        if (!catherine::end_config_settings()) return 1;
        const auto original = catherine::host_settings();
        if (g_fullscreen != original.fullscreen) toggle_fullscreen(g_window);
        if (!g_fullscreen && g_window_height != original.window_height)
            resize_window(g_window, original.window_height);
        g_window_height = original.window_height;
        update_render_resolution(g_window);
        return 1;
    }
    auto config = catherine::host_settings();
    if (field == PC_DISCARD_DRAFT) {
        g_display_draft.store(0, std::memory_order_release);
        return 1;
    }
    if (field == PC_PREVIEW) {
        if (!g_preview_deadline) return 0;
        const auto status = read_display_preview();
        if (GetTickCount64() < g_preview_deadline && status != PreviewStatus::Failed)
            return status == PreviewStatus::Ready ? 2 : 1;
        finish_display_preview(false);
        g_preview_deadline = 0;
        g_pc_preview.store(false, std::memory_order_release);
        return 0;
    }
    if (field == PC_KEEP || field == PC_REVERT) {
        if (!g_preview_deadline) return 0;
        const bool keep = field == PC_KEEP && read_display_preview() == PreviewStatus::Ready;
        const bool result = finish_display_preview(keep);
        g_preview_deadline = 0;
        g_pc_preview.store(false, std::memory_order_release);
        return result;
    }
    if(field==PC_BINDINGS_BEGIN){catherine::begin_bindings_settings(g_binding_player.load());return 1;}
    if(field==PC_BINDINGS_DIRTY)return catherine::bindings_settings_dirty();
    if(field==PC_BINDINGS_ACTIVE)return catherine::bindings_settings_active();
    if(field==PC_BINDINGS_SAVE)return catherine::save_bindings_settings();
    if(field==PC_BINDINGS_DISCARD){catherine::discard_bindings_settings();return 1;}
    if((field>=PC_KEY_BINDING&&field<=PC_RESET_KEYBOARD)||
       (field>=PC_BINDING&&field<=PC_RESET_BINDINGS)||
       field==PC_MOVEMENT_PRESET||field==PC_SWAP_STICKS) {
        config=catherine::bindings_settings();
        auto& profile=catherine::input_profile(config,g_binding_player.load());
        if(field>=PC_KEY_BINDING&&field<PC_RESET_KEYBOARD){
            const unsigned index=field-PC_KEY_BINDING;
            if(!change)return int(profile.keyboard[index]);
            if(!catherine::rebind_keyboard(profile.keyboard,index,unsigned(change),profile.movement_preset))return 0;
        } else if(field==PC_RESET_KEYBOARD) {
            profile.keyboard=catherine::default_keyboard(profile.movement_preset);
        } else if(field>=PC_BINDING&&field<PC_RESET_BINDINGS) {
            const unsigned logical=field-PC_BINDING,shift=logical*4;
            const auto current=(profile.bindings>>shift)&15;
            if(!change)return int(current);
            // D-pad and analog directions are fixed; physical button targets are 0..11.
            if(logical>=12||change<1||change>12)return 0;
            const uint64_t target=unsigned(change-1);
            for(unsigned i=0;i<48;i+=4)if(((profile.bindings>>i)&15)==target){
                profile.bindings^=(current^target)<<i;
                profile.bindings^=(current^target)<<shift;
                break;
            }
        } else if(field==PC_RESET_BINDINGS) {
            profile.bindings=catherine::kDefaultBindings;
            profile.swap_sticks=false;
        } else if(field==PC_MOVEMENT_PRESET) {
            if(!change)return profile.movement_preset;
            const int next=(profile.movement_preset+3+(change>0?1:-1))%3;
            if(!catherine::change_movement_preset(profile.keyboard,profile.movement_preset,next))return 0;
            profile.movement_preset=next;
        } else if(field==PC_SWAP_STICKS) {
            if(!change)return profile.swap_sticks;
            profile.swap_sticks=!profile.swap_sticks;
        }
        return catherine::store_bindings_draft(config);
    }

    config.fullscreen = g_fullscreen;
    config.window_height = g_window_height;
    const unsigned draft = g_display_draft.load(std::memory_order_acquire);
    if (draft && (field == PC_MODE || field == PC_RESOLUTION || field == PC_APPLY)) {
        config.fullscreen = (draft & 1) != 0;
        config.window_height = int(draft >> 2);
    }
    const auto cycle = [change](const auto& options, int current, const auto& allowed) {
        const int size = int(std::size(options));
        int index = 0;
        while (index + 1 < size && options[index] != current) ++index;
        if (!change) return index;
        for (int count = 0; count < size; ++count) {
            index = (index + size + change) % size;
            if (allowed(options[index])) return index;
        }
        return index;
    };
    constexpr int fps[] = {-1, 30, 60, 120, 240, 0};
    constexpr int heights[] = {0, 720, 1080, 1440, 2160};
    constexpr int aa[] = {0, 1, 2, 4, 8};
    constexpr int movies[] = {0, 60, 120, 240, -1};
    const auto any = [](int) { return true; };
    int index = 0;
    switch (field) {
    case PC_APPLY:
        if (!draft) return 1;
        g_display_draft.store(0, std::memory_order_release);
        break;
    case PC_FPS: index = cycle(fps, config.fps, any); config.fps = fps[index]; break;
    case PC_MODE: index = config.fullscreen; if (change) config.fullscreen = !config.fullscreen; break;
    case PC_RESOLUTION:
        if (config.fullscreen) return change ? 0 : 5;
        index = cycle(heights, config.window_height, [](int height) { return height <= max_window_height(g_window); });
        config.window_height = heights[index];
        break;
    case PC_AA:
        index = cycle(aa, config.anti_aliasing, [](int samples) { return samples < 2 || (nvn::msaa_sample_mask() & samples); });
        config.anti_aliasing = aa[index];
        break;
    case PC_MOVIES: index = cycle(movies, config.movie_interpolation, any); config.movie_interpolation = movies[index]; break;
    default: return 0;
    }
    if (!change) return index;
    if (field == PC_MODE || field == PC_RESOLUTION) {
        const unsigned next = config.fullscreen == g_fullscreen && config.window_height == g_window_height
            ? 0 : 2 | unsigned(config.fullscreen) | (unsigned(config.window_height) << 2);
        g_display_draft.store(next, std::memory_order_release);
        return 1;
    }
    const auto result = apply_window_settings(config);
    if (result == ConfigApply::Preview) {
        g_preview_deadline = GetTickCount64() + 15000;
        g_pc_preview.store(true, std::memory_order_release);
    }
    return result == ConfigApply::Preview || result == ConfigApply::Saved;
}

extern "C" const char* catherine_keyboard_label(unsigned i){
    const auto config=catherine::bindings_settings();
    const auto&profile=catherine::input_profile(config,g_binding_player.load());
    return catherine::keyboard_key_name(i<catherine::kKeyboardBindings?profile.keyboard[i]:0);
}
static int pointer_field(unsigned field) {
    // A cumulative counter lets each active menu consume every wheel detent once.
    if(field==5)return g_pointer_wheel_delta.load(std::memory_order_relaxed);
    if(field==6)return int(g_pointer_right_click_serial.load(std::memory_order_relaxed));
    if(!perf_background_window()) {

    if(field==2)return int(g_pointer_move_serial.load(std::memory_order_relaxed));
    if(field==3)return int(g_pointer_click_serial.load(std::memory_order_relaxed));
    POINT point{};RECT client{};
    ContentPointer pointer;
    if(g_window&&!IsIconic(g_window)&&GetCursorPos(&point)&&ScreenToClient(g_window,&point)&&GetClientRect(g_window,&client))
        pointer=content_pointer(point.x,point.y,client.right-client.left,client.bottom-client.top);
    if(field==0)return pointer.x;
    if(field==1)return pointer.y;
    return field==4&&pointer.inside;

    }
    const auto* pointer=qa_input();
    switch(field){case 0:return pointer->pointer_x;case 1:return pointer->pointer_y;
    case 2:return pointer->pointer_move;case 3:return pointer->pointer_click;case 4:return pointer->pointer_valid;}
    return 0;
}

extern "C" int catherine_pc_mouse(unsigned field) {
    return catherine_dev_gui_input_blocked() ? 0 : pointer_field(field);
}
extern "C" int catherine_dev_overlay_mouse(unsigned field) {
    if (field==5) return pointer_field(field)/WHEEL_DELTA;
    return pointer_field(field);
}

extern "C" int catherine_config_setting(unsigned field, int change) {
    if(field==PC_BINDING_PLAYER){if(change==1||change==2)g_binding_player.store(unsigned(change-1));return int(g_binding_player.load());}
    if(field==PC_INPUT_MENU_MODE){if(change){g_input_menu_mode.store(change==2);if(change==2)g_keyboard_glyph_player.store(0);}return g_input_menu_mode.load();}
    if(!perf_background_window()){if(field==PC_MENU_KEYBOARD_ALLOWED)return 1;}
    if(field==PC_MENU_KEYBOARD_ALLOWED)return 1;
    if(field==PC_CONNECTED_PLAYERS)return int(connected_player_count());
    if(field==PC_INPUT_DEVICE_COUNT)return int(controller_snapshots().size());
    if(field==PC_PLAYER1_DEVICE||field==PC_PLAYER2_DEVICE)return player_device_setting(field-PC_PLAYER1_DEVICE,change);
    if (field == PC_APPLY_BUTTON) return !catherine_dev_gui_input_blocked() && g_apply_button.load(std::memory_order_relaxed);
    if (field == PC_RAW_PAD) return catherine_dev_gui_input_blocked()?0:g_raw_player_pad[g_binding_player.load()].load(std::memory_order_relaxed);
    if (field == PC_DUALSENSE) return prompt_input_layout()==InputLayout::PlayStation||prompt_input_layout()==InputLayout::PS4;
    if (field == PC_BUTTON_PROMPTS) {auto config=catherine::host_settings();if(change){config.button_prompts=change>=100?std::clamp(change-100,0,4):(config.button_prompts+(change<0?4:1))%5;return catherine::store_host_settings(config);}return config.button_prompts;}
    if (field == PC_INPUT_LAYOUT) return int(active_input_layout());
    if (field == PC_CAMERA_INPUT_LAYOUT) return int(g_p1_camera_layout.load(std::memory_order_acquire));
    if (field == PC_CURSOR_HIDDEN) {
        if(g_hide_cursor.exchange(change!=0)!=bool(change) && g_window)
            PostMessageW(g_window,kCursorPolicy,0,0);
        return change!=0;
    }
    if (field == PC_RAW_KEY){if(catherine_dev_gui_input_blocked())return 0;const unsigned k=g_raw_key.load();return k>=256&&GetTickCount64()>=g_wheel_until.load()?0:int(k);}
    if (!change && field>=PC_KEY_BINDING && field<PC_RESET_KEYBOARD)return int(catherine::input_profile(catherine::bindings_settings(),g_binding_player.load()).keyboard[field-PC_KEY_BINDING]);
    if(!change&&field==PC_MOVEMENT_PRESET)return catherine::input_profile(catherine::bindings_settings(),g_binding_player.load()).movement_preset;
    if(!change&&field==PC_SWAP_STICKS)return catherine::input_profile(catherine::bindings_settings(),g_binding_player.load()).swap_sticks;
    if(field==PC_BINDINGS_DIRTY)return catherine::bindings_settings_dirty();
    if(field==PC_BINDINGS_ACTIVE)return catherine::bindings_settings_active();
    if (!g_window) return 0;
    if (!change && field >= PC_BINDING && field < PC_RESET_BINDINGS)
        return int((catherine::input_profile(catherine::bindings_settings(),g_binding_player.load()).bindings >> ((field - PC_BINDING) * 4)) & 15);
    if (!change && (field <= PC_MOVIES || field == PC_PREVIEW || field == PC_APPLY)) {
        const auto config = catherine::host_settings();
        if (field == PC_PREVIEW && !g_pc_preview.load(std::memory_order_acquire)) return 0;
        if (field == PC_FPS) return config.fps == -1 ? 0 : config.fps == 0 ? 5 : config.fps == 30 ? 1 : config.fps == 60 ? 2 : config.fps == 120 ? 3 : 4;
        if (field == PC_AA) return config.anti_aliasing < 2 ? config.anti_aliasing : config.anti_aliasing == 2 ? 2 : config.anti_aliasing == 4 ? 3 : 4;
        if (field == PC_MOVIES) return config.movie_interpolation == -1 ? 4 : config.movie_interpolation == 0 ? 0 : config.movie_interpolation == 60 ? 1 : config.movie_interpolation == 120 ? 2 : 3;
        const unsigned draft = g_display_draft.load(std::memory_order_acquire);
        if (field == PC_APPLY) return draft != 0;
        if (draft && field == PC_MODE) return draft & 1;
        if (draft && field == PC_RESOLUTION) {
            const unsigned height = draft >> 2;
            return draft & 1 ? 5 : height == 0 ? 0 : height == 720 ? 1 : height == 1080 ? 2 : height == 1440 ? 3 : 4;
        }
        if (field == PC_MODE && !g_pc_preview.load(std::memory_order_acquire)) return config.fullscreen;
        if (field == PC_RESOLUTION && !g_pc_preview.load(std::memory_order_acquire))
            return config.fullscreen ? 5 : config.window_height == 0 ? 0 : config.window_height == 720 ? 1 : config.window_height == 1080 ? 2 : config.window_height == 1440 ? 3 : 4;
    }
    return int(SendMessageW(g_window, kConfigSetting, field, change));
}

extern "C" void catherine_pc_exit_game(void) {
    if (g_window) PostMessageW(g_window, kExitGame, 0, 0);
}

static void step_window_fps(int direction) {
    if (g_preview_deadline) return;
    const int effective = catherine_requested_refresh_rate();
    const int next = catherine::step_fps_limit(effective, direction);
    if (next == effective) return;
    auto config = catherine::host_settings();
    config.fps = next;
    if (!catherine::store_host_settings(config, true)) MessageBeep(MB_ICONWARNING);
}

static void toggle_saved_fullscreen(HWND window) {
    if (g_preview_deadline) return;
    toggle_fullscreen(window);
    update_render_resolution(window);
    auto config = catherine::host_settings();
    config.fullscreen = g_fullscreen;
    if (!catherine::store_host_settings(config)) log_line("Could not write Catherine's fullscreen setting");
}
#endif

#ifdef CATHERINE_HIGH_REFRESH_BUILD
static void keyboard_activity(unsigned keycode=0){
    if(GetForegroundWindow()!=g_window)return;
    g_input_layout.store(unsigned(InputLayout::KeyboardMouse),std::memory_order_relaxed);
    if(keycode){g_raw_key.store(keycode,std::memory_order_relaxed);g_keyboard_press_serial.fetch_add(1,std::memory_order_relaxed);}
    if(keycode&&keycode<256)g_camera_keyboard_serial.fetch_add(1,std::memory_order_release);
}
static bool mouse_input(HRAWINPUT input){
    RAWINPUT data{};UINT size=sizeof(data);
    if(GetRawInputData(input,RID_INPUT,&data,&size,sizeof(RAWINPUTHEADER))==UINT(-1)||data.header.dwType!=RIM_TYPEMOUSE)return false;
    if(catherine_dev_gui_input_blocked()||GetForegroundWindow()!=g_window)return true;
    if(!(data.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){
        const int x=data.data.mouse.lLastX,y=data.data.mouse.lLastY;
        g_mouse_dx.fetch_add(x,std::memory_order_relaxed);g_mouse_dy.fetch_add(y,std::memory_order_relaxed);
        // Motion alone may be the user moving between monitors; clicks/keys choose the layout.
    }
    return true;
}
#endif

#ifdef CATHERINE_HIGH_REFRESH_BUILD
// Window-thread state: native caption/resize gestures must never drag against
// a client-area cursor clip, including activation before WM_SETFOCUS.
static bool g_native_size_move = false;
static bool g_nonclient_mouse = false;

static bool cursor_in_client(HWND window, RECT& screen_rect) {
    POINT cursor{};
    if (!GetClientRect(window, &screen_rect) || !GetCursorPos(&cursor)) return false;
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&screen_rect), 2);
    return PtInRect(&screen_rect, cursor) != FALSE;
}
#endif

static void update_mouse_cursor(HWND h) {
    if(perf_background_window())return; // QA background windows never own the desktop cursor.
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    if(g_native_size_move||g_nonclient_mouse){ClipCursor(nullptr);return;}
    RECT rect{};
    const bool hide=!catherine_dev_gui_input_blocked()&&
        g_hide_cursor.load(std::memory_order_relaxed)&&GetForegroundWindow()==h&&cursor_in_client(h,rect);
    SetCursor(hide?nullptr:LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    if(hide)ClipCursor(&rect);
    else ClipCursor(nullptr);
#endif
}
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    LRESULT dev_result=0;
    if(catherine_dev_gui_window_message(h,msg,w,l,&dev_result))return dev_result;
#endif
    switch (msg) {
#ifdef CATHERINE_HIGH_REFRESH_BUILD
        case kCursorPolicy: update_mouse_cursor(h);return 0;
        case WM_MOUSEACTIVATE:
            if(LOWORD(l)!=HTCLIENT){g_nonclient_mouse=true;update_mouse_cursor(h);}
            else if(!g_native_size_move)g_nonclient_mouse=false;
            break;  // Default activation may synchronously send WM_SETFOCUS.
        case WM_NCLBUTTONDOWN:case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONDOWN:case WM_NCMBUTTONDOWN:case WM_NCXBUTTONDOWN:
            g_nonclient_mouse=true;update_mouse_cursor(h);break;
        case WM_SYSCOMMAND:
            if((w&0xfff0)==SC_MOVE||(w&0xfff0)==SC_SIZE){g_nonclient_mouse=true;update_mouse_cursor(h);}
            break;
        case WM_ENTERSIZEMOVE:
            g_native_size_move=true;g_nonclient_mouse=true;update_mouse_cursor(h);return 0;
        case WM_EXITSIZEMOVE:
            g_native_size_move=false;g_nonclient_mouse=false;update_mouse_cursor(h);return 0;
        case WM_SETCURSOR:
            if(LOWORD(l)!=HTCLIENT){g_nonclient_mouse=true;update_mouse_cursor(h);break;}
            if(!g_native_size_move)g_nonclient_mouse=false;
            if(g_hide_cursor.load(std::memory_order_relaxed)) {update_mouse_cursor(h);return TRUE;}
            break;
        case WM_SETFOCUS: update_mouse_cursor(h);break;
#endif
        case WM_CLOSE:
            log("window closed");
            ExitProcess(0);
        case WM_INPUT:
#ifdef CATHERINE_HIGH_REFRESH_BUILD
            if(!mouse_input(reinterpret_cast<HRAWINPUT>(l)))
#endif
            g_dualsense.input(reinterpret_cast<HRAWINPUT>(l));
            break;
        case WM_INPUT_DEVICE_CHANGE:
            if (w == GIDC_REMOVAL) g_dualsense.remove(reinterpret_cast<HANDLE>(l));
            g_dualsense.refresh_connection(w == GIDC_REMOVAL ? reinterpret_cast<HANDLE>(l) : nullptr);
            return 0;
#ifdef CATHERINE_HIGH_REFRESH_BUILD
        case WM_MOUSEWHEEL: {
            g_pointer_wheel_delta.fetch_add(GET_WHEEL_DELTA_WPARAM(w),std::memory_order_relaxed);
            const unsigned key=GET_WHEEL_DELTA_WPARAM(w)>0?catherine::kWheelUp:catherine::kWheelDown;
            keyboard_activity(key);g_wheel_key.store(key);g_wheel_until.store(GetTickCount64()+80);return 0;
        }
        case WM_MOUSEMOVE: {
            const int x=short(LOWORD(l)),y=short(HIWORD(l));
            if(x!=g_pointer_last_x||y!=g_pointer_last_y){
                g_pointer_last_x=x;g_pointer_last_y=y;
                g_pointer_move_serial.fetch_add(1,std::memory_order_relaxed);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            g_pointer_click_serial.fetch_add(1,std::memory_order_relaxed);
            keyboard_activity(VK_LBUTTON);return 0;
        case WM_RBUTTONDOWN:
            g_pointer_right_click_serial.fetch_add(1,std::memory_order_relaxed);
            keyboard_activity(VK_RBUTTON);return 0;
        case WM_MBUTTONDOWN: keyboard_activity(VK_MBUTTON);return 0;
        case WM_XBUTTONDOWN: keyboard_activity(GET_XBUTTON_WPARAM(w)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2);return TRUE;
        case WM_KEYUP:case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:case WM_XBUTTONUP:
            g_raw_key.store(0);break;
        case WM_KILLFOCUS:
            if(!perf_background_window()){ClipCursor(nullptr);SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));}
            g_raw_key.store(0);g_wheel_until.store(0);g_mouse_dx.store(0);g_mouse_dy.store(0);break;
#endif
        case WM_TIMER:
            if (w == kFpsTimer) {
                update_window_fps(h);
                return 0;
            }
            break;
#ifdef CATHERINE_HIGH_REFRESH_BUILD
        case kConfigSetting:
            if(unsigned(w)>=PC_BINDING_PLAYER&&unsigned(w)<=PC_CONNECTED_PLAYERS)return catherine_config_setting(unsigned(w),int(l));
            if(unsigned(w)==120)return keyboard_binding(unsigned(l));
            if(unsigned(w)==121)return LRESULT(keyboard_binding_version());
            return config_setting(unsigned(w),int(l));
        case kExitGame:
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        case WM_WINDOWPOSCHANGED:
            refresh_window_monitor(h);
            update_mouse_cursor(h);
            break;
        case WM_DISPLAYCHANGE:
            refresh_window_monitor(h, true);
            break;
        case WM_SIZE:
#endif
        case WM_KEYDOWN: {
            const bool first_press = !(l & (1ll << 30));
#ifdef CATHERINE_HIGH_REFRESH_BUILD
            if(first_press)keyboard_activity(unsigned(w));
#endif
#ifdef CATHERINE_HIGH_REFRESH_BUILD
            if (w == VK_F9 || w == VK_F10) {
                if (first_press) step_window_fps(w == VK_F10 ? 1 : -1);
                return 0;
            }
            if (w == VK_ESCAPE) return 0;
            if (w == VK_F11 && first_press) toggle_saved_fullscreen(h);
#else
            if (w == VK_F11 && first_press) toggle_fullscreen(h);
#endif
            break;
        }
        case WM_SYSKEYDOWN:
            if (w == VK_RETURN && (l & (1ll << 29))) {  // Alt+Enter
                if (!(l & (1ll << 30))) {
#ifdef CATHERINE_HIGH_REFRESH_BUILD
                    g_block_return.store(true, std::memory_order_release);
                    toggle_saved_fullscreen(h);
#else
                    toggle_fullscreen(h);
#endif
                }
                return 0;
            }
            break;
        case WM_SYSCHAR:
            if (w == VK_RETURN) return 0;  // no beep
            break;
    }
    return DefWindowProcW(h, msg, w, l);
}

// The window lives on its own thread so its message loop keeps running whatever the game threads do.
static void window_thread(std::atomic<bool>* ready) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    const std::wstring cls = widen(nx::game::kInfo.title) + L"Recomp";  // tools/capture.ps1 finds the window by it
    wc.lpszClassName = cls.c_str();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    RegisterClassW(&wc);
    // The render resolution's aspect (NX_RESOLUTION, default 16:9), about 80% of the screen height, centred.
    unsigned rw = 16, rh = 9;
    if (const char* r = std::getenv("NX_RESOLUTION")) std::sscanf(r, "%ux%u", &rw, &rh);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ch = std::max(540, sh * 8 / 10), cw = int(int64_t(ch) * rw / std::max(1u, rh));
    RECT r{0, 0, cw, ch};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (sh - h) / 2;
    // NX_HEADLESS: created hidden (the surface still needs a window), never brought to the front.
    const DWORD style = (headless() || perf_background_window()) ? (kWindowed & ~WS_VISIBLE) : kWindowed;
    g_window = CreateWindowExW(perf_background_window()?WS_EX_NOACTIVATE:0,wc.lpszClassName, widen(nx::game::kInfo.title).c_str(), style,
                             std::max(0, x), std::max(0, y), w, h, nullptr, nullptr, wc.hInstance, nullptr);
    if (g_window) {
        SendMessageW(g_window, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
        HICON small_icon = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        SendMessageW(g_window, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
    if (g_window && !g_dualsense.register_window(g_window)) log_line("DualSense Raw Input registration failed");
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    if(g_window){RAWINPUTDEVICE mouse{1,2,0,g_window};if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse)))log_line("Mouse Raw Input registration failed");}
#endif
    if (g_window) SetTimer(g_window, kFpsTimer, 1000, nullptr);
    const char* fullscreen = std::getenv("NX_FULLSCREEN");
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    const auto config = catherine::host_settings();
    if (g_window) {
        if (config.window_height && resize_window(g_window, config.window_height))
            g_window_height = config.window_height;
        if (fullscreen ? *fullscreen == '1' : config.fullscreen) toggle_fullscreen(g_window);
        g_display_ready = true;
        refresh_window_monitor(g_window, true);
    }
#else
    if (!headless() && fullscreen && *fullscreen == '1' && g_window) toggle_fullscreen(g_window);
#endif
    if (!headless()) {
        if(perf_background_window()) {
            ShowWindow(g_window,SW_SHOWNOACTIVATE);
            SetWindowPos(g_window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        } else SetForegroundWindow(g_window);
    }
    *ready = true;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
#ifdef CATHERINE_HIGH_REFRESH_BUILD
        if(catherine_dev_gui_pretranslate(&m))continue;
#endif
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void* create_window() {
    if (!g_window) {
        start_periodic_render_log();
        nx::nvn::start_frame_dump_request_worker();
        static std::atomic<bool> ready{false};
        std::thread(window_thread, &ready).detach();
        while (!ready) Sleep(1);
    }
    return g_window;
}

bool window_minimized() { return g_window && IsIconic(g_window); }

void window_size(uint32_t& w, uint32_t& h) {
    RECT r{};
    if (g_window) GetClientRect(g_window, &r);
    w = uint32_t(r.right - r.left);
    h = uint32_t(r.bottom - r.top);
}

void screen_size(uint32_t& w, uint32_t& h) { window_size(w, h); }

// ---- input: DualSense or XInput pad 0, plus keyboard -----------------------------------------------------------

static bool key(int vk){return qa_input()->key==unsigned(vk)||(!perf_background_window()&&g_window&&GetForegroundWindow()==g_window&&(GetAsyncKeyState(vk)&0x8000));}

bool headless() {  // NX_HEADLESS (platform.h)
    static const bool on = std::getenv("NX_HEADLESS") != nullptr;
    return on;
}

static bool read_xinput(unsigned slot,PadState& p) {
    XINPUT_STATE xs{};
    if(XInputGetState(slot,&xs)!=ERROR_SUCCESS)return false;
        const auto& g = xs.Gamepad;
        if (g.wButtons & XINPUT_GAMEPAD_B) p.buttons |= kA;  // Nintendo layout
        if (g.wButtons & XINPUT_GAMEPAD_A) p.buttons |= kB;
        if (g.wButtons & XINPUT_GAMEPAD_Y) p.buttons |= kX;
        if (g.wButtons & XINPUT_GAMEPAD_X) p.buttons |= kY;
        if (g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) p.buttons |= kL;
        if (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) p.buttons |= kR;
        if (g.bLeftTrigger > 100) p.buttons |= kZL;
        if (g.bRightTrigger > 100) p.buttons |= kZR;
        if (g.wButtons & XINPUT_GAMEPAD_START) p.buttons |= kPlus;
        if (g.wButtons & XINPUT_GAMEPAD_BACK) p.buttons |= kMinus;
        if (g.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) p.buttons |= kStickL;
        if (g.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) p.buttons |= kStickR;
        if (g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) p.buttons |= kLeft;
        if (g.wButtons & XINPUT_GAMEPAD_DPAD_UP) p.buttons |= kUp;
        if (g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) p.buttons |= kRight;
        if (g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) p.buttons |= kDown;
        p.lx = pad_stick_dead_zone(g.sThumbLX);
        p.ly = pad_stick_dead_zone(g.sThumbLY);
        p.rx = pad_stick_dead_zone(g.sThumbRX);
        p.ry = pad_stick_dead_zone(g.sThumbRY);
    return true;
}
#ifdef CATHERINE_HIGH_REFRESH_BUILD
static void publish_qa_pad_status(const PadState (&players)[2],const bool (&connected)[2]) {
    auto*status=qa_input();status->pad=g_hide_cursor.load();status->output_buttons=players[0].buttons;
    status->output_lx=players[0].lx;status->output_ly=players[0].ly;status->output_rx=players[0].rx;status->output_ry=players[0].ry;
    status->output_layout=unsigned(active_input_layout());status->output_raw=g_raw_key.load();
    status->output_second_buttons=players[1].buttons;status->output_second_lx=players[1].lx;status->output_second_ly=players[1].ly;
    status->output_second_rx=players[1].rx;status->output_second_ry=players[1].ry;
    status->output_second_connected=connected[1];status->output_first_connected=connected[0];
}
static void sample_overlay_controller(const PadState &pad) {
    const uint64_t buttons=pad.buttons;
    unsigned held=0;
    if ((buttons&kUp) || pad.ly>kStickMax/2) held|=1;
    if ((buttons&kDown) || pad.ly < -kStickMax/2) held|=2;
    if ((buttons&kLeft) || pad.lx < -kStickMax/2) held|=4;
    if ((buttons&kRight) || pad.lx>kStickMax/2) held|=8;
    if (buttons&kA) held|=16;
    if (buttons&kB) held|=32;
    catherine_dev_overlay_pad(held);
}
static void suppress_overlay_game_input(PadState (&players)[2]) {
    if (!catherine_dev_gui_input_blocked()) return;
    players[0]={};players[1]={};
    g_raw_pad.store(0);g_raw_player_pad[0].store(0);g_raw_player_pad[1].store(0);
    g_apply_button.store(false);g_raw_key.store(0);g_wheel_until.store(0);
    g_mouse_dx.store(0);g_mouse_dy.store(0);
}
static void read_player_pads_native(PadState (&players)[2],bool (&connected)[2]) {
    auto config=catherine::host_settings();
    std::vector<ControllerSnapshot> controllers;
    for(const auto&device:g_dualsense.snapshots())controllers.push_back({device.identity,device.name,device.state,InputLayout::PlayStation});
    const bool has_direct_dualsense=!controllers.empty();
    bool wgi_available=false;
    for(const auto&device:windows_controllers(wgi_available)){
        if(device.direct_dualsense&&has_direct_dualsense)continue;
        controllers.push_back({device.identity,device.name,device.state,device.layout});
    }
    for(unsigned slot=0;!wgi_available&&slot<XUSER_MAX_COUNT;++slot){
        PadState state{};if(!read_xinput(slot,state))continue;
        // The slot is XInput's persistent logical identifier. Audio endpoints, where
        // available, distinguish physical controllers across reassigned XInput slots.
        std::string identity="xinput:"+std::to_string(slot);
        using AudioIds=DWORD (WINAPI*)(DWORD,LPWSTR,UINT*,LPWSTR,UINT*);
        static const auto audio_ids=[](){HMODULE lib=LoadLibraryW(L"xinput1_4.dll");return lib?reinterpret_cast<AudioIds>(GetProcAddress(lib,"XInputGetAudioDeviceIds")):nullptr;}();
        if(audio_ids){wchar_t render[1024]{},capture[1024]{};UINT r=1024,c=1024;
            if(audio_ids(slot,render,&r,capture,&c)==ERROR_SUCCESS&&render[0]){
                const int n=WideCharToMultiByte(CP_UTF8,0,render,-1,nullptr,0,nullptr,nullptr);
                std::string endpoint(n,'\0');WideCharToMultiByte(CP_UTF8,0,render,-1,endpoint.data(),n,nullptr,nullptr);
                endpoint.pop_back();identity="xinput-audio:"+endpoint;
            }
        }
        controllers.push_back({identity,"Xbox Controller "+std::to_string(slot+1),state,InputLayout::Xbox});
    }
    std::sort(controllers.begin(),controllers.end(),[](const auto&a,const auto&b){return a.identity<b.identity;});
    // Number identical direct devices without replacing their persistent identity.
    std::map<std::string,unsigned> totals,ordinals;
    for(const auto&device:controllers)++totals[device.name];
    for(auto&device:controllers)if(totals[device.name]>1){const auto ordinal=++ordinals[device.name];device.name+=" "+std::to_string(ordinal);}
    {std::lock_guard lock(g_controllers_mutex);g_controllers=controllers;}
    std::vector<std::string> live_ids;for(const auto&device:controllers)live_ids.push_back(device.identity);
    config.input_devices=catherine::effective_input_devices(config.input_devices,live_ids);
    const bool menus=g_input_menu_mode.load(std::memory_order_relaxed);
    g_keyboard_glyph_player.store(catherine::keyboard_glyph_owner(menus,config.input_devices),std::memory_order_release);
    const bool focused=g_window&&GetForegroundWindow()==g_window;
    static auto last=std::chrono::steady_clock::now();const auto now=std::chrono::steady_clock::now();
    const double dt=std::chrono::duration<double>(now-last).count();last=now;
    static MouseLookFilter mouse;
    const int dx=g_mouse_dx.exchange(0),dy=g_mouse_dy.exchange(0);
    mouse.sample(focused?dx:0,focused?-dy:0,dt);
    const auto held=[&](unsigned vk){
        if(!focused)return false;
        if(vk>=256)return g_wheel_key.load()==vk&&GetTickCount64()<g_wheel_until.load();
        if(vk==VK_RETURN&&(key(VK_MENU)||g_block_return.load()))return false;
        return (GetAsyncKeyState(int(vk))&0x8000)!=0;
    };
    if(!key(VK_RETURN))g_block_return.store(false,std::memory_order_release);
    PadState keyboard[2]{};
    for(unsigned i=0;i<2;++i)keyboard_mouse_pad(keyboard[i],catherine::input_profile(config,i).keyboard,held,MouseLookFilter::axis(mouse.x),MouseLookFilter::axis(mouse.y));
    static std::map<std::string,uint64_t> previous;
    for(const auto&device:controllers){
        if(device.state.buttons&~previous[device.identity]){
            g_input_layout.store(unsigned(device.layout),std::memory_order_relaxed);
        }
        previous[device.identity]=device.state.buttons;
    }
    for(auto it=previous.begin();it!=previous.end();) {
        if(std::none_of(controllers.begin(),controllers.end(),[&](const auto&d){return d.identity==it->first;}))it=previous.erase(it);else ++it;
    }
    const auto p1_source=g_p1_input_owner.select(config.input_devices[0],controllers,g_camera_keyboard_serial.load(std::memory_order_acquire),0);
    static uint64_t mouse_source_generation=0;
    if(mouse_source_generation!=p1_source.generation){
        mouse.x=mouse.y=0;
        for(auto& state:keyboard)state.rx=state.ry=0;
        mouse_source_generation=p1_source.generation;
    }
    std::vector<PadState> raw_controllers;for(const auto&device:controllers)raw_controllers.push_back(device.state);
    sample_overlay_controller(catherine::menu_input_pad(PadState{},raw_controllers,config.bindings,config.swap_sticks));
    const unsigned raw_all=catherine::raw_capture_buttons(raw_controllers);
    g_raw_pad.store(raw_all,std::memory_order_relaxed);
    const auto mapped=[&](const ControllerSnapshot&device,unsigned player){
        PadState result=device.state;const auto&profile=catherine::input_profile(config,player);
        result.buttons=catherine::remap_input_buttons(result.buttons,profile.bindings);swap_controller_sticks(result,profile.swap_sticks);return result;
    };
    for(unsigned player=0;player<2;++player){
        const unsigned profile_player=catherine::input_binding_profile(menus,player);
        const auto&stored=config.input_devices[player];const auto&id=stored=="auto"?p1_source.identity:stored;
        if(id=="keyboard"){players[player]=keyboard[profile_player];connected[player]=true;}
        else for(const auto&device:controllers)if(device.identity==id){players[player]=mapped(device,profile_player);connected[player]=true;break;}
        if(player==0)connected[player]=true;
        g_raw_player_pad[player].store(raw_all,std::memory_order_relaxed);
    }
    if(menus){
        // Main menus accept every source through P1's applied profile, independent of ownership.
        players[0]=catherine::menu_input_pad(keyboard[0],raw_controllers,config.bindings,config.swap_sticks);
        // P2's native character-select ring also uses P1 controls in menus.
    }
    g_p1_camera_layout.store(unsigned(p1_source.layout),std::memory_order_release);
    g_connected_players.store(unsigned(connected[0])+unsigned(connected[1]),std::memory_order_release);
    g_apply_button.store((players[0].buttons&kX)!=0,std::memory_order_relaxed);
    publish_qa_pad_status(players,connected);
    input_script(players[0]);
    suppress_overlay_game_input(players);
}
void read_player_pads(PadState (&players)[2],bool (&connected)[2]) {
    // Interactive windows use the complete production input path. Synthetic
    // controls remain available only while an explicit owned QA input is held.
    const auto*request=qa_input();
    const bool synthetic=request->key||request->buttons||request->lx||request->ly||request->rx||request->ry||request->family||request->second_buttons||request->second_connected;
    if(!perf_background_window()&&!synthetic){read_player_pads_native(players,connected);return;}

    auto config=catherine::host_settings();
    const auto*bench=qa_input();
    // QA key 192 follows the ordinary owner-window tilde route, once per press.
    static unsigned previous_dev_key=0;
    if((bench->key==VK_OEM_3 || bench->key==VK_F8) && previous_dev_key!=bench->key && g_window)
        PostMessageW(g_window,WM_KEYDOWN,bench->key,1);
    previous_dev_key=bench->key;
    PadState fake{};fake.buttons=bench->buttons;fake.lx=bench->lx;fake.ly=bench->ly;fake.rx=bench->rx;fake.ry=bench->ry;
    const auto family=InputLayout(bench->family>=1&&bench->family<=4?bench->family:1);
    std::vector<ControllerSnapshot> controllers{{"qa:controller-1",family==InputLayout::PlayStation?"DualSense (QA)":"Xbox Controller (QA)",fake,family}};
    if(bench->second_connected){
        PadState second{};second.buttons=bench->second_buttons;second.lx=bench->second_lx;second.ly=bench->second_ly;second.rx=bench->second_rx;second.ry=bench->second_ry;
        const auto second_family=InputLayout(bench->second_family>=1&&bench->second_family<=4?bench->second_family:1);
        controllers.push_back({"qa:controller-2",second_family==InputLayout::PlayStation?"DualSense 2 (QA)":"Xbox Controller 2 (QA)",second,second_family});
    }
    std::sort(controllers.begin(),controllers.end(),[](const auto&a,const auto&b){return a.identity<b.identity;});
    // Number identical direct devices without replacing their persistent identity.
    std::map<std::string,unsigned> totals,ordinals;
    for(const auto&device:controllers)++totals[device.name];
    for(auto&device:controllers)if(totals[device.name]>1){const auto ordinal=++ordinals[device.name];device.name+=" "+std::to_string(ordinal);}
    {std::lock_guard lock(g_controllers_mutex);g_controllers=controllers;}
    std::vector<std::string> live_ids;for(const auto&device:controllers)live_ids.push_back(device.identity);
    config.input_devices=catherine::effective_input_devices(config.input_devices,live_ids);
    const bool menus=g_input_menu_mode.load(std::memory_order_relaxed);
    g_keyboard_glyph_player.store(catherine::keyboard_glyph_owner(menus,config.input_devices),std::memory_order_release);
    const bool focused=true;
    static unsigned previous_key=0,previous_serial=0,previous_click=0;
    g_raw_key.store(bench->key);
    if(bench->key==VK_RBUTTON && previous_key!=VK_RBUTTON)
        g_pointer_right_click_serial.fetch_add(1,std::memory_order_relaxed);
    if(bench->key>=256)g_wheel_until.store(GetTickCount64()+200);
    if((bench->key&&bench->key!=previous_key)||bench->pointer_click!=previous_click){g_input_layout.store(unsigned(InputLayout::KeyboardMouse));g_keyboard_press_serial.fetch_add(1);}
    if((bench->key&&bench->key<256&&bench->key!=previous_key)||bench->pointer_click!=previous_click)g_camera_keyboard_serial.fetch_add(1,std::memory_order_release);
    previous_key=bench->key;previous_click=bench->pointer_click;
    if(bench->serial!=previous_serial){g_mouse_dx.fetch_add(bench->mouse_x);g_mouse_dy.fetch_add(bench->mouse_y);previous_serial=bench->serial;}
    static auto last=std::chrono::steady_clock::now();const auto now=std::chrono::steady_clock::now();
    const double dt=std::chrono::duration<double>(now-last).count();last=now;
    static MouseLookFilter mouse;
    const int dx=g_mouse_dx.exchange(0),dy=g_mouse_dy.exchange(0);
    mouse.sample(focused?dx:0,focused?-dy:0,dt);
    const auto held=[&](unsigned vk){
        if(!focused)return false;
        if(vk>=256)return g_wheel_key.load()==vk&&GetTickCount64()<g_wheel_until.load();
        if(vk==VK_RETURN&&(key(VK_MENU)||g_block_return.load()))return false;
        return key(int(vk));
    };
    if(!key(VK_RETURN))g_block_return.store(false,std::memory_order_release);
    PadState keyboard[2]{};
    for(unsigned i=0;i<2;++i)keyboard_mouse_pad(keyboard[i],catherine::input_profile(config,i).keyboard,held,MouseLookFilter::axis(mouse.x),MouseLookFilter::axis(mouse.y));
    static std::map<std::string,uint64_t> previous;
    for(const auto&device:controllers){
        if(device.state.buttons&~previous[device.identity]){
            g_input_layout.store(unsigned(device.layout),std::memory_order_relaxed);
        }
        previous[device.identity]=device.state.buttons;
    }
    for(auto it=previous.begin();it!=previous.end();) {
        if(std::none_of(controllers.begin(),controllers.end(),[&](const auto&d){return d.identity==it->first;}))it=previous.erase(it);else ++it;
    }
    const auto p1_source=g_p1_input_owner.select(config.input_devices[0],controllers,g_camera_keyboard_serial.load(std::memory_order_acquire),1);
    static uint64_t mouse_source_generation=0;
    if(mouse_source_generation!=p1_source.generation){
        mouse.x=mouse.y=0;
        for(auto& state:keyboard)state.rx=state.ry=0;
        mouse_source_generation=p1_source.generation;
    }
    std::vector<PadState> raw_controllers;for(const auto&device:controllers)raw_controllers.push_back(device.state);
    sample_overlay_controller(catherine::menu_input_pad(PadState{},raw_controllers,config.bindings,config.swap_sticks));
    const unsigned raw_all=catherine::raw_capture_buttons(raw_controllers);
    g_raw_pad.store(raw_all,std::memory_order_relaxed);
    const auto mapped=[&](const ControllerSnapshot&device,unsigned player){
        PadState result=device.state;const auto&profile=catherine::input_profile(config,player);
        result.buttons=catherine::remap_input_buttons(result.buttons,profile.bindings);swap_controller_sticks(result,profile.swap_sticks);return result;
    };
    for(unsigned player=0;player<2;++player){
        const unsigned profile_player=catherine::input_binding_profile(menus,player);
        const auto&stored=config.input_devices[player];const auto&id=stored=="auto"?p1_source.identity:stored;
        if(id=="keyboard"){players[player]=keyboard[profile_player];connected[player]=true;}
        else for(const auto&device:controllers)if(device.identity==id){players[player]=mapped(device,profile_player);connected[player]=true;break;}
        if(player==0)connected[player]=true;
        g_raw_player_pad[player].store(raw_all,std::memory_order_relaxed);
    }
    if(menus){
        // Main menus accept every source through P1's applied profile, independent of ownership.
        players[0]=catherine::menu_input_pad(keyboard[0],raw_controllers,config.bindings,config.swap_sticks);
        // P2's native character-select ring also uses P1 controls in menus.
    }
    g_p1_camera_layout.store(unsigned(p1_source.layout),std::memory_order_release);
    g_connected_players.store(unsigned(connected[0])+unsigned(connected[1]),std::memory_order_release);
    g_apply_button.store((players[0].buttons&kX)!=0,std::memory_order_relaxed);
    publish_qa_pad_status(players,connected);
    input_script(players[0]);
    suppress_overlay_game_input(players);
}
#else
unsigned connected_player_count() noexcept{return 1;}
void read_player_pads(PadState (&players)[2],bool (&connected)[2]) {
    read_xinput(0,players[0]);connected[0]=true;connected[1]=false;
    if(key('A')||key(VK_LEFT))players[0].lx=-kStickMax;
    if(key('D')||key(VK_RIGHT))players[0].lx=kStickMax;
    if(key('W')||key(VK_UP))players[0].ly=kStickMax;
    if(key('S')||key(VK_DOWN))players[0].ly=-kStickMax;
    if(key(VK_RETURN))players[0].buttons|=kA;
    if(key(VK_ESCAPE))players[0].buttons|=kPlus;
    input_script(players[0]);
}
#endif
void read_pad(PadState& p){PadState players[2]{};bool connected[2]{};read_player_pads(players,connected);p=players[0];}

// ---- audio: XAudio2 -------------------------------------------------------------------------------------------

namespace {

std::atomic<uint32_t> g_queued{0};
AudioDone g_done = nullptr;
uint32_t g_frame_bytes = 0;

struct AudioTrace {
    bool enabled = false;
    uint64_t opened_ns = 0;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> submitted_frames{0};
    std::atomic<uint64_t> missing_frames{0};
    std::atomic<uint32_t> underruns{0};
    std::atomic<uint32_t> empty_submissions{0};
    std::atomic<HRESULT> error{S_OK};
    std::atomic<uint64_t> last_submit_ns{0}, max_gap_ns{0}, next_report_ns{0};
    std::atomic<uint32_t> reports{0};
};
AudioTrace g_audio_trace;

struct Voice : IXAudio2VoiceCallback {
    void STDMETHODCALLTYPE OnBufferEnd(void* ctx) noexcept override {
        auto* p = static_cast<std::pair<void*, std::vector<uint8_t>>*>(ctx);
        g_queued--;
        g_done(p->first);
        delete p;
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 bytes_required) noexcept override {
        if (g_audio_trace.enabled && bytes_required && g_audio_trace.running.load(std::memory_order_relaxed) &&
            g_audio_trace.submitted_frames.load(std::memory_order_relaxed)) {
            g_audio_trace.underruns.fetch_add(1, std::memory_order_relaxed);
            g_audio_trace.missing_frames.fetch_add(bytes_required / g_frame_bytes, std::memory_order_relaxed);
        }
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT error) noexcept override {
        g_audio_trace.error.store(error, std::memory_order_relaxed);
    }
};

IXAudio2* g_xa = nullptr;
IXAudio2SourceVoice* g_voice = nullptr;
Voice g_callback;

void trace_audio_submit(size_t bytes, const XAUDIO2_VOICE_STATE& before) {
    const uint64_t total = g_audio_trace.submitted_frames.fetch_add(bytes / g_frame_bytes, std::memory_order_relaxed) + bytes / g_frame_bytes;
    if (!before.BuffersQueued) g_audio_trace.empty_submissions.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = monotonic_ns();
    const uint64_t last = g_audio_trace.last_submit_ns.exchange(now, std::memory_order_relaxed);
    if (last && now > last) {
        const uint64_t gap = now - last;
        uint64_t max_gap = g_audio_trace.max_gap_ns.load(std::memory_order_relaxed);
        while (gap > max_gap && !g_audio_trace.max_gap_ns.compare_exchange_weak(max_gap, gap, std::memory_order_relaxed)) {}
    }
    uint64_t due = g_audio_trace.next_report_ns.load(std::memory_order_relaxed);
    if (now < due || g_audio_trace.reports.load(std::memory_order_relaxed) >= 120 ||
        !g_audio_trace.next_report_ns.compare_exchange_strong(due, now + 2000000000, std::memory_order_relaxed)) return;

    g_audio_trace.reports.fetch_add(1, std::memory_order_relaxed);
    XAUDIO2_VOICE_STATE after{};
    g_voice->GetState(&after);
    std::fprintf(stderr, "XAudio2 t=%.1fs frames=%zu submitted=%llu played=%llu depth=%u->%u empty=%u underruns=%u missing=%llu max_gap=%.1fms error=%#lx\n",
                 double(now - g_audio_trace.opened_ns) / 1e9, bytes / g_frame_bytes, total, after.SamplesPlayed,
                 before.BuffersQueued, after.BuffersQueued, g_audio_trace.empty_submissions.exchange(0),
                 g_audio_trace.underruns.exchange(0), g_audio_trace.missing_frames.exchange(0),
                 double(g_audio_trace.max_gap_ns.exchange(0)) / 1e6, g_audio_trace.error.load());
}

}  // namespace

bool audio_open(AudioDone done, uint32_t channels) {
    if (channels != 2 && channels != 6) return false;
    g_done = done;
    // QA only: preserve the real device/sample clock while silencing the mastering voice.
    const bool qa_silent_output = headless() || std::getenv("NX_MUTE") != nullptr;
    g_audio_trace.enabled = std::getenv("CATHERINE_TRACE_AUDIO") != nullptr;
    if (g_audio_trace.enabled) {
        g_audio_trace.opened_ns = monotonic_ns();
        g_audio_trace.next_report_ns = g_audio_trace.opened_ns + 2000000000;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const HRESULT qa_engine_result = XAudio2Create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(qa_engine_result)) {
        std::fprintf(stderr, "QA_REAL_AUDIO_CLOCK device=failed stage=engine hr=%#lx\n", qa_engine_result);
        return false;
    }
    IXAudio2MasteringVoice* master = nullptr;
    const HRESULT qa_master_result = g_xa->CreateMasteringVoice(&master);
    if (FAILED(qa_master_result)) {
        std::fprintf(stderr, "QA_REAL_AUDIO_CLOCK device=failed stage=master hr=%#lx\n", qa_master_result);
        g_xa->Release();
        g_xa = nullptr;
        return false;
    }
    if (qa_silent_output) {
        const HRESULT qa_mute_result = master->SetVolume(0.0f);
        if (FAILED(qa_mute_result)) {
            std::fprintf(stderr, "QA_REAL_AUDIO_CLOCK device=failed stage=mute hr=%#lx\n", qa_mute_result);
            master->DestroyVoice();
            g_xa->Release();
            g_xa = nullptr;
            return false;
        }
    }
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels = static_cast<WORD>(channels);
    wf.Format.nSamplesPerSec = 48000;
    wf.Format.wBitsPerSample = 16;
    wf.Format.nBlockAlign = static_cast<WORD>(channels * sizeof(int16_t));
    wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
    wf.Format.cbSize = sizeof wf - sizeof wf.Format;
    wf.Samples.wValidBitsPerSample = wf.Format.wBitsPerSample;
    wf.dwChannelMask = channels == 6 ? KSAUDIO_SPEAKER_5POINT1 : KSAUDIO_SPEAKER_STEREO;
    wf.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    g_frame_bytes = wf.Format.nBlockAlign;
    if (FAILED(g_xa->CreateSourceVoice(&g_voice, &wf.Format, 0, 2.0f, &g_callback)) ||
        FAILED(g_voice->Start())) {
        std::fprintf(stderr, "QA_REAL_AUDIO_CLOCK device=failed stage=source_create_or_start\n");
        if (g_voice) g_voice->DestroyVoice();
        master->DestroyVoice();
        g_xa->Release();
        g_voice = nullptr;
        g_xa = nullptr;
        return false;
    }
    std::fprintf(stderr, "QA_REAL_AUDIO_CLOCK device=open channels=%u master_volume=%s trace=%u\n",
                 channels, qa_silent_output ? "0" : "native", unsigned(g_audio_trace.enabled));
    return true;
}

bool audio_submit(const void* data, size_t bytes, void* ctx) {
    if (!g_voice || !bytes || bytes > XAUDIO2_MAX_BUFFER_BYTES || bytes % g_frame_bytes) return false;
    auto* p = new std::pair<void*, std::vector<uint8_t>>(
        ctx, std::vector<uint8_t>(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes));
    XAUDIO2_BUFFER xb{};
    xb.AudioBytes = UINT32(bytes);
    xb.pAudioData = p->second.data();
    xb.pContext = p;
    XAUDIO2_VOICE_STATE before{};
    if (g_audio_trace.enabled) g_voice->GetState(&before, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    g_queued++;
    if (FAILED(g_voice->SubmitSourceBuffer(&xb))) {
        g_queued--;
        delete p;
        return false;
    }
    if (g_audio_trace.enabled) trace_audio_submit(bytes, before);
    return true;
}

uint32_t audio_queued() { return g_queued; }

// ---- Vulkan ---------------------------------------------------------------------------------------------------

void* vulkan_get_instance_proc_addr() {
    HMODULE m = LoadLibraryW(L"vulkan-1.dll");
    return m ? reinterpret_cast<void*>(GetProcAddress(m, "vkGetInstanceProcAddr")) : nullptr;
}

const char* vulkan_surface_extension() { return VK_KHR_WIN32_SURFACE_EXTENSION_NAME; }

int vulkan_create_surface(void* instance, void* native_window, uint64_t* surface, void* gipa) {
    auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(gipa)(static_cast<VkInstance>(instance), "vkCreateWin32SurfaceKHR"));
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = static_cast<HWND>(native_window);
    VkSurfaceKHR s = VK_NULL_HANDLE;
    VkResult r = create(static_cast<VkInstance>(instance), &ci, nullptr, &s);
    *surface = reinterpret_cast<uint64_t>(s);
    return r;
}

}  // namespace nx::plat

// The guest IPC services are C. They share the platform's one PCM voice.
extern "C" int host_audio_open(void (*done)(void*), uint32_t channels) {
    return nx::plat::audio_open(done, channels);
}

extern "C" int host_audio_submit(const void* data, size_t bytes, void* tag) {
    return nx::plat::audio_submit(data, bytes, tag);
}

extern "C" void host_audio_set_running(int running) {
    if (auto* voice = nx::plat::g_voice) {
        nx::plat::g_audio_trace.running.store(running != 0, std::memory_order_relaxed);
        if (running) voice->Start();
        else voice->Stop();
    }
}

extern "C" uint64_t host_audio_played_samples() {
    XAUDIO2_VOICE_STATE state{};
    if (auto* voice = nx::plat::g_voice) voice->GetState(&state);
    return state.SamplesPlayed;
}

extern "C" int host_audio_flush() {
    auto* voice = nx::plat::g_voice;
    return voice && nx::plat::audio_queued() && SUCCEEDED(voice->FlushSourceBuffers());
}

extern "C" void host_audio_set_volume(float volume) {
    if (auto* voice = nx::plat::g_voice) voice->SetVolume(volume);
}

#ifdef CATHERINE_HIGH_REFRESH_BUILD
extern "C" int catherine_guest_precise_sleep(int64_t ns) {
    static const bool enabled = [] {
        const char* option = std::getenv("CATHERINE_GUEST_PRECISE_SLEEP");
        return option && option[0] == '1' && option[1] == '\0';
    }();
    if (!enabled || ns <= 0 || ns > 20'000'000 || catherine_requested_refresh_rate() == 30 ||
        catherine_fullscreen_movie_active()) return 0;

    const uint64_t deadline = nx::plat::monotonic_ns() + uint64_t(ns);
    if (ns > 200'000) nx::plat::sleep_ns(ns - 100'000);
    while (nx::plat::monotonic_ns() < deadline) YieldProcessor();
    return 1;
}
#endif

