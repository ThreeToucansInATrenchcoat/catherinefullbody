#include "settings_overlay_win.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <iterator>
#include <string>
#include <windowsx.h>

#include "platform.h"

namespace nx::settings_ui {
namespace {

constexpr wchar_t kClass[] = L"CatherineSettingsOverlay";
constexpr UINT_PTR kPadTimer = 1;
constexpr int kWidth = 960, kHeight = 550;
constexpr int kLeft = 46, kRight = 914;
constexpr int kGraphicsRows = 5;
constexpr int kBindings = static_cast<int>(kDigitalBindings);
constexpr int kResetRow = kBindings;
constexpr uint64_t kDigitalMask = (uint64_t{1} << kBindings) - 1;
constexpr uint64_t kCancelChord = plat::kA | plat::kB;
constexpr int kFps[] = {kAutoFps, 30, 60, 120, 240, kUnlimitedFps};
constexpr int kResolutions[] = {0, 720, 1080, 1440, 2160};
constexpr int kAntiAliasing[] = {0, 1, 2, 4, 8};
constexpr int kMovieRates[] = {0, 60, 120, 240, -1};

constexpr const wchar_t* kActions[] = {
    L"Confirm / Interact", L"Back / Run", L"Top action", L"Left action",
    L"Left stick press", L"Right stick press", L"Undo / Previous tab", L"Autoplay / Next tab",
    L"Left trigger action", L"Right trigger action", L"Pause / Menu", L"View / Select",
    L"Move left", L"Move up", L"Move right", L"Move down",
};
constexpr const wchar_t* kXboxButtons[] = {
    L"B", L"A", L"Y", L"X", L"LS", L"RS", L"LB", L"RB", L"LT", L"RT", L"Menu", L"View",
    L"D-pad left", L"D-pad up", L"D-pad right", L"D-pad down",
};
constexpr const wchar_t* kDualSenseButtons[] = {
    L"Circle", L"Cross", L"Triangle", L"Square", L"L3", L"R3", L"L1", L"R1", L"L2", L"R2", L"Options", L"Create",
    L"D-pad left", L"D-pad up", L"D-pad right", L"D-pad down",
};
static_assert(std::size(kActions) == kDigitalBindings && std::size(kXboxButtons) == kDigitalBindings &&
              std::size(kDualSenseButtons) == kDigitalBindings);

enum class Direction { None, Up, Down, Left, Right };
enum class Font { Title, Body, Small, Button, Count };

struct State {
    HWND window = nullptr;
    HWND owner = nullptr;
    Apply apply = nullptr;
    FinishPreview finish_preview = nullptr;
    ReadPreview read_preview = nullptr;
    ReadRawPad read_pad = nullptr;
    Quit quit = nullptr;
    Values original, draft;
    Presentation presentation;
    Page page = Page::Display;
    int row = 0;
    int binding_column = 0;
    int capture = -1;
    bool capture_armed = false;
    bool pad_armed = false;
    bool save_failed = false;
    bool preview_failed = false;
    bool keep_selected = false;
    bool dualsense = false;
    uint64_t capture_button = 0;
    uint64_t previous_buttons = 0;
    ULONGLONG next_repeat = 0;
    ULONGLONG preview_deadline = 0;
    unsigned preview_seconds = 0;
    PreviewStatus preview_status = PreviewStatus::Pending;
    Direction previous_direction = Direction::None;
};

State g;
std::atomic_bool g_open{false};

constexpr RECT kBackButton{645, 472, 768, 518};
constexpr RECT kApplyButton{780, 472, 914, 518};
constexpr RECT kResetButton{46, 433, 228, 461};
constexpr RECT kCaptureCancel{696, 340, 776, 376};
constexpr RECT kRevertButton{430, 326, 590, 373};
constexpr RECT kKeepButton{602, 326, 766, 373};

constexpr COLORREF kBackdrop = RGB(15, 12, 20);
constexpr COLORREF kPanel = RGB(25, 20, 32);
constexpr COLORREF kRaised = RGB(37, 30, 47);
constexpr COLORREF kSelected = RGB(59, 34, 60);
constexpr COLORREF kLine = RGB(86, 71, 100);
constexpr COLORREF kWhite = RGB(251, 245, 251);
constexpr COLORREF kMuted = RGB(204, 187, 210);
constexpr COLORREF kAccent = RGB(255, 108, 184);
constexpr COLORREF kInk = RGB(35, 16, 33);

struct Layout {
    explicit Layout(RECT area) {
        const int width = static_cast<int>(area.right - area.left);
        const int height = static_cast<int>(area.bottom - area.top);
        scale = std::max(0.01, std::min({2.5, width / double(kWidth), height / double(kHeight)}));
        x = (width - px(kWidth)) / 2;
        y = (height - px(kHeight)) / 2;
    }

