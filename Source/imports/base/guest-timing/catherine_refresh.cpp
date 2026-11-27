#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <charconv>
#include <fstream>
#include <filesystem>
#ifndef _WIN32
#include <cerrno>
#include <unistd.h>
#endif
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <iterator>
#include <mutex>
#include <string>

#include "catherine_refresh.h"
#include "catherine_refresh_policy.h"
#include "catherine_shadow_quality.h"
#include "catherine_settings_win.h"

namespace catherine {
namespace {

constexpr int kFpsLimits[] = {30, 60, 120, 240, kUnlimitedFps};

bool valid_fps(int rate) {
    for (int limit : kFpsLimits)
        if (rate == limit) return true;
    return rate == kAutoFps;
}

bool valid_resolution(int height) {
    return height == 0 || height == 720 || height == 1080 || height == 1440 || height == 2160;
}

bool valid_anti_aliasing(int mode) {
    return mode == 0 || mode == 1 || mode == 2 || mode == 4 || mode == 8;
}

bool valid_movie_interpolation(int rate) {
    return rate == -1 || rate == 0 || rate == 60 || rate == 120 || rate == 240;
}

bool valid_bindings(uint64_t bindings) {
    unsigned seen = 0;
    for (unsigned logical = 0; logical < 16; ++logical) {
        const unsigned physical = unsigned(bindings >> (logical * 4)) & 15u;
        if ((logical < 12 && physical >= 12) || (logical >= 12 && physical != logical)) return false;
        const unsigned bit = 1u << physical;
        if (seen & bit) return false;
        seen |= bit;
    }
    return true;
}

bool valid_input_settings(const HostSettings& value) {
    return valid_bindings(value.bindings) && valid_keyboard(value.keyboard,value.movement_preset) &&
           valid_bindings(value.player2.bindings) && valid_keyboard(value.player2.keyboard,value.player2.movement_preset);
}

void copy_input_settings(HostSettings& target,const HostSettings& source) {
    target.bindings=source.bindings;
    target.keyboard=source.keyboard;
    target.movement_preset=source.movement_preset;
    target.swap_sticks=source.swap_sticks;
    target.player2=source.player2;
}

bool same_input_settings(const HostSettings& a,const HostSettings& b) {
    return a.bindings==b.bindings && a.keyboard==b.keyboard &&
           a.movement_preset==b.movement_preset && a.swap_sticks==b.swap_sticks && a.player2==b.player2;
}

struct DisplayMode {
    int hz = 60;
    int width = 1280;
    int height = 720;
};

HostDisplay primary_display() {
#ifdef _WIN32
    return MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
#else
    // The window backend updates this once it has selected a display.
    return {};
#endif
}

DisplayMode query_display_mode(HostDisplay monitor) {
#ifdef _WIN32
    MONITORINFOEXW info{};
    info.cbSize = sizeof info;
    DEVMODEW mode{};
    mode.dmSize = sizeof mode;
    DisplayMode result;
    if (monitor && GetMonitorInfoW(monitor, &info) &&
        EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        if (mode.dmDisplayFrequency >= 2) result.hz = static_cast<int>(mode.dmDisplayFrequency);
        if (mode.dmPelsWidth && mode.dmPelsHeight) {
            result.width = static_cast<int>(mode.dmPelsWidth);
            result.height = static_cast<int>(mode.dmPelsHeight);
        }
    }
    return result;
#else
    return {monitor.hz, monitor.width, monitor.height};
#endif
}

int auto_height(const DisplayMode& display) {
    for (int height : {2160, 1440, 1080})
        if (display.height >= height && display.width >= height * 16 / 9) return height;
    return 720;
}

std::filesystem::path settings_path() {
#ifdef _WIN32
    wchar_t path[32768];
    const DWORD size = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!size || size >= std::size(path)) return {};
    const wchar_t* filename = std::wcsrchr(path, L'\\');
    return filename ? std::wstring(path, size_t(filename + 1 - path)) + L"catherine-settings.ini" : std::wstring{};
#else
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path() / "catherine-settings.ini";
#endif
}

struct Ini : std::map<std::string, std::map<std::string, std::string>> {
    bool native_gpu_hint = false;
};

std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == text.npos ? std::string{} :
        std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

Ini read_ini(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    // Older Win32 profile readers also accepted UTF-16LE INIs saved by Notepad.
    if (bytes.starts_with("\xff\xfe")) {
#ifdef _WIN32
        using IniCodeUnit = wchar_t;
#else
        using IniCodeUnit = char16_t;
#endif
        std::basic_string<IniCodeUnit> wide;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2)
            wide.push_back(IniCodeUnit(static_cast<unsigned char>(bytes[i]) |
                                   (unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8)));
#ifdef _WIN32
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
        bytes.resize(size);
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), bytes.data(), size, nullptr, nullptr);
#else
        try {
            bytes = std::filesystem::path(wide).string();
        } catch (const std::filesystem::filesystem_error&) {
            return {};
        }
