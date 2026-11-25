#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace nx::settings_ui {

inline constexpr int kAutoFps = -1;
inline constexpr int kUnlimitedFps = 0;
inline constexpr std::size_t kDigitalBindings = 16;

enum class Page { Display, Controls, Quit };
enum class ApplyResult { Failed, DisplayFailed, Saved, Preview };
enum class PreviewStatus { Pending, Ready, Failed };

struct Values {
    int fps = kAutoFps;
    bool fullscreen = false;
    int window_height = 0;
    std::array<uint64_t, kDigitalBindings> bindings = [] {
        std::array<uint64_t, kDigitalBindings> defaults{};
        for (std::size_t i = 0; i < defaults.size(); ++i) defaults[i] = uint64_t{1} << i;
        return defaults;
    }();
    int anti_aliasing = 0;
    int movie_interpolation = 0;
};

struct Presentation {
    int effective_fps = 0;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    int max_window_height = 0;
    unsigned msaa_mask = 0;
};

struct RawPad {
    uint64_t buttons = 0;
    int32_t lx = 0;
    int32_t ly = 0;
};

using Apply = ApplyResult (*)(const Values&);
using FinishPreview = bool (*)(bool keep);
using ReadPreview = PreviewStatus (*)();
using ReadRawPad = RawPad (*)();
using Quit = void (*)();

// The owning window thread calls everything except is_open(), which is thread-safe.
bool open(HWND owner, Page page, const Values& values, Presentation presentation, Apply apply,
          FinishPreview finish_preview, ReadPreview read_preview, ReadRawPad read_pad, Quit quit);
bool is_open();
void layout(HWND owner);
void close();

}  // namespace nx::settings_ui