    int px(int value) const { return static_cast<int>(value * scale + 0.5); }
    RECT rect(RECT r) const { return {x + px(r.left), y + px(r.top), x + px(r.right), y + px(r.bottom)}; }

    double scale;
    int x, y;
};

class Paint {
public:
    Paint(HDC dc, Layout layout) : dc_(dc), layout_(layout) {
        constexpr int sizes[] = {43, 19, 16, 18};
        for (std::size_t i = 0; i < std::size(sizes); ++i) {
            const int weight = i == static_cast<std::size_t>(Font::Title) ||
                               i == static_cast<std::size_t>(Font::Button) ? FW_SEMIBOLD : FW_NORMAL;
            fonts_[i] = CreateFontW(-std::max(9, layout.px(sizes[i])), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
        SetBkMode(dc_, TRANSPARENT);
    }

    ~Paint() {
        for (auto font : fonts_)
            if (font) DeleteObject(font);
    }

    void fill(RECT area, COLORREF color) const {
        area = layout_.rect(area);
        SetDCBrushColor(dc_, color);
        FillRect(dc_, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    void outline(RECT area, COLORREF color) const {
        area = layout_.rect(area);
        SetDCBrushColor(dc_, color);
        FrameRect(dc_, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    void text(RECT area, const wchar_t* value, Font font, COLORREF color, UINT alignment = DT_LEFT) const {
        area = layout_.rect(area);
        const auto face = fonts_[static_cast<std::size_t>(font)];
        const auto previous = face ? SelectObject(dc_, face) : nullptr;
        SetTextColor(dc_, color);
        DrawTextW(dc_, value, -1, &area, alignment | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (previous) SelectObject(dc_, previous);
    }

private:
    HDC dc_;
    Layout layout_;
    HFONT fonts_[static_cast<std::size_t>(Font::Count)]{};
};

RECT graphics_row(int row) {
    const int top = 166 + row * 51;
    return {kLeft, top, kRight, top + 47};
}

RECT binding_row(int row) {
    const int left = row < 8 ? kLeft : 489;
    const int top = 188 + (row % 8) * 30;
    return {left, top, left + 425, top + 27};
}

int back_row() { return g.page == Page::Display ? kGraphicsRows : kResetRow + 1; }
int apply_row() { return back_row() + 1; }

bool hit(const Layout& layout, RECT area, POINT point) {
    area = layout.rect(area);
    return PtInRect(&area, point) != FALSE;
}

const wchar_t* button_name(uint64_t button) {
    if (!std::has_single_bit(button) || !(button & kDigitalMask)) return L"Unassigned";
    return (g.dualsense ? kDualSenseButtons : kXboxButtons)[std::countr_zero(button)];
}

std::wstring fps_name(int fps) {
    if (fps == kAutoFps) return L"Auto";
    if (fps == kUnlimitedFps) return L"Unlimited";
    return std::to_wstring(fps) + L" FPS";
}

std::wstring resolution_name(int height) {
    if (!height) return L"Auto";
    return std::to_wstring(height * 16 / 9) + L" \u00d7 " + std::to_wstring(height);
}

std::wstring anti_aliasing_name(int mode) {
    if (mode == 0) return L"Off";
    if (mode == 1) return L"FXAA";
    return L"MSAA " + std::to_wstring(mode) + L"\u00d7";
}

void draw_button(const Paint& p, RECT area, const wchar_t* label, bool selected, bool primary = false) {
    p.fill(area, primary ? kAccent : selected ? kSelected : kRaised);
    if (selected) p.outline(area, primary ? kWhite : kAccent);
    p.text(area, label, Font::Button, primary ? kInk : kWhite, DT_CENTER);
}

void draw_graphics(const Paint& p) {
    constexpr const wchar_t* labels[] = {
        L"Frame-rate limit", L"Display mode", L"Resolution", L"Anti-aliasing", L"FMV interpolation"
    };
    const std::wstring effective_fps = L"Active limit  " + fps_name(g.presentation.effective_fps);
    std::wstring effective_resolution;
    if (g.presentation.output_width && g.presentation.output_height)
        effective_resolution = L"Current  " + std::to_wstring(g.presentation.output_width) +
                                L" \u00d7 " + std::to_wstring(g.presentation.output_height);
    if (g.draft.fullscreen) effective_resolution = L"Borderless fullscreen uses the desktop resolution";
    const bool unsupported_msaa = g.draft.anti_aliasing >= 2 &&
                                  !(g.presentation.msaa_mask & unsigned(g.draft.anti_aliasing));
    const wchar_t* aa_detail = unsupported_msaa ? L"This sample count is unavailable on the current GPU"
                                                : L"Higher quality settings can reduce frame rate";
    const wchar_t* details[] = {effective_fps.c_str(), L"Switch between windowed and borderless fullscreen",
                               effective_resolution.c_str(), aa_detail, L"Experimental Open-SVPFlow. Target depends on system speed."};
    const int movie_rate = g.draft.movie_interpolation;
    const std::wstring movie_value = movie_rate == 0 ? L"Off" : movie_rate == -1 ? L"Match" : fps_name(movie_rate);
    const std::wstring values[] = {fps_name(g.draft.fps), g.draft.fullscreen ? L"Borderless" : L"Windowed",
                                   g.draft.fullscreen ? L"Desktop" : resolution_name(g.draft.window_height),
                                   anti_aliasing_name(g.draft.anti_aliasing), movie_value};
    for (int row = 0; row < kGraphicsRows; ++row) {
        const auto area = graphics_row(row);
        const bool selected = g.row == row;
        p.fill(area, selected ? kSelected : kRaised);
        if (selected) p.outline(area, kAccent);
        p.text({area.left + 16, area.top + 2, area.right - 246, area.top + 26}, labels[row], Font::Button, kWhite);
        p.text({area.left + 16, area.top + 25, area.right - 246, area.bottom - 3}, details[row], Font::Small, kMuted);
        const bool editable = row != 2 || !g.draft.fullscreen;
        if (editable) p.text({662, area.top, 707, area.bottom}, L"\u2039", Font::Title, kAccent, DT_CENTER);
        p.text({706, area.top, 867, area.bottom}, values[row].c_str(), Font::Button, kWhite, DT_CENTER);
        if (editable) p.text({869, area.top, 913, area.bottom}, L"\u203a", Font::Title, kAccent, DT_CENTER);
    }
    if (g.preview_failed)
        p.text({kLeft, 438, kRight, 465}, L"The requested display mode could not be applied.",
               Font::Small, kAccent);
}

void draw_controls(const Paint& p) {
    p.text({kLeft, 155, kRight, 181}, L"Select a game action to change its controller button.",
           Font::Small, kMuted);
    for (int row = 0; row < kBindings; ++row) {
        const auto area = binding_row(row);
        const bool selected = g.row == row;
        p.fill(area, selected ? kSelected : kRaised);
        if (selected) p.outline(area, kAccent);
        p.text({area.left + 11, area.top, area.left + 224, area.bottom}, kActions[row], Font::Small, kWhite);
        p.text({area.left + 226, area.top, area.right - 12, area.bottom}, button_name(g.draft.bindings[row]),
               Font::Small, selected ? kAccent : kMuted, DT_RIGHT);
    }
    draw_button(p, kResetButton, L"Reset bindings", g.row == kResetRow);
    p.text({242, 433, kRight, 461}, g.dualsense ? L"DualSense" : L"Xbox controller", Font::Small, kMuted);
}

void draw_capture(const Paint& p) {
    constexpr RECT card{155, 170, 805, 400};
    p.fill(card, kPanel);
    p.outline(card, kAccent);
    p.text({185, 194, 775, 238}, L"Change controller binding", Font::Title, kWhite);
    p.text({187, 247, 775, 275}, kActions[g.capture], Font::Button, kAccent);
    p.text({187, 279, 775, 310}, g.capture_armed ? L"Press and release one controller button."
                                              : L"Release the controller buttons to begin.",
           Font::Body, kWhite);
    p.text({187, 322, 682, 345}, L"Esc or both bottom and right face buttons cancel.", Font::Small, kMuted);
    if (g.capture_button)
        p.text({187, 347, 682, 376}, button_name(g.capture_button), Font::Button, kAccent);
    draw_button(p, kCaptureCancel, L"Cancel", false);
}

void draw_confirmation(const Paint& p, bool quitting) {
    p.fill({155, 170, 805, 405}, kPanel);
    p.outline({155, 170, 805, 405}, kAccent);
    p.text({185, 189, 775, 250}, quitting ? L"Quit game?" : L"Keep these display settings?",
           Font::Title, kWhite);
    const std::wstring countdown = L"Reverting in " + std::to_wstring(g.preview_seconds) + L" seconds";
    p.text({187, 253, 775, 286}, quitting ? L"Any unsaved progress will be lost." : countdown.c_str(),
           Font::Body, kMuted);
    if (!quitting && g.preview_status == PreviewStatus::Pending)
        p.text({187, 286, 775, 310}, L"Applying the display settings…", Font::Small, kMuted);
    draw_button(p, kRevertButton, quitting ? L"Cancel" : L"Revert", !g.keep_selected);
    if (quitting || g.preview_status == PreviewStatus::Ready)
        draw_button(p, kKeepButton, quitting ? L"Quit" : L"Keep", g.keep_selected, true);
}

void render(HDC dc, RECT area) {
    SetDCBrushColor(dc, kBackdrop);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const Paint p(dc, Layout(area));
    if (g.page == Page::Quit) {
        draw_confirmation(p, true);
        return;
    }
    p.fill({20, 12, 940, 538}, kPanel);
    p.fill({46, 30, 79, 33}, kAccent);
    p.text({91, 17, kRight, 45}, L"CATHERINE FULL BODY", Font::Small, kAccent);
    p.text({kLeft, 41, kRight, 97}, g.page == Page::Display ? L"Display" : L"Controller bindings",
           Font::Title, kWhite);
    p.fill({kLeft, 157, kRight, 158}, kLine);
    if (g.page == Page::Display) draw_graphics(p);
    else draw_controls(p);
    if (g.save_failed)
        p.text({kLeft, 471, 630, 516}, L"Could not save settings. Try Apply again.", Font::Small, kAccent);
    else
        p.text({kLeft, 471, 630, 516}, L"Arrows / D-pad  Navigate     Enter  Select     Esc  Back", Font::Small, kMuted);
    draw_button(p, kBackButton, L"Back", g.row == back_row());
    draw_button(p, kApplyButton, L"Apply", g.row == apply_row(), true);
    if (g.capture >= 0) draw_capture(p);
    if (g.preview_deadline) draw_confirmation(p, false);
}

void redraw() { InvalidateRect(g.window, nullptr, FALSE); }

template<std::size_t N>
int cycle(const int (&options)[N], int value, int step) {
    const auto it = std::find(std::begin(options), std::end(options), value);
    const int index = it == std::end(options) ? 0 : static_cast<int>(it - std::begin(options));
    return options[(index + static_cast<int>(N) + step) % static_cast<int>(N)];
}

void adjust(int row, int step) {
    switch (row) {
        case 0: g.draft.fps = cycle(kFps, g.draft.fps, step); break;
        case 1: g.draft.fullscreen = !g.draft.fullscreen; break;
        case 2:
            if (!g.draft.fullscreen) {
                do {
                    g.draft.window_height = cycle(kResolutions, g.draft.window_height, step);
                } while (g.draft.window_height > g.presentation.max_window_height);
            }
            break;
        case 3: g.draft.anti_aliasing = cycle(kAntiAliasing, g.draft.anti_aliasing, step); break;
        case 4: g.draft.movie_interpolation = cycle(kMovieRates, g.draft.movie_interpolation, step); break;
    }
    redraw();
}

void cancel_capture() {
    g.capture = -1;
    g.capture_button = 0;
    g.capture_armed = false;
    redraw();
}

void commit_capture(uint64_t button) {
    auto& bindings = g.draft.bindings;
    const auto displaced = std::find(bindings.begin(), bindings.end(), button);
    if (displaced != bindings.end()) std::iter_swap(displaced, bindings.begin() + g.capture);
    else bindings[g.capture] = button;
    cancel_capture();
}

void apply_draft() {
    g.save_failed = false;
    g.preview_failed = false;
    switch (g.apply(g.draft)) {
        case ApplyResult::Saved: close(); return;
        case ApplyResult::Preview:
            g.preview_deadline = GetTickCount64() + 15000;
            g.preview_seconds = 15;
            g.preview_status = g.read_preview();
            g.keep_selected = false;
            break;
        case ApplyResult::Failed: g.save_failed = true; break;
        case ApplyResult::DisplayFailed: g.preview_failed = true; break;
    }
    redraw();
}

void confirm(bool keep) {
    if (g.page == Page::Quit) {
        if (keep) g.quit();
        else close();
        return;
    }
    if (keep && g.preview_status != PreviewStatus::Ready) return;
    const bool saved = g.finish_preview(keep);
    if (keep && saved) {
        close();
        return;
    }
    g.preview_deadline = 0;
    g.draft.fullscreen = g.original.fullscreen;
    g.draft.window_height = g.original.window_height;
    g.save_failed = keep && !saved;
    redraw();
}

bool confirming() { return g.page == Page::Quit || g.preview_deadline; }

void back() {
    if (confirming()) confirm(false);
    else if (g.capture >= 0) cancel_capture();
    else close();
}

void activate() {
    if (confirming()) {
        confirm(g.keep_selected);
    } else if (g.row == back_row()) {
        back();
    } else if (g.row == apply_row()) {
        apply_draft();
    } else if (g.page == Page::Display) {
        adjust(g.row, 1);
    } else if (g.row == kResetRow) {
        g.draft.bindings = Values{}.bindings;
        redraw();
    } else {
        g.capture = g.row;
        g.capture_armed = false;
        g.capture_button = 0;
        redraw();
    }
}

void move(Direction direction) {
    if (confirming()) {
        if (direction == Direction::Left || direction == Direction::Right) {
            g.keep_selected = direction == Direction::Right &&
                (g.page == Page::Quit || g.preview_status == PreviewStatus::Ready);
            redraw();
        }
        return;
    }
    const bool horizontal = direction == Direction::Left || direction == Direction::Right;
    const int step = direction == Direction::Right || direction == Direction::Down ? 1 : -1;
    if (horizontal) {
        if (g.row >= back_row()) {
            g.row = g.row == back_row() ? apply_row() : back_row();
        } else if (g.page == Page::Display) {
            adjust(g.row, step);
            return;
        } else if (g.row < kBindings) {
            g.binding_column = direction == Direction::Right ? 1 : 0;
            g.row = (g.row % 8) + 8 * g.binding_column;
        }
    } else if (g.page == Page::Display) {
        g.row = std::clamp(g.row + step, 0, back_row());
    } else if (g.row < kBindings) {
        const int next = (g.row % 8) + step;
        if (next >= 8) g.row = kResetRow;
        else if (next >= 0) g.row += step;
    } else if (step < 0) {
        g.row = g.row == kResetRow ? 7 + 8 * g.binding_column : kResetRow;
    } else {
        g.row = back_row();
    }
    redraw();
}

bool key_down(WPARAM key, bool repeat) {
    if (key == VK_ESCAPE) {
        if (!repeat) back();
        return true;
    }
    if (g.capture >= 0) return true;
    switch (key) {
        case VK_UP: move(Direction::Up); return true;
        case VK_DOWN: move(Direction::Down); return true;
        case VK_LEFT: move(Direction::Left); return true;
        case VK_RIGHT: move(Direction::Right); return true;
        case VK_TAB:
            if (!repeat && confirming()) move(g.keep_selected ? Direction::Left : Direction::Right);
            return true;
        case VK_RETURN:
        case VK_SPACE:
            if (!repeat) {
                if (key == VK_RETURN && !confirming() && (GetKeyState(VK_CONTROL) & 0x8000)) apply_draft();
                else activate();
            }
            return true;
        default: return false;
    }
}

void click(HWND window, POINT point) {
    RECT client{};
    GetClientRect(window, &client);
    const Layout layout(client);
    if (confirming()) {
        if (hit(layout, kRevertButton, point)) confirm(false);
        else if (hit(layout, kKeepButton, point)) confirm(true);
        return;
    }
    if (g.capture >= 0) {
        if (hit(layout, kCaptureCancel, point)) cancel_capture();
        return;
    }
    if (hit(layout, kBackButton, point)) back();
    else if (hit(layout, kApplyButton, point)) apply_draft();
    else if (g.page == Page::Display) {
        for (int row = 0; row < kGraphicsRows; ++row) {
            const auto area = graphics_row(row);
            if (!hit(layout, area, point)) continue;
            g.row = row;
            if (hit(layout, {662, area.top, 708, area.bottom}, point)) adjust(row, -1);
            else if (hit(layout, {868, area.top, 914, area.bottom}, point)) adjust(row, 1);
            else redraw();
            return;
        }
    } else if (hit(layout, kResetButton, point)) {
        g.row = kResetRow;
        activate();
    } else {
        for (int row = 0; row < kBindings; ++row) {
            if (!hit(layout, binding_row(row), point)) continue;
            g.row = row;
            g.binding_column = row / 8;
            activate();
            return;
        }
    }
}

Direction pad_direction(const RawPad& pad) {
    if ((pad.buttons & plat::kUp) || pad.ly > 16000) return Direction::Up;
    if ((pad.buttons & plat::kDown) || pad.ly < -16000) return Direction::Down;
    if ((pad.buttons & plat::kLeft) || pad.lx < -16000) return Direction::Left;
    if ((pad.buttons & plat::kRight) || pad.lx > 16000) return Direction::Right;
    return Direction::None;
}

void handle_pad(const RawPad& pad) {
    const uint64_t buttons = pad.buttons & kDigitalMask;
    const auto direction = pad_direction(pad);
    if (!g.pad_armed) {
        g.pad_armed = !buttons && direction == Direction::None;
        return;
    }
    const uint64_t pressed = buttons & ~g.previous_buttons;
    g.previous_buttons = buttons;
    if (g.capture >= 0) {
        if (!g.capture_armed) {
            if (!buttons) {
                g.capture_armed = true;
                redraw();
            }
        } else if ((buttons & kCancelChord) == kCancelChord) {
            cancel_capture();
        } else if (!buttons && g.capture_button) {
            commit_capture(g.capture_button);
        } else if (std::has_single_bit(buttons) && !g.capture_button) {
            g.capture_button = buttons;
            redraw();
        } else if (buttons && buttons != g.capture_button) {
            g.capture_armed = false;
            g.capture_button = 0;
            redraw();
        }
        return;
    }
    if (pressed & g.original.bindings[1]) {
        back();
        return;
    }
    if (pressed & g.original.bindings[0]) {
        activate();
        return;
    }
    const auto now = GetTickCount64();
    if (direction != Direction::None && (direction != g.previous_direction || now >= g.next_repeat)) {
        move(direction);
        g.next_repeat = now + (direction == g.previous_direction ? 105 : 340);
    }
    g.previous_direction = direction;
}

LRESULT CALLBACK wndproc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const auto dc = BeginPaint(window, &paint);
            RECT area{};
            GetClientRect(window, &area);
            auto memory = CreateCompatibleDC(dc);
            const auto surface = memory ? CreateCompatibleBitmap(dc, area.right, area.bottom) : nullptr;
            if (surface) {
                const auto previous = SelectObject(memory, surface);
                render(memory, area);
                BitBlt(dc, 0, 0, area.right, area.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, previous);
                DeleteObject(surface);
            } else {
                render(dc, area);
            }
            if (memory) DeleteDC(memory);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_PRINTCLIENT: {
            RECT area{};
            GetClientRect(window, &area);
            render(reinterpret_cast<HDC>(w), area);
            return 0;
        }
        case WM_KEYDOWN:
            if (key_down(w, (l & (1ll << 30)) != 0)) return 0;
            break;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            click(window, {GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            return 0;
        case WM_TIMER:
            if (w != kPadTimer) break;
            if (g.preview_deadline) {
                const auto now = GetTickCount64();
                const auto status = g.read_preview();
                if (status == PreviewStatus::Failed || now >= g.preview_deadline) {
                    confirm(false);
                    g.preview_failed = status == PreviewStatus::Failed;
                } else {
                    const auto seconds = unsigned((g.preview_deadline - now + 999) / 1000);
                    if (status != g.preview_status || seconds != g.preview_seconds) {
                        g.preview_status = status;
                        g.preview_seconds = seconds;
                        redraw();
                    }
                }
            }
            if (g.page == Page::Controls && g.dualsense != plat::dualsense_connected()) {
                g.dualsense = plat::dualsense_connected();
                redraw();
            }
            if (GetForegroundWindow() == g.owner) {
                if (g.read_pad) handle_pad(g.read_pad());
            } else {
                g.pad_armed = false;
                g.previous_buttons = 0;
                g.previous_direction = Direction::None;
                if (g.capture >= 0 && (g.capture_armed || g.capture_button)) {
                    g.capture_armed = false;
                    g.capture_button = 0;
                    redraw();
                }
            }
            return 0;
        case WM_NCDESTROY: {
            if (g.window == window) {
                KillTimer(window, kPadTimer);
                g = {};
                g_open.store(false, std::memory_order_release);
            }
            break;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

}  // namespace

bool open(HWND owner, Page page, const Values& values, Presentation presentation, Apply apply,
          FinishPreview finish_preview, ReadPreview read_preview, ReadRawPad read_pad, Quit quit) {
    if (!owner || g.window) return false;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW klass{};
    klass.lpfnWndProc = wndproc;
    klass.hInstance = instance;
    klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    klass.lpszClassName = kClass;
    if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    g.owner = owner;
    g.page = page;
    g.original = g.draft = values;
    g.presentation = presentation;
    g.apply = apply;
    g.finish_preview = finish_preview;
    g.read_preview = read_preview;
    g.read_pad = read_pad;
    g.quit = quit;
    g.dualsense = plat::dualsense_connected();
    g.window = CreateWindowExW(0, kClass, L"Catherine Settings", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                              owner, nullptr, instance, nullptr);
    if (!g.window) {
        g = {};
        return false;
    }
    if (!SetTimer(g.window, kPadTimer, 16, nullptr)) {
        DestroyWindow(g.window);
        return false;
    }
    g_open.store(true, std::memory_order_release);
    layout(owner);
    SetFocus(g.window);
    return true;
}

bool is_open() { return g_open.load(std::memory_order_acquire); }

void layout(HWND owner) {
    if (!g.window || owner != g.owner) return;
    if (IsIconic(owner)) {
        ShowWindow(g.window, SW_HIDE);
        return;
    }
    RECT area{};
    if (!GetClientRect(owner, &area)) return;
    g.presentation.output_width = uint32_t(area.right);
    g.presentation.output_height = uint32_t(area.bottom);
    const bool quitting = g.page == Page::Quit;
    const int width = area.right * (quitting ? 85 : 76) / 100;
    const int height = area.bottom * 88 / 100;
    SetWindowPos(g.window, HWND_TOP, quitting ? (area.right - width) / 2 : area.right - width - area.right / 50,
                 (area.bottom - height) / 2, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    redraw();
}

void close() {
    if (!g.window) return;
    const auto owner = g.owner;
    DestroyWindow(g.window);
    if (IsWindow(owner)) SetFocus(owner);
}

}  // namespace nx::settings_ui