#endif
    }
    std::istringstream input(std::move(bytes));
    Ini ini;
    std::string line, section;
    while (std::getline(input, line)) {
        if (line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == ';' || line[0] == '#') {
            const auto hint = lower(trim(std::string_view(line).substr(1)));
            const auto equals = hint.find('=');
            if (section == "catherine" && equals != hint.npos &&
                trim(hint.substr(0, equals)) == "nativegpuskinning" && trim(hint.substr(equals + 1)) == "false")
                ini.native_gpu_hint = true;
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(std::string_view(line).substr(1, line.size() - 2)));
            continue;
        }
        const auto equals = line.find('=');
        if (equals != line.npos)
            ini[section][lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
    }
    return ini;
}

std::string field(const Ini& ini, const std::string& section, const char* name) {
    const auto group = ini.find(lower(section));
    if (group == ini.end()) return {};
    const auto it = group->second.find(lower(name));
    return it == group->second.end() ? std::string{} : it->second;
}

template<class T>
std::optional<T> parse_integer(std::string_view text, int base = 10) {
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return {};
    return value;
}

int boolean(std::string text, int fallback) {
    text = lower(text);
    if (text == "true" || text == "1") return 1;
    if (text == "false" || text == "0") return 0;
    return fallback;
}

int load_shadow_resolution(const Ini& ini) {
    const auto text = lower(field(ini, "Catherine", "ShadowResolution"));
    if (text == "original" || text == "native") return 2048;
    const auto value = parse_integer<int>(text);
    return value && (*value == 2048 || *value == 4096 || *value == 8192) ? *value : 0;
}

constexpr const char* actions[] = {
    "Grab", "Cancel", "Phone", "Item", "LeftStickPress", "RightStickPress",
    "PreviousTab", "NextTab", "LeftTrigger", "RightTrigger", "Pause", "View",
    "MoveLeft", "MoveUp", "MoveRight", "MoveDown", "CameraLeft", "CameraUp",
    "CameraRight", "CameraDown", "MouseGrab", "MouseCancel", "ArrowLeft",
    "ArrowUp", "ArrowRight", "ArrowDown", "AlternateConfirm"
};
constexpr const char* pad_buttons[] = {
    "East", "South", "North", "West", "LeftStick", "RightStick",
    "LeftShoulder", "RightShoulder", "LeftTrigger", "RightTrigger", "Plus", "Minus"
};
static_assert(std::size(actions) == kKeyboardBindings);

std::string key_text(unsigned key) {
    const char* name = keyboard_key_name(key);
    return std::string_view(name) == "Key" ? "VK" + std::to_string(key) : name;
}

int parse_key(const std::string& text) {
    const auto name = lower(text);
    if (name.starts_with("vk"))
        return parse_integer<int>(std::string_view(name).substr(2)).value_or(-1);
    for (unsigned i = 1; i <= kWheelDown; ++i) {
        const auto label = keyboard_key_name(i);
        if (std::string_view(label) != "Key" && lower(label) == name) return int(i);
    }
    return -1;
}

void load_profile(const Ini& ini, HostSettings& settings, unsigned player, int version) {
    const auto legacy = [&](const char* key) {
        return field(ini, "Catherine", ((player ? std::string("P2") : "") + key).c_str());
    };
    auto& profile = input_profile(settings, player);
    const auto preset = parse_integer<int>(legacy("MovementPreset")).value_or(0);
    profile.movement_preset = valid_movement_preset(preset) ? preset : 0;
    profile.swap_sticks = boolean(legacy("SwapSticks"), false);
    profile.keyboard = default_keyboard(profile.movement_preset);

    auto old_pad = legacy("Bindings");
    auto packed = parse_integer<uint64_t>(old_pad, 16);
    if (old_pad.size() == 16 && packed && valid_bindings(*packed))
        profile.bindings = version < 6 && *packed == kIdentityBindings ? kDefaultBindings : *packed;
    auto keys = profile.keyboard;
    auto old_keys = legacy("Keyboard");
    if (old_keys.size() == keys.size() * 4) {
        bool valid = true;
        for (unsigned i = 0; i < keys.size(); ++i) {
            auto key = parse_integer<uint16_t>(std::string_view(old_keys).substr(i * 4, 4), 16);
            if (!key) { valid = false; break; }
            keys[i] = *key;
        }
        if (valid && change_movement_preset(keys, profile.movement_preset, profile.movement_preset))
            profile.keyboard = keys;
    }

    const std::string prefix = player ? "Player2." : "Player1.";
    keys = profile.keyboard;
    auto pad = profile.bindings;
    for (unsigned i = 0; i < keys.size(); ++i) {
        const auto text = field(ini, prefix + "Keyboard", actions[i]);
        const int key = parse_key(text);
        const int maximum = keyboard_binding_editable(i) ? kWheelDown : UINT16_MAX;
        if (key >= int(keyboard_binding_editable(i)) && key <= maximum)
            keys[i] = static_cast<uint16_t>(key);
        if (i >= std::size(pad_buttons)) continue;
        const auto button = lower(field(ini, prefix + "Gamepad", actions[i]));
        for (unsigned physical = 0; physical < std::size(pad_buttons); ++physical) {
            if (lower(pad_buttons[physical]) != button) continue;
            pad = (pad & ~(uint64_t{15} << (i * 4))) | (uint64_t{physical} << (i * 4));
            break;
        }
    }
    set_owned_movement_keys(keys, profile.movement_preset);
    if (valid_keyboard(keys, profile.movement_preset)) profile.keyboard = keys;
    if (valid_bindings(pad)) profile.bindings = pad;
}

HostSettings load_settings(const Ini& ini) {
    HostSettings settings;
    const auto value = [&](const char* key) { return field(ini, "Catherine", key); };
    const auto number = [&](const char* key, int fallback) {
        return parse_integer<int>(value(key)).value_or(fallback);
    };
    const int fps = number("Fps", 60), height = number("WindowHeight", 0);
    const int aa = number("AntiAliasing", 0), movie = number("MovieInterpolation", 0);
    const int prompts = number("ButtonPrompts", 0);
    if (valid_fps(fps)) settings.fps = fps;
    if (valid_resolution(height)) settings.window_height = height;
    if (valid_anti_aliasing(aa)) settings.anti_aliasing = aa;
    if (valid_movie_interpolation(movie)) settings.movie_interpolation = movie;
    if (prompts >= 0 && prompts <= 4) settings.button_prompts = prompts;
    settings.fullscreen = boolean(value("Fullscreen"), false);
    settings.vsync = boolean(value("VSync"), false);
    settings.triple_buffering = boolean(value("TripleBuffering"), false);
    settings.alt_build_mode = boolean(value("AltBuildMode"), false);
    const int upscaling = number("InternalUpscaling", 1);
    if (upscaling >= 0 && upscaling <= 4) settings.internal_upscaling = upscaling;
    for (unsigned player = 0; player < 2; ++player) {
        load_profile(ini, settings, player, number("Version", 0));
        const auto device = value(player ? "P2Device" : "P1Device");
        if (!device.empty()) settings.input_devices[player] = device;
    }
    if (settings.input_devices[0] == "none") settings.input_devices[0] = "auto";
    if (settings.input_devices[1] == "auto" || settings.input_devices[0] == "auto" ||
        settings.input_devices[0] == settings.input_devices[1]) settings.input_devices[1] = "none";
    return settings;
}

// Menu descriptor IDs and guest offsets from f_7000034100/f_7000034720.
// Boolean values use the native first/second option and are documented below.
enum class NativeKind { Choice, Toggle, Brightness, Difficulty };
struct NativeOption { const char* key; unsigned offset; NativeKind kind; };
constexpr NativeOption native_options[] = {
    {"PuzzleType", 0x9b5c, NativeKind::Choice}, {"PuzzleDifficulty", 0x9b58, NativeKind::Difficulty},
    {"Subtitles", 0x300, NativeKind::Toggle}, {"Brightness", 0x2fb, NativeKind::Brightness},
    {"Vibration", 0x2f4, NativeKind::Toggle}, {"InvertCameraY", 0x2f5, NativeKind::Choice},
    {"InvertCameraX", 0x2f6, NativeKind::Choice}, {"BacksideCamera", 0x2f9, NativeKind::Toggle},
    {"Guidelines", 0x2fa, NativeKind::Toggle}, {"RetryAssist", 0x2ff, NativeKind::Toggle},
    {"PuzzleTutorial", 0x2fe, NativeKind::Toggle}, {"VoiceLanguage", 0x301, NativeKind::Choice}
};
constexpr int brightness_steps[] = {-100,-90,-80,-70,-60,-50,-40,-30,-20,-10,0,9,18,27,36,45,54,63,72,81,90};
using NativeValues = std::array<std::optional<int>, std::size(native_options)>;

bool valid_native(const NativeOption& option, int value) {
    if (option.kind == NativeKind::Brightness) return value >= 0 && value <= 100 && value % 5 == 0;
    return value >= 0 && value <= (option.kind == NativeKind::Difficulty ? 3 : 1);
}

int read_native(const unsigned char* game, const NativeOption& option) {
    const int raw = game[option.offset];
    switch (option.kind) {
    case NativeKind::Toggle: return raw == 0;
    case NativeKind::Difficulty: return 3 - raw;
    case NativeKind::Brightness: {
        const int brightness = static_cast<int8_t>(raw);
        const auto step = std::lower_bound(std::begin(brightness_steps), std::end(brightness_steps), brightness);
        return (std::min)(20, int(step - brightness_steps)) * 5;
    }
    default: return raw;
    }
}

void apply_native(unsigned char* game, const NativeOption& option, int value) {
    int raw = value;
    if (option.kind == NativeKind::Toggle) raw = !value;
    if (option.kind == NativeKind::Difficulty) raw = 3 - value;
    if (option.kind == NativeKind::Brightness) raw = brightness_steps[value / 5];
    // Native input tracks a second axis byte. A menu toggle flips both copies.
    if (option.offset == 0x2f5 || option.offset == 0x2f6)
        game[option.offset + 2] ^= game[option.offset] ^ static_cast<unsigned char>(raw);
    game[option.offset] = static_cast<unsigned char>(raw);
}

NativeValues load_native(const Ini& ini) {
    NativeValues values{};
    for (unsigned i = 0; i < values.size(); ++i) {
        const auto& option = native_options[i];
        const auto text = field(ini, "Game", option.key);
        const int value = option.kind == NativeKind::Toggle ? boolean(text, -1)
            : parse_integer<int>(text).value_or(-1);
        if (valid_native(option, value)) values[i] = value;
    }
    return values;
}

bool write_settings(const std::filesystem::path& path, const HostSettings& value, bool devmenu, const NativeValues& native, const std::array<bool, 3>& intros, bool native_gpu_skinning, int shadow_resolution) {
    if (path.empty()) return false;
    std::ostringstream out;
    out << "[Catherine]\nVersion=7\ndevmenu=" << (devmenu ? "true" : "false")
        << "\nFps=" << value.fps << "\nFullscreen=" << int(value.fullscreen)
        << "\nVSync=" << (value.vsync ? "true" : "false")
        << "\nTripleBuffering=" << (value.triple_buffering ? "true" : "false")
        << "\nAltBuildMode=" << (value.alt_build_mode ? "true" : "false")
        << (native_gpu_skinning
            ? "\n; Uncomment this line to disable GPU skinning (restart required):\n; NativeGpuSkinning=false"
            : "\n; Comment this line to enable GPU skinning (restart required):\nNativeGpuSkinning=false")
        << "\n; ShadowResolution: Auto (default), 2048 (original), 4096, or 8192."
        << "\n; Auto uses 4096 at 720p/1080p and 8192 at 1440p/4K. Higher sizes use more GPU memory."
        << "\n; Auto follows internal resolution changes. Restart after editing this INI option."
        << "\nShadowResolution=" << (shadow_resolution ? std::to_string(shadow_resolution) : "Auto")
        << "\n; InternalUpscaling: 0=Off (1280x720), 1=Auto (default)"
        << "\n; 2=1920x1080, 3=2560x1440, 4=3840x2160"
        << "\n; Auto picks the highest level that fits the current screen/window."
        << "\n; Explicit levels are capped to fit; 720p screens use 1280x720."
        << "\nInternalUpscaling=" << value.internal_upscaling
        << "\nWindowHeight=" << value.window_height << "\nAntiAliasing=" << value.anti_aliasing
        << "\nMovieInterpolation=" << value.movie_interpolation << "\nButtonPrompts=" << value.button_prompts;
    for (unsigned player = 0; player < 2; ++player) {
        const auto& profile = input_profile(value, player);
        const char* prefix = player ? "P2" : "";
        out << "\n" << prefix << "MovementPreset=" << profile.movement_preset
            << "\n" << prefix << "SwapSticks=" << int(profile.swap_sticks)
            << "\nP" << player + 1 << "Device=" << value.input_devices[player];
    }
    out << "\n\n[Progress]\nBabelIntroSeen=" << (intros[0] ? "true" : "false")
        << "\nColosseumIntroSeen=" << (intros[1] ? "true" : "false")
        << "\nJokerIntroSeen=" << (intros[2] ? "true" : "false") << "\n";
    out << "\n[Game]\n";
    for (unsigned i = 0; i < native.size(); ++i) {
        if (!native[i]) continue;
        const auto& option = native_options[i];
        out << option.key << '=';
        if (option.kind == NativeKind::Toggle) out << (*native[i] ? "true" : "false");
        else out << *native[i];
        out << '\n';
    }
    for (unsigned player = 0; player < 2; ++player) {
        const auto& profile = input_profile(value, player);
        out << "\n[Player" << player + 1 << ".Keyboard]\n";
        for (unsigned i = 0; i < profile.keyboard.size(); ++i)
            out << actions[i] << '=' << key_text(profile.keyboard[i]) << '\n';
        out << "\n[Player" << player + 1 << ".Gamepad]\n";
        for (unsigned i = 0; i < std::size(pad_buttons); ++i)
            out << actions[i] << '=' << pad_buttons[(profile.bindings >> (i * 4)) & 15] << '\n';
    }
    const auto content = out.str();
#ifdef _WIN32
    const auto length = static_cast<DWORD>(content.size());
    const std::wstring temporary = path.native() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool complete = WriteFile(file, content.data(), DWORD(length), &written, nullptr) &&
                          written == DWORD(length) && FlushFileBuffers(file);
    CloseHandle(file);
    if (complete && MoveFileExW(temporary.c_str(), path.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.c_str());
    return false;
#else
    std::string temporary = path.native() + ".XXXXXX";
    const int file = mkstemp(temporary.data());
    if (file < 0) return false;
    size_t written = 0;
    while (written < content.size()) {
        const ssize_t count = write(file, content.data() + written, content.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += static_cast<size_t>(count);
    }
    const bool complete = written == content.size() && fsync(file) == 0;
    const bool closed = close(file) == 0;
    if (complete && closed && rename(temporary.c_str(), path.c_str()) == 0) return true;
    unlink(temporary.c_str());
    return false;
#endif
}

const Ini& initial_ini(const std::filesystem::path& path) {
    static const Ini ini = read_ini(path);
    return ini;
}

struct RuntimeSettings {
    const std::filesystem::path path = settings_path();
    std::mutex mutex;
    HostSettings values = load_settings(initial_ini(path));
    HostSettings config_original;
    int config_original_debug_rate = kAutoFps;
    bool config_editing = false;
    HostSettings bindings_draft;
    bool bindings_editing = false;
    unsigned binding_player = 0;
    DisplayMode display = query_display_mode(primary_display());
    unsigned internal_height = internal_upscaling_height(values.internal_upscaling, display.width, display.height, values.fullscreen, values.window_height);
    std::atomic<uint64_t> bindings{values.bindings};
    std::atomic<int> anti_aliasing{values.anti_aliasing};
    std::atomic<int> button_prompts{values.button_prompts};
    std::atomic<bool> swap_sticks{values.swap_sticks};
    std::array<std::atomic<uint16_t>,kKeyboardBindings> keyboard{};
    std::array<std::atomic<uint16_t>,kKeyboardBindings> player2_keyboard{};
    std::atomic<uint64_t> keyboard_version{1};
    std::atomic<uint64_t> player2_keyboard_version{1};
    RuntimeSettings(){
        for(unsigned i=0;i<kKeyboardBindings;++i){keyboard[i].store(values.keyboard[i]);player2_keyboard[i].store(values.player2.keyboard[i]);}
        if(((native_gpu_skinning && (!initial_ini(path).native_gpu_hint ||
           !field(initial_ini(path),"Catherine","NativeGpuSkinning").empty())) ||
           field(initial_ini(path),"Catherine","ShadowResolution").empty()) &&
           !write_settings(path,values,devmenu,native,intros,native_gpu_skinning,shadow_resolution))
            std::fprintf(stderr,"Could not initialize NativeGpuSkinning in catherine-settings.ini\n");
    }
    void publish_keyboard(){
        for(unsigned i=0;i<kKeyboardBindings;++i){keyboard[i].store(values.keyboard[i],std::memory_order_relaxed);player2_keyboard[i].store(values.player2.keyboard[i],std::memory_order_relaxed);}
        keyboard_version.fetch_add(1,std::memory_order_release);player2_keyboard_version.fetch_add(1,std::memory_order_release);
    }
    int debug_rate = [] {
        if (std::getenv("NX_NO_FRAME_LIMIT")) return kUnlimitedFps;
        const char* rate = std::getenv("CATHERINE_REFRESH_RATE");
        return rate ? refresh::parse_rate(rate) : kAutoFps;
    }();
    std::atomic<int> effective{resolve_rate()};
    std::atomic<int> effective_movie{resolve_movie_rate()};

    const bool devmenu = boolean(field(initial_ini(path), "Catherine", "devmenu"), false);
    NativeValues native = load_native(initial_ini(path));
    std::array<bool, 3> intros{
        bool(boolean(field(initial_ini(path), "Progress", "BabelIntroSeen"), false)),
        bool(boolean(field(initial_ini(path), "Progress", "ColosseumIntroSeen"), false)),
        bool(boolean(field(initial_ini(path), "Progress", "JokerIntroSeen"), false))};
    // INI-only startup choice, kept outside the shared presentation settings ABI.
    const bool native_gpu_skinning = boolean(field(initial_ini(path), "Catherine", "NativeGpuSkinning"), true);
    const int shadow_resolution = load_shadow_resolution(initial_ini(path));

    int resolve_rate() const {
        if (debug_rate != kAutoFps) return debug_rate;
        if (values.fps != kAutoFps) return values.fps;
        return display.hz > 240 ? kUnlimitedFps : (display.hz < 30 ? 30 : display.hz);
    }

    int resolve_movie_rate() const {
        if (values.movie_interpolation != -1) return values.movie_interpolation;
        const int gameplay = resolve_rate();
        const int match = gameplay == kUnlimitedFps ? display.hz : gameplay;
        return match <= 30 ? 0 : std::clamp(match, 30, 240);
    }

    void publish_rates() {
        effective.store(resolve_rate(), std::memory_order_release);
        effective_movie.store(resolve_movie_rate(), std::memory_order_release);
    }
};

RuntimeSettings& runtime() {
    static RuntimeSettings state;
    return state;
}

}  // namespace

HostSettings host_settings() {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    return state.values;
}

bool store_host_settings(const HostSettings& value, bool explicit_fps) {
    if (!valid_fps(value.fps) || !valid_resolution(value.window_height) ||
        !valid_anti_aliasing(value.anti_aliasing) || !valid_movie_interpolation(value.movie_interpolation) ||
        !valid_input_settings(value)||value.button_prompts<0||value.button_prompts>4||value.internal_upscaling<0||value.internal_upscaling>4) return false;
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    // Config choices take effect for preview, but only Save Settings writes them.
    if (!state.config_editing && !write_settings(state.path, value, state.devmenu, state.native, state.intros, state.native_gpu_skinning, state.shadow_resolution)) return false;
    if (explicit_fps || value.fps != state.values.fps) state.debug_rate = kAutoFps;
    const bool keyboard_changed=state.values.keyboard!=value.keyboard||state.values.player2.keyboard!=value.player2.keyboard;
    state.values = value;
    if(keyboard_changed)state.publish_keyboard();
    state.bindings.store(value.bindings, std::memory_order_release);
    state.swap_sticks.store(value.swap_sticks, std::memory_order_release);
    state.anti_aliasing.store(value.anti_aliasing, std::memory_order_release);
    state.button_prompts.store(value.button_prompts,std::memory_order_release);
    state.publish_rates();
    return true;
}

HostSettings bindings_settings() {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    return state.bindings_editing?state.bindings_draft:state.values;
}

void begin_bindings_settings(unsigned player) {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    if(state.bindings_editing)return;
    state.binding_player=player?1:0;
    state.bindings_draft=state.values;state.bindings_editing=true;
}

bool store_bindings_draft(const HostSettings& value) {
    if(!valid_input_settings(value))return false;
    auto& state=runtime();std::lock_guard lock(state.mutex);
    if(!state.bindings_editing){state.bindings_draft=state.values;state.bindings_editing=true;}
    input_profile(state.bindings_draft,state.binding_player)=input_profile(value,state.binding_player);
    return true;
}

bool bindings_settings_dirty() {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    return state.bindings_editing&&input_profile(state.bindings_draft,state.binding_player)!=input_profile(state.values,state.binding_player);
}

bool bindings_settings_active() {
    auto& state=runtime();std::lock_guard lock(state.mutex);return state.bindings_editing;
}

bool save_bindings_settings() {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    if(!state.bindings_editing)return true;
    // Input Apply is an independent transaction: do not persist the main Config draft.
    auto persisted=state.config_editing?state.config_original:state.values;
    input_profile(persisted,state.binding_player)=input_profile(state.bindings_draft,state.binding_player);
    if(!write_settings(state.path, persisted, state.devmenu, state.native, state.intros, state.native_gpu_skinning, state.shadow_resolution))return false;
    const bool keyboard_changed=state.values.keyboard!=state.bindings_draft.keyboard||state.values.player2.keyboard!=state.bindings_draft.player2.keyboard;
    input_profile(state.values,state.binding_player)=input_profile(state.bindings_draft,state.binding_player);
    if(state.config_editing)input_profile(state.config_original,state.binding_player)=input_profile(state.bindings_draft,state.binding_player);
    if(keyboard_changed)state.publish_keyboard();
    state.bindings.store(state.values.bindings,std::memory_order_release);
    state.swap_sticks.store(state.values.swap_sticks,std::memory_order_release);
    state.bindings_draft=state.values;
    return true;
}

void discard_bindings_settings() {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    state.bindings_draft=state.values;state.bindings_editing=false;
}

bool controller_swap_sticks(){return runtime().swap_sticks.load(std::memory_order_acquire);}

void begin_config_settings() {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    if (state.config_editing) return;
    state.config_original = state.values;
    state.config_original_debug_rate = state.debug_rate;
    state.config_editing = true;
}

bool save_config_settings() {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    if (!state.config_editing) return true;
    if (!write_settings(state.path, state.values, state.devmenu, state.native, state.intros, state.native_gpu_skinning, state.shadow_resolution)) return false;
    state.config_original = state.values;
    state.config_original_debug_rate = state.debug_rate;
    return true;
}

bool end_config_settings() {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    if (!state.config_editing) return false;
    state.config_editing = false;
    state.bindings_editing = false;
    const bool keyboard_changed=state.values.keyboard!=state.config_original.keyboard||state.values.player2.keyboard!=state.config_original.player2.keyboard;
    state.values = state.config_original;
    if(keyboard_changed)state.publish_keyboard();
    state.debug_rate = state.config_original_debug_rate;
    state.bindings.store(state.values.bindings, std::memory_order_release);
    state.swap_sticks.store(state.values.swap_sticks, std::memory_order_release);
    state.anti_aliasing.store(state.values.anti_aliasing, std::memory_order_release);
    state.button_prompts.store(state.values.button_prompts,std::memory_order_release);
    state.publish_rates();
    return true;
}

unsigned keyboard_binding(unsigned index,unsigned player){
    if(index>=kKeyboardBindings)return 0;
    auto& state=runtime();return (player?state.player2_keyboard[index]:state.keyboard[index]).load(std::memory_order_relaxed);
}
uint64_t keyboard_binding_version(unsigned player){
    auto& state=runtime();return (player?state.player2_keyboard_version:state.keyboard_version).load(std::memory_order_acquire);
}

unsigned display_render_height(HostDisplay monitor, bool fullscreen, int window_height) {
    const auto display = query_display_mode(monitor);
    return internal_upscaling_height(host_settings().internal_upscaling, display.width, display.height, fullscreen, window_height);
}

int requested_anti_aliasing() {
    return runtime().anti_aliasing.load(std::memory_order_acquire);
}
int button_prompt_style(){return runtime().button_prompts.load(std::memory_order_acquire);}

void update_display_mode(HostDisplay monitor) {
    const DisplayMode display = query_display_mode(monitor);
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    state.display = display;
    state.publish_rates();
}

#ifndef _WIN32
void initialize_display_mode(HostDisplay monitor, bool fullscreen, int window_height) {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    state.display = query_display_mode(monitor);
    state.internal_height = internal_upscaling_height(state.values.internal_upscaling, state.display.width, state.display.height, fullscreen, window_height);
    state.publish_rates();
}
#endif

int step_fps_limit(int effective, int direction) {
    if (direction > 0) {
        if (effective == kUnlimitedFps) return kUnlimitedFps;
        for (int limit : kFpsLimits)
            if (limit == kUnlimitedFps || limit > effective) return limit;
    } else if (direction < 0) {
        if (effective == kUnlimitedFps) return 240;
        for (int index = 3; index >= 0; --index)
            if (kFpsLimits[index] < effective) return kFpsLimits[index];
        return 30;
    }
    return effective;
}

uint64_t remap_pad_buttons(uint64_t buttons) {
    const uint64_t map = pad_binding_map();
    if (map == kIdentityBindings || !buttons) return buttons;
    uint64_t result = buttons & ~uint64_t{0xffff};
    for (unsigned logical = 0; logical < 16; ++logical) {
        const unsigned physical = unsigned(map >> (logical * 4)) & 15u;
        result |= ((buttons >> physical) & 1u) << logical;
    }
    return result;
}

uint64_t pad_binding_map() {
    return runtime().bindings.load(std::memory_order_acquire);
}

}  // namespace catherine

extern "C" int catherine_requested_refresh_rate(void) {
    return catherine::runtime().effective.load(std::memory_order_acquire);
}

extern "C" int catherine_requested_movie_interpolation(void) {
    return catherine::runtime().effective_movie.load(std::memory_order_acquire);
}

extern "C" unsigned catherine_internal_render_height(void) {
    return catherine::runtime().internal_height;
}

extern "C" int catherine_dev_enabled(void) {
    return catherine::runtime().devmenu;
}

extern "C" int catherine_shadow_resolution_setting(void) {
    return catherine::runtime().shadow_resolution;
}

extern "C" int catherine_native_gpu_skinning_enabled(void) {
    return catherine::runtime().native_gpu_skinning;
}

extern "C" void catherine_load_native_config(unsigned char* game, int load_campaign_defaults) {
    using namespace catherine;
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    bool missing = false;
    for (unsigned i = 0; i < state.native.size(); ++i) {
        const auto& option = native_options[i];
        if (!state.native[i]) {
            const int inherited = read_native(game, option);
            if (!valid_native(option, inherited)) continue;
            state.native[i] = inherited;
            missing = true;
        }
        if (i >= 2 || load_campaign_defaults) apply_native(game, option, *state.native[i]);
    }
    if (missing && !write_settings(state.path, state.values, state.devmenu, state.native, state.intros, state.native_gpu_skinning, state.shadow_resolution))
        std::fprintf(stderr, "Could not initialize catherine-settings.ini\n");
}

extern "C" int catherine_save_native_config(const unsigned char* game, int campaign_options_editable) {
    using namespace catherine;
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    auto next = state.native;
    for (unsigned i = 0; i < next.size(); ++i) {
        if (i < 2 && !campaign_options_editable) continue;
        const int value = read_native(game, native_options[i]);
        if (valid_native(native_options[i], value)) next[i] = value;
    }
    if (!write_settings(state.path, state.values, state.devmenu, next, state.intros, state.native_gpu_skinning, state.shadow_resolution)) return 0;
    state.native = next;
    state.config_original = state.values;
    state.config_original_debug_rate = state.debug_rate;
    return 1;
}

// Returns whether the requested optional-mode introduction should run.
// An explicit DEV cutscene bypasses this policy at the guest call site.
extern "C" int catherine_mode_intro_should_play(unsigned group, unsigned scene) {
    const int mode = group == 102 && scene == 10 ? 0 : group == 101 && scene == 11 ? 1 :
        group == 450 && scene == 10 ? 2 : -1;
    if (mode < 0) return 1;
    auto& state = catherine::runtime();
    std::lock_guard lock(state.mutex);
    if (state.intros[mode]) return 0;
    state.intros[mode] = true;
    if (!catherine::write_settings(state.path, state.values, state.devmenu, state.native, state.intros, state.native_gpu_skinning, state.shadow_resolution))
        std::fprintf(stderr, "Could not save optional-mode introduction progress.\n");
    return 1;
}

// Title menus accept a pointer for either keyboard assignment or P1 Auto.
extern "C" int catherine_title_mouse_enabled() {
    const auto settings = catherine::host_settings();
    return settings.input_devices[0] == "auto" ||
           settings.input_devices[0] == "keyboard" ||
           settings.input_devices[1] == "keyboard";
}

// Transient native menu ownership; never persisted to the user's settings.
static std::atomic<unsigned> catherine_menu_cursor_owners{0};
extern "C" void catherine_menu_cursor_set(unsigned owner, int visible) {
    if (visible) catherine_menu_cursor_owners.fetch_or(owner, std::memory_order_relaxed);
    else catherine_menu_cursor_owners.fetch_and(~owner, std::memory_order_relaxed);
}
extern "C" int catherine_menu_cursor_visible(void) {
    return catherine_menu_cursor_owners.load(std::memory_order_relaxed) != 0;
}

extern "C" int catherine_alt_build_mode(void) {
    return catherine::host_settings().alt_build_mode;
}
