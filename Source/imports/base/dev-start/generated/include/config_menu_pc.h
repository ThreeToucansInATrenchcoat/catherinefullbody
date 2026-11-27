#pragma once

#include <uchar.h>
#include "config_pc_host.h"
#include "menu_mouse_pc.h"
#include "config_bindings_ui.h"
#include "pc_dialogs.h"
#include "pc_dropdown.h"

extern void f_700009c1a0(Cpu *c);
extern void f_70002abad0(Cpu *c);
extern void f_7000277ff0(Cpu *c);
extern void f_700008fd00(Cpu *c);
extern void f_7000034720(Cpu *c);
extern void f_7000034100(Cpu *c);
extern void f_7000031d70(Cpu *c);
extern void f_7000072920(Cpu *c);
extern void f_70000729d0(Cpu *c);
extern void f_7000072a70(Cpu *c);
extern void f_7000072b10(Cpu *c);

unsigned catherine_pc_display_dialog;
unsigned catherine_pc_question;

// Use the host preview's monotonic Windows clock, independent of rendered frames.
#include "../../../../../platform/host_clock.h"
static uint64_t catherine_pc_display_deadline_ms;

static inline void pc_display_countdown_begin(void) {
    catherine_pc_display_deadline_ms = catherine_host_milliseconds() + 15000;
}

unsigned catherine_config_display_countdown_seconds(void) {
    const uint64_t now = catherine_host_milliseconds();
    if (now >= catherine_pc_display_deadline_ms) return 0;
    const uint64_t seconds = (catherine_pc_display_deadline_ms - now + 999) / 1000;
    return seconds > 15 ? 15 : (unsigned)seconds;
}

const uint16_t *catherine_config_display_countdown_text(void) {
    static uint16_t text[64];
    static const char prefix[] = "Display settings revert in ";
    static const char suffix[] = " seconds.";
    const unsigned seconds = catherine_config_display_countdown_seconds();
    unsigned n = 0;
    for (unsigned i = 0; prefix[i]; ++i) text[n++] = (unsigned char)prefix[i];
    if (seconds >= 10) text[n++] = '0' + seconds / 10;
    text[n++] = '0' + seconds % 10;
    for (unsigned i = 0; suffix[i]; ++i) text[n++] = (unsigned char)suffix[i];
    text[n] = 0;
    return text;
}

enum {
    CATHERINE_CONFIG_GAME = 0,
    CATHERINE_CONFIG_AUDIO = 4,
    CATHERINE_CONFIG_DISPLAY = 1,
    CATHERINE_CONFIG_CONTROL = 2,
    CATHERINE_CONFIG_ACTIVE = 4,
    CATHERINE_CONFIG_PROMPT = 5,
    CATHERINE_CONFIG_UP = 1 << 4,
    CATHERINE_CONFIG_RIGHT = 1 << 5,
    CATHERINE_CONFIG_DOWN = 1 << 6,
    CATHERINE_CONFIG_LEFT = 1 << 7,
    CATHERINE_CONFIG_TAB = (1 << 10) | (1 << 11),
    CATHERINE_CONFIG_CONFIRM = 1 << 13,
    CATHERINE_CONFIG_OTHER_ACTION = (1 << 14) | (1 << 15)
};

enum CatherineConfigPcInput {
    CATHERINE_CONFIG_INPUT_NATIVE,
    CATHERINE_CONFIG_INPUT_HANDLED,
    CATHERINE_CONFIG_INPUT_MENU_ONLY,
    CATHERINE_CONFIG_INPUT_SAVE
};

enum { PC_VISIBLE_DISPLAY = PC_DISPLAY_ROWS, PC_VISIBLE_BINDINGS = 1, PC_TEXT_SLOTS = 80 };

static struct {
    uint64_t handle;
    char16_t text[64];
} pc_text[PC_TEXT_SLOTS];

static struct {
    uint64_t menu;
    unsigned previous_tab, selected_tab, row, scroll;
    unsigned capturing, armed, apply_held, save_after_apply, mouse_move, mouse_click;
    unsigned control_player, device_open, device_row, device_scroll, device_wheel;
    const char *notice;
} catherine_config_pc;

int catherine_config_pc_is_open(void) {
    return catherine_config_pc.menu != 0;
}

void catherine_config_pc_question_pointer_consumed(void) {
    if (!catherine_config_pc.menu) return;
    // Native modal state bypasses Config input. Consume its pointer serials here
    // so dismissing No cannot edit the row under the closing question.
    catherine_config_pc.mouse_move = catherine_pc_mouse(2);
    catherine_config_pc.mouse_click = catherine_pc_mouse(3);
    catherine_config_pc.capturing = 2;
}

static struct {
    unsigned open, capturing, armed, dialog, wait_release, save_applying, apply_held;
    unsigned mouse_move, mouse_click, wheel_held, dragging, ignore_footer_mouse, back_mouse_held;
    float wheel_wait;
    CatherineBindingFocus focus;
    const char *notice;
} catherine_bindings_ui;

static inline unsigned catherine_config_pc_tab(uint64_t menu) {
    return LD32(menu + 8);
}

// The original Subtitles descriptor is visible in modes 0..5, hidden in 6.
static inline int catherine_config_pc_subtitles_visible(uint64_t menu) {
    return LD32(menu + 4) < 6;
}

static inline unsigned catherine_config_pc_added_rows(uint64_t menu, unsigned tab) {
    // Native mode6 is first-boot/restricted Brightness, not a full Config page.
    if (LD32(menu + 4) >= 6) return 0;
    if (tab == CATHERINE_CONFIG_GAME) return 1;
    if (tab == CATHERINE_CONFIG_DISPLAY) return PC_DISPLAY_ROWS;
    if (tab == CATHERINE_CONFIG_AUDIO) return catherine_config_pc_subtitles_visible(menu);
    return tab == CATHERINE_CONFIG_CONTROL && LD32(menu + 4) < 6 ? 2 : 0;
}

static inline int catherine_config_bindings_visible(uint64_t menu) {
    return catherine_bindings_ui.open && catherine_config_pc.menu == menu &&
           catherine_config_pc_tab(menu) == CATHERINE_CONFIG_CONTROL;
}

// Native Config header positions on the 1920x1080 canvas.
static inline unsigned pc_config_arrow_at(int x, int y) {
    if (y < 58 || y >= 148) return 0;
    if (x >= 385 && x < 475) return 1u << 10;
    if (x >= 740 && x < 830) return 1u << 11;
    return 0;
}

static unsigned pc_config_pending_tabs, pc_config_previous_tab_key, pc_config_tab_key_held;

static inline uint32_t catherine_config_bindings_chrome_color(uint64_t menu, unsigned sprite, uint32_t rgba) {
    const unsigned direction = (sprite == 0xc || sprite == 0x10) ? 1u << 10 :
                               (sprite == 0xd || sprite == 0x11) ? 1u << 11 : 0;
    if (!direction) return rgba;
    if (catherine_config_bindings_visible(menu)) return 0x68686800u | (rgba & 0xffu);
    if (LD32(menu + 0xc) == CATHERINE_CONFIG_ACTIVE && !catherine_bindings_ui.dialog &&
        !catherine_bindings_ui.save_applying && !catherine_pc_display_dialog && catherine_pc_mouse(4) &&
        pc_config_arrow_at(catherine_pc_mouse(0), catherine_pc_mouse(1)) == direction)
        return ((sprite == 0xc || sprite == 0xd) ? 0xff82c400u : 0xffc9e500u) | (rgba & 0xffu);
    return rgba;
}

static inline uint32_t catherine_config_pc_tab_intent(uint64_t pad) {
    uint32_t buttons = LD32(pad + 4);
    const unsigned key = catherine_config_setting(PC_RAW_KEY, 0);
    const unsigned preset = catherine_config_setting(PC_MOVEMENT_PRESET, 0);
    const unsigned previous = preset == 1 ? 'W' : 'Q', next = preset == 1 ? 'R' : 'E';
    const int keyboard = catherine_config_setting(PC_INPUT_LAYOUT, 0) == 0;
    const int left = keyboard && key && (key == previous || key == (unsigned)catherine_config_setting(PC_KEY_BINDING + 8, 0));
    const int right = keyboard && key && (key == next || key == (unsigned)catherine_config_setting(PC_KEY_BINDING + 7, 0));
    pc_config_tab_key_held = left || right;
    if (key && key != pc_config_previous_tab_key && pc_config_tab_key_held)
        pc_config_pending_tabs |= left ? 1u << 10 : 1u << 11;
    pc_config_previous_tab_key = key;
    return buttons | pc_config_pending_tabs;
}

static inline uint32_t catherine_config_pc_tab_buttons(uint64_t pad) {
    uint32_t buttons = LD32(pad + 4);
    if (catherine_bindings_ui.open || catherine_bindings_ui.dialog ||
        catherine_bindings_ui.save_applying || catherine_pc_display_dialog) {
        pc_config_pending_tabs = 0;
        return buttons & ~CATHERINE_CONFIG_TAB;
    }
    buttons |= pc_config_pending_tabs;
    pc_config_pending_tabs = 0;
    return buttons;
}

static inline int catherine_config_display_entering(uint64_t menu) {
    const unsigned state = LD32(menu + 0xc);
    return LD32(menu + 4) < 6 && catherine_config_pc_tab(menu) == CATHERINE_CONFIG_DISPLAY &&
           (state < CATHERINE_CONFIG_ACTIVE || (state >= 14 && state <= 17));
}

static inline int catherine_config_pc_page_visible(uint64_t menu) {
    const unsigned state = LD32(menu + 0xc);
    return state == CATHERINE_CONFIG_ACTIVE || catherine_config_display_entering(menu) ||
           state == CATHERINE_CONFIG_PROMPT;
}

static inline uint32_t catherine_config_page_alpha(uint64_t menu) {
    // Use the Brightness row's opacity to reveal the entire Display page together.
    return LD8(menu + (catherine_config_display_entering(menu) ? 0x59 : 0x3c));
}

static unsigned pc_config_arrow_key, pc_config_arrow_repeat;
static float pc_config_arrow_wait;

static inline void pc_config_update_arrow_keys(void) {
    const unsigned raw = catherine_config_setting(PC_INPUT_LAYOUT, 0) == 0 ?
        catherine_config_setting(PC_RAW_KEY, 0) : 0;
    const unsigned key = raw == 0x25 || raw == 0x27 ? raw : 0;
    pc_config_arrow_repeat = 0;
    if (!key) { pc_config_arrow_key = 0; pc_config_arrow_wait = 0; return; }
    if (key != pc_config_arrow_key) {
        pc_config_arrow_key = key;
        pc_config_arrow_wait = .30f;
        pc_config_arrow_repeat = key == 0x25 ? CATHERINE_CONFIG_LEFT : CATHERINE_CONFIG_RIGHT;
    } else {
        const uint32_t bits = LD32(0x70031cc1e8ull);
        float delta; memcpy(&delta, &bits, 4);
        if (!(delta > 0 && delta < .1f)) delta = 1.0f / 30.0f;
        pc_config_arrow_wait -= delta;
        if (pc_config_arrow_wait <= 0) {
            pc_config_arrow_wait += .12f;
            pc_config_arrow_repeat = key == 0x25 ? CATHERINE_CONFIG_LEFT : CATHERINE_CONFIG_RIGHT;
        }
    }
}

static inline uint32_t catherine_config_pc_repeat(uint64_t menu, uint64_t pad) {
    const uint32_t repeat = LD32(pad + 0x18);
    if (pc_config_arrow_key)
        return (repeat & ~(CATHERINE_CONFIG_LEFT | CATHERINE_CONFIG_RIGHT)) | pc_config_arrow_repeat;
    if (catherine_config_pc_tab(menu) != CATHERINE_CONFIG_DISPLAY && !catherine_bindings_ui.open) return repeat;

    const unsigned vertical = CATHERINE_CONFIG_UP | CATHERINE_CONFIG_DOWN;
    const unsigned horizontal = CATHERINE_CONFIG_LEFT | CATHERINE_CONFIG_RIGHT;
    const unsigned directions = vertical | horizontal;
    const uint32_t dpad_held = LD32(pad) & directions;
    if (dpad_held) {
        const unsigned axis = (dpad_held & vertical) ? vertical : horizontal;
        if (catherine_bindings_ui.open) return repeat & (~directions | axis);
        return (repeat & ~directions) | (LD32(pad + 8) & axis);
    }

    // The guest stick ranges from -128 to 127. Require a decisive horizontal tilt
    // for changing values so a slightly angled up/down gesture stays vertical.
    const int x = (int32_t)LD32(pad + 0x20);
    const int y = (int32_t)LD32(pad + 0x24);
    const int abs_x = x < 0 ? -x : x;
    const int abs_y = y < 0 ? -y : y;
    unsigned direction = 0;
    if (abs_x >= 56 && abs_x >= abs_y + 20)
        direction = x < 0 ? CATHERINE_CONFIG_LEFT : CATHERINE_CONFIG_RIGHT;
    else if (abs_y >= 33 && abs_y >= abs_x + 12)
        direction = y < 0 ? CATHERINE_CONFIG_UP : CATHERINE_CONFIG_DOWN;
    return repeat & (~directions | direction);
}

static inline unsigned catherine_config_pc_row_count(uint64_t menu, unsigned tab) {
    const unsigned mode = LD32(menu + 4);
    if (mode > 6) return 0;
    unsigned first, end;
    switch (tab) {
    case CATHERINE_CONFIG_GAME: first = 0xc; end = 2; break;
    case CATHERINE_CONFIG_DISPLAY: first = 0x34; end = 2; break;
    case CATHERINE_CONFIG_CONTROL: first = 0x5c; end = 3; break;
    case 3: first = 0x98; end = 5; break;
    case CATHERINE_CONFIG_AUDIO: first = 0xfc; end = 1; break;
    default: return 0;
    }
    const uint64_t descriptor = 0x70007a0448ull + mode + first;
    unsigned rows = 0;
    for (unsigned index = 0; index < end; ++index) {
        rows += LD8(descriptor + index * 20) != 2;
    }
    return rows;
}

static inline int catherine_config_pc_selected(uint64_t menu) {
    return LD32(menu + 4) < 6 && catherine_config_pc.menu == menu && catherine_config_pc.selected_tab == catherine_config_pc_tab(menu) + 1 &&
           catherine_config_pc_page_visible(menu);
}

static inline uint32_t catherine_config_row_highlight_alpha(uint64_t menu) {
    // Player selectors and their binding links use text colour and box outlines.
    // Only suppress the native selection brush; retain the page's own opacity.
    if (catherine_config_pc_tab(menu) == CATHERINE_CONFIG_CONTROL && catherine_config_pc_selected(menu)) return 0;
    return catherine_config_page_alpha(menu);
}

static inline float pc_list_top(uint64_t menu) {
    if (catherine_config_pc_tab(menu) == CATHERINE_CONFIG_DISPLAY) return 211.5f;
    return 211.5f + 140.0f * catherine_config_pc_row_count(menu, catherine_config_pc_tab(menu));
}

static inline float catherine_config_pc_y(uint64_t menu) {
    if (catherine_config_pc_tab(menu) == CATHERINE_CONFIG_AUDIO) return 115.0f;
    if (catherine_config_pc_tab(menu) == CATHERINE_CONFIG_CONTROL)
        return (681.5f - 24.0f) / 1.5f + 62.0f * catherine_config_pc.row;
    return (pc_list_top(menu) - 24.0f) / 1.5f +
           62.0f * (catherine_config_pc.row - catherine_config_pc.scroll);
}

static inline float catherine_config_pc_x(uint64_t menu) {
    return catherine_config_pc_tab(menu) == CATHERINE_CONFIG_CONTROL && catherine_config_pc.control_player ?
        160.0f + (1030.0f - 322.0f) / 1.5f : 160.0f;
}

static inline uint32_t catherine_config_pc_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static inline float catherine_config_pc_float(uint64_t address) {
    const uint32_t bits = LD32(address);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

// Footer action IDs retain the child navigation order: Return, Confirm, Defaults.
static inline unsigned pc_config_footer_at(int x, int y) {
    if (y < 960 || y >= 1050) return 0;
    if (x >= 1170 && x < 1465) return 3;
    if (x >= 1480 && x < 1645) return 1;
    if (x >= 1660 && x < 1870) return 2;
    return 0;
}

static inline int pc_config_footer_enabled(uint64_t menu) {
    return catherine_config_pc.menu == menu && LD32(menu + 0xc) == CATHERINE_CONFIG_ACTIVE &&
        !catherine_bindings_ui.capturing && !catherine_bindings_ui.dialog &&
        !catherine_bindings_ui.save_applying && !catherine_pc_display_dialog &&
        !catherine_config_setting(PC_PREVIEW, 0);
}

static inline uint32_t catherine_config_pc_footer_text_color(uint64_t menu, unsigned action, uint32_t rgba) {
    if (pc_config_footer_enabled(menu) && catherine_pc_mouse(4) &&
        pc_config_footer_at(catherine_pc_mouse(0), catherine_pc_mouse(1)) == action)
        return 0xff369800u | (rgba & 0xffu);
    return rgba;
}

static struct { uint64_t menu; float first_x, y; } pc_brightness;

static inline void catherine_config_pc_brightness_geometry(uint64_t menu, const Cpu *draw, unsigned index) {
    uint32_t xb = (uint32_t)draw->v[0], yb = (uint32_t)draw->v[1];
    float x, y;
    memcpy(&x, &xb, 4); memcpy(&y, &yb, 4);
    pc_brightness.menu = menu;
    // The native selection sprite is 44 source pixels square; its centre is
    // 33 canvas pixels from the draw origin used at LR33750.
    pc_brightness.first_x = x + 33.0f - 24.0f * index;
    pc_brightness.y = y + 33.0f;
}

static inline int pc_config_brightness_click(Cpu *c, uint64_t menu, int x, int y) {
    if (pc_brightness.menu != menu || catherine_config_pc_tab(menu) != CATHERINE_CONFIG_DISPLAY ||
        y < pc_brightness.y - 25 || y >= pc_brightness.y + 25 ||
        x < pc_brightness.first_x - 12 || x >= pc_brightness.first_x + 492) return 0;
    const unsigned target = (unsigned)((x - pc_brightness.first_x + 12) / 24);
    Cpu adjust = *c;
    adjust.x[1] = 3;
    f_7000034100(&adjust);
    const int delta = (int)target - (int)adjust.x[0];
    if (delta) {
        adjust = *c; adjust.x[0] = menu; adjust.x[1] = 3; adjust.x[2] = (unsigned char)delta;
        f_7000034720(&adjust);
        adjust = *c; adjust.x[0] = 0xbba; f_7000277ff0(&adjust);
    }
    return 1;
}

static inline void pc_destroy_text(Cpu *c, unsigned slot) {
    if (!pc_text[slot].handle) return;
    Cpu draw = *c;
    f_700009c1a0(&draw);
    const uint64_t manager = LD64(draw.x[0] + 0x40);
    draw.x[0] = manager;
    draw.x[1] = (uint64_t)(uintptr_t)&pc_text[slot].handle;
    draw.pc = LD64(LD64(manager) + 0x30);
    rt_dispatch(&draw);
    pc_text[slot].handle = 0;
}

static inline void catherine_config_pc_release(Cpu *c, uint64_t menu) {
    if (!menu || catherine_config_pc.menu != menu) return;
    if (catherine_bindings_ui.open || catherine_bindings_ui.dialog || catherine_bindings_ui.save_applying) {
        if (catherine_bindings_ui.open) catherine_config_setting(PC_BINDINGS_DISCARD, 1);
        if (catherine_bindings_ui.dialog) {
            Cpu dialog = *c;
            f_70000729d0(&dialog);
            catherine_pc_question = CATHERINE_PC_QUESTION_NONE;
            catherine_pc_display_dialog = 0;
        }
        memset(&catherine_bindings_ui, 0, sizeof catherine_bindings_ui);
    }
    catherine_config_setting(PC_DISCARD_DRAFT, 1);
    if (catherine_config_setting(PC_PREVIEW, 0)) catherine_config_setting(PC_REVERT, 1);
    if (catherine_pc_display_dialog) {
        Cpu dialog = *c;
        f_70000729d0(&dialog);
        catherine_pc_display_dialog = 0;
    }
    catherine_pc_display_deadline_ms = 0;
    pc_brightness.menu = 0;
    pc_config_arrow_key = pc_config_arrow_repeat = 0;
    pc_config_arrow_wait = 0;
    catherine_menu_cursor_set(8, 0);
    catherine_config_setting(PC_CONFIG_END, 1);
    for (unsigned slot = 0; slot < PC_TEXT_SLOTS; ++slot) pc_destroy_text(c, slot);
    memset(&catherine_config_pc, 0, sizeof catherine_config_pc);
}

static inline void catherine_config_pc_prepare(Cpu *c, uint64_t menu) {
    if (catherine_config_pc.menu == menu) return;
    catherine_config_pc_release(c, catherine_config_pc.menu);
    catherine_config_setting(PC_CONFIG_BEGIN, 1);
    catherine_config_setting(PC_BINDING_PLAYER, 1);
    catherine_menu_cursor_set(8, 1);
    catherine_config_setting(PC_CURSOR_HIDDEN, 0);
    catherine_config_pc.menu = menu;
    catherine_config_pc.previous_tab = (unsigned)-1;
    catherine_config_pc.mouse_move = catherine_pc_mouse(2);
    catherine_config_pc.mouse_click = catherine_pc_mouse(3);
}

static inline void catherine_config_pc_sync_tab(uint64_t menu) {
    const unsigned tab = catherine_config_pc_tab(menu);
    if (catherine_config_pc.menu == menu && catherine_config_pc.previous_tab != tab) {
        catherine_config_pc.previous_tab = tab;
        catherine_config_pc.selected_tab =
            ((tab == CATHERINE_CONFIG_DISPLAY || tab == CATHERINE_CONFIG_AUDIO) &&
             catherine_config_pc_added_rows(menu, tab)) ? tab + 1 : 0;
        catherine_config_pc.row = catherine_config_pc.scroll = 0;
    }
}

static inline unsigned pc_control_rows(void) { return 2; }

static inline int catherine_config_pc_save(uint64_t menu) {
    const uint64_t game = LD64(LD64(0x7000cc9f60ull));
    const unsigned mode = LD32(menu + 4);
    const int campaign_editable = mode < 7 && LD8(0x70007a0448ull + 12 + mode) == 0;
    if (catherine_save_native_config((const unsigned char*)(uintptr_t)game, campaign_editable)) return 1;
    catherine_config_pc.notice = "Could not save PC settings. Please try again.";
    if (catherine_bindings_ui.open) catherine_bindings_ui.notice = catherine_config_pc.notice;
    return 0;
}

static inline int pc_binding_buttons_held(uint64_t pad) {
    return LD32(pad) || LD32(pad + 4) || catherine_config_setting(PC_RAW_KEY, 0) ||
           catherine_config_setting(PC_RAW_PAD, 0);
}

static inline void pc_bindings_open(Cpu *c) {
    catherine_config_setting(PC_BINDING_PLAYER, (int)catherine_config_pc.control_player + 1);
    memset(&catherine_bindings_ui, 0, sizeof catherine_bindings_ui);
    if (!catherine_config_setting(PC_BINDINGS_BEGIN, 1)) return;
    catherine_bindings_ui.open = 1;
    catherine_bindings_ui.wait_release = 1;
    catherine_config_pc.capturing = 2;
    catherine_bindings_ui.focus.column = catherine_config_setting(PC_INPUT_LAYOUT, 0) != 0;
    catherine_bindings_ui.mouse_move = catherine_pc_mouse(2);
    catherine_bindings_ui.mouse_click = catherine_pc_mouse(3);
    catherine_bindings_ui.apply_held = catherine_config_setting(PC_APPLY_BUTTON, 0);
    // Both native Config page directions use this cue (f320a0).
    Cpu sound = *c;
    sound.x[0] = 0xbd3;
    f_7000277ff0(&sound);
}

static inline void pc_bindings_close(void) {
    catherine_config_setting(PC_BINDINGS_DISCARD, 1);
    // Menu controls always use P1; the column focus retains which editor to reopen.
    catherine_config_setting(PC_BINDING_PLAYER, 1);
    memset(&catherine_bindings_ui, 0, sizeof catherine_bindings_ui);
    // The button that dismissed the child page must not activate the parent.
    catherine_config_pc.capturing = 2;
    catherine_config_pc.notice = 0;
}

static inline void pc_bindings_defaults(void) {
    const unsigned preset = catherine_config_setting(PC_MOVEMENT_PRESET, 0);
    const int movement = !preset || catherine_config_setting(PC_MOVEMENT_PRESET, preset == 1 ? -1 : 1);
    const int keyboard = catherine_config_setting(PC_RESET_KEYBOARD, 1);
    // RESET_BINDINGS also resets Swap Sticks to Off in the binding draft.
    const int controller = catherine_config_setting(PC_RESET_BINDINGS, 1);
    catherine_bindings_ui.notice = movement && keyboard && controller
        ? 0 : "Could not reset all controls. Please try again.";
    catherine_bindings_ui.wait_release = 1;
}

// The child confirms its input draft independently; parent Display/Config drafts
// remain pending for the parent page's Apply/Confirm transaction.
static inline void pc_bindings_confirm(void) {
    if (!catherine_config_setting(PC_BINDINGS_SAVE, 1)) {
        catherine_bindings_ui.notice = "Could not save bindings. Please try again.";
        catherine_bindings_ui.wait_release = 1;
        return;
    }
    pc_bindings_close();
}

static inline void pc_bindings_question(Cpu *c, unsigned question) {
    Cpu dialog = *c;
    catherine_bindings_ui.dialog = question;
    catherine_pc_question = question;
    dialog.x[0] = 0x25;
    f_7000072920(&dialog);
}

static inline void pc_bindings_cancel_display_save(void) {
    catherine_pc_display_deadline_ms = 0;
    if (catherine_config_setting(PC_PREVIEW, 0)) catherine_config_setting(PC_REVERT, 1);
    catherine_config_setting(PC_DISCARD_DRAFT, 1);
    catherine_bindings_ui.save_applying = 0;
    catherine_bindings_ui.wait_release = 1;
    catherine_config_pc.capturing = 2;
    catherine_bindings_ui.notice = "Display changes reverted. Other changes are still unsaved.";
    catherine_config_pc.notice = catherine_bindings_ui.notice;
    catherine_config_pc.save_after_apply = 0;
}

static inline void pc_bindings_begin_save(Cpu *c) {
    catherine_bindings_ui.notice = 0;
    catherine_config_pc.notice = 0;
    if (catherine_config_setting(PC_APPLY, 0)) {
        if (!catherine_config_setting(PC_APPLY, 1)) {
            catherine_bindings_ui.notice = "These display settings could not be applied.";
            catherine_bindings_ui.wait_release = 1;
            catherine_config_pc.capturing = 2;
            catherine_config_pc.notice = catherine_bindings_ui.notice;
            return;
        }
        pc_display_countdown_begin();
        // Keep the current page visible while the renderer applies the mode.
        // A preview that expires/fails before readiness must cancel this save,
        // rather than silently falling through to a non-Display save question.
        catherine_bindings_ui.save_applying = 1;
        catherine_bindings_ui.notice = "Applying display changes...";
        catherine_config_pc.notice = catherine_bindings_ui.notice;
        return;
    }
    pc_bindings_question(c, CATHERINE_PC_QUESTION_SAVE_CONFIG);
}

static inline enum CatherineConfigPcInput pc_bindings_commit_save(void) {
    catherine_pc_display_deadline_ms = 0;
    // Display Keep must finish first: its proposal still contains the old map.
    // Publish the binding draft only after Yes, then reuse native Config save I/O.
    catherine_bindings_ui.wait_release = 1;
    catherine_config_pc.capturing = 2;
    if (catherine_config_setting(PC_BINDINGS_ACTIVE, 0) && !catherine_config_setting(PC_BINDINGS_SAVE, 1)) {
        catherine_bindings_ui.notice = "Could not save bindings. Please try again.";
        catherine_config_pc.notice = catherine_bindings_ui.notice;
        catherine_bindings_ui.wait_release = 1;
        catherine_config_pc.capturing = 2;
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }
    catherine_bindings_ui.save_applying = 0;
    catherine_config_pc.save_after_apply = 0;
    return CATHERINE_CONFIG_INPUT_SAVE;
}

enum CatherineConfigPointerInput {
    PC_CONFIG_POINTER_NONE, PC_CONFIG_POINTER_HANDLED,
    PC_CONFIG_POINTER_CONFIRM, PC_CONFIG_POINTER_RETURN, PC_CONFIG_POINTER_DEFAULTS
};

static inline void pc_config_save_question(Cpu *c, uint64_t menu) {
    // f30a20's normal Confirm branch: retain the native question/result/save I/O.
    Cpu dialog = *c;
    dialog.x[0] = 0xbb9;
    f_7000277ff0(&dialog);
    dialog = *c;
    dialog.x[0] = LD32(menu + 4) == 6 ? 0x28 : 0x24;
    f_7000072920(&dialog);
    ST32(menu + 0x14, 0);
    ST32(menu + 0xc, CATHERINE_CONFIG_PROMPT);
}

static inline void pc_config_footer_question(Cpu *c, uint64_t menu, unsigned action) {
    if (LD32(menu + 4) == 6) return; // The native restricted Config has only Confirm.
    Cpu dialog = *c;
    dialog.x[0] = action == 3 ? 0xbce : 0xbbb;
    f_7000277ff0(&dialog);
    dialog = *c; dialog.x[0] = action == 3 ? 0x27 : 0x25;
    f_7000072920(&dialog);
    ST32(menu + 0x14, action == 3 ? 2 : 1);
    ST32(menu + 0xc, CATHERINE_CONFIG_PROMPT);
}

static inline unsigned pc_control_device_count(unsigned player) {
    unsigned controllers = catherine_config_setting(PC_INPUT_DEVICE_COUNT, 0);
    if (controllers > 30) controllers = 30;
    // Keyboard/mouse is the sole device without a connected controller. P1
    // retains it; P2 must not offer that same device as a usable second input.
    if (player && !controllers) return 1;
    return controllers + 2;
}

static inline CatherineDevicePopup pc_control_device_popup(uint64_t menu, unsigned player) {
    CatherineDevicePopup popup;
    popup.left = player ? 1010.0f : 302.0f;
    popup.top = 728.0f + catherine_config_pc_float(menu + 0xe0) * 1.5f;
    popup.width = 650.0f;
    popup.count = pc_control_device_count(player);
    const float space = (920.0f - popup.top - 20.0f) / 55.0f;
    unsigned rows = space > 1.0f ? (unsigned)space : 1;
    if (rows > 6) rows = 6;
    popup.visible = popup.count < rows ? popup.count : rows;
    return popup;
}

static inline void pc_control_device_open(unsigned player) {
    catherine_config_pc.control_player = player;
    catherine_config_pc.device_open = player + 1;
    catherine_config_pc.device_row = catherine_config_setting(player ? PC_PLAYER2_DEVICE : PC_PLAYER1_DEVICE, 0);
    const unsigned count = pc_control_device_count(player);
    if (catherine_config_pc.device_row >= count) catherine_config_pc.device_row = 0;
    const unsigned visible = pc_control_device_popup(catherine_config_pc.menu, player).visible;
    catherine_config_pc.device_scroll = catherine_config_pc.device_row >= visible ? catherine_config_pc.device_row + 1 - visible : 0;
    catherine_config_pc.device_wheel = 0;
}

static inline void pc_control_device_choose(Cpu *c, unsigned player, unsigned option) {
    catherine_config_setting(player ? PC_PLAYER2_DEVICE : PC_PLAYER1_DEVICE, (int)option + 1);
    catherine_config_pc.device_open = 0;
    catherine_config_pc.capturing = 2;
    Cpu cue = *c; cue.x[0] = 0xbba; f_7000277ff0(&cue);
}

static inline int pc_control_device_input(Cpu *c, uint64_t pad) {
    const unsigned player = catherine_config_pc.device_open - 1;
    const unsigned buttons = LD32(pad + 4), repeat = catherine_config_pc_repeat(catherine_config_pc.menu, pad);
    const unsigned raw = catherine_config_setting(PC_RAW_KEY, 0);
    const unsigned move = catherine_pc_mouse(2), click = catherine_pc_mouse(3);
    const int moved = move != catherine_config_pc.mouse_move, clicked = click != catherine_config_pc.mouse_click;
    const int x = catherine_pc_mouse(0), y = catherine_pc_mouse(1);
    if (catherine_config_setting(PC_APPLY_BUTTON, 0) ||
        (clicked && catherine_pc_mouse(4) && (pc_config_footer_at(x, y) || pc_config_arrow_at(x, y)))) {
        catherine_config_pc.device_open = 0;
        catherine_config_pc.capturing = 0;
        return 0; // The existing parent transaction or header owns this event.
    }
    catherine_config_pc.mouse_move = move; catherine_config_pc.mouse_click = click;
    if (catherine_config_pc.capturing == 2) {
        if (!pc_binding_buttons_held(pad)) catherine_config_pc.capturing = 0;
        return 1;
    }
    if ((buttons & (1u << 14)) || raw == 2) {
        catherine_config_pc.device_open = 0; catherine_config_pc.capturing = 2; return 1;
    }
    const CatherineDevicePopup popup = pc_control_device_popup(catherine_config_pc.menu, player);
    const unsigned count = popup.count, visible = popup.visible;
    if (catherine_config_pc.device_scroll > count - visible) catherine_config_pc.device_scroll = count - visible;
    if (catherine_config_pc.device_row >= count) catherine_config_pc.device_row = count - 1;
    if (repeat & CATHERINE_CONFIG_UP) catherine_config_pc.device_row = (catherine_config_pc.device_row + count - 1) % count;
    else if (repeat & CATHERINE_CONFIG_DOWN) catherine_config_pc.device_row = (catherine_config_pc.device_row + 1) % count;
    if ((raw == 256 || raw == 257) && raw != catherine_config_pc.device_wheel) {
        const int scroll = (int)catherine_config_pc.device_scroll + (raw == 256 ? -1 : 1);
        catherine_config_pc.device_scroll = scroll < 0 ? 0 : (unsigned)scroll > count - visible ? count - visible : (unsigned)scroll;
    }
    catherine_config_pc.device_wheel = raw;
    if (repeat & (CATHERINE_CONFIG_UP | CATHERINE_CONFIG_DOWN)) {
        if (catherine_config_pc.device_row < catherine_config_pc.device_scroll) catherine_config_pc.device_scroll = catherine_config_pc.device_row;
        if (catherine_config_pc.device_row >= catherine_config_pc.device_scroll + visible)
            catherine_config_pc.device_scroll = catherine_config_pc.device_row + 1 - visible;
    }
    if ((moved || clicked) && catherine_pc_mouse(4)) {
        const int hit = catherine_dropdown_hit(&popup, (float)x, (float)y);
        if (hit >= 0) {
            catherine_config_pc.device_row = catherine_config_pc.device_scroll + (unsigned)hit;
            if (clicked) pc_control_device_choose(c, player, catherine_config_pc.device_row);
        } else if (clicked) {
            catherine_config_pc.device_open = 0; catherine_config_pc.capturing = 2;
        }
    } else if (buttons & CATHERINE_CONFIG_CONFIRM) pc_control_device_choose(c, player, catherine_config_pc.device_row);
    return 1;
}

static inline void pc_config_pointer_adjust(Cpu *c, uint64_t menu, uint64_t pad) {
    // Reuse the actual native row/descriptor dispatch, including disabled rows
    // and its value-change cue. Restore the transient direction before returning.
    const uint32_t held = LD32(pad), edge = LD32(pad + 8), repeat = LD32(pad + 0x18);
    ST32(pad, CATHERINE_CONFIG_RIGHT);
    ST32(pad + 8, CATHERINE_CONFIG_RIGHT);
    ST32(pad + 0x18, CATHERINE_CONFIG_RIGHT);
    Cpu adjust = *c;
    adjust.x[0] = menu;
    f_7000031d70(&adjust);
    ST32(pad, held);
    ST32(pad + 8, edge);
    ST32(pad + 0x18, repeat);
}

typedef struct { float x, width; const char *label; unsigned value; } CatherineGameChoice;
static const CatherineGameChoice pc_game_difficulty[] = {
    {388,130,"Safety",0},{640,100,"Easy",1},{860,150,"Normal",2},{1118,100,"Hard",3}
};
static const CatherineGameChoice pc_game_type[] = {{415,145,"Classic",0},{662,130,"Remix",1}};
static const CatherineGameChoice pc_game_prompts[] = {
    {740,90,"Auto",0},{880,100,"Xbox",1},{1020,110,"Switch",2},{1160,75,"PS4",3},{1300,75,"PS5",4}
};
static inline const CatherineGameChoice *pc_game_choices(unsigned row,unsigned *count) {
    *count=row==0?4:row==1?2:5;
    return row==0?pc_game_difficulty:row==1?pc_game_type:pc_game_prompts;
}
static inline unsigned pc_game_choice_state(uint64_t menu,unsigned row) {
    return row<2?LD8(0x70007a0448ull+row*20+12+LD32(menu+4)):0;
}
static inline float pc_game_choice_y(uint64_t menu,unsigned row) {
    return row<2?281.5f+140.f*row:pc_list_top(menu);
}
static inline int pc_game_choice_at(uint64_t menu,unsigned row,int x,float y) {
    if(pc_game_choice_state(menu,row)!=0)return -1;
    const float top=pc_game_choice_y(menu,row);
    if(y<top-6||y>=top+48)return -1;
    unsigned count;const CatherineGameChoice *choices=pc_game_choices(row,&count);
    for(unsigned i=0;i<count;++i)if(x>=choices[i].x&&x<choices[i].x+choices[i].width)return i;
    return -1;
}
static inline int pc_game_choice_hover(uint64_t menu,unsigned row) {
    if(catherine_config_pc.menu!=menu||!catherine_config_pc_page_visible(menu)||
       LD32(menu+0xc)!=CATHERINE_CONFIG_ACTIVE||catherine_pc_display_dialog||catherine_pc_question||
       !catherine_pc_mouse(4))return -1;
    return pc_game_choice_at(menu,row,catherine_pc_mouse(0),
        catherine_pc_mouse(1)-catherine_config_pc_float(menu+0xe0)*1.5f);
}
static inline void pc_game_choice_select(Cpu *c,uint64_t menu,unsigned row,unsigned option) {
    unsigned count;const CatherineGameChoice *choices=pc_game_choices(row,&count);
    if(option>=count||pc_game_choice_state(menu,row)!=0)return;
    if(row==2){catherine_config_setting(PC_BUTTON_PROMPTS,(int)choices[option].value+100);return;}
    // Retain native state changes and option-brush animation, only on a click.
    const unsigned field=LD16(0x70007a0448ull+row*20+10);
    catherine_config_pc.selected_tab=0;ST32(menu+0x10,row);
    for(unsigned i=0;i<4;++i){
        Cpu read=*c;read.x[0]=menu;read.x[1]=field;f_7000034100(&read);
        if((unsigned)read.x[0]==choices[option].value)break;
        pc_config_pointer_adjust(c,menu,LD64(0x7000cc9070ull));
    }
}

static inline enum CatherineConfigPointerInput pc_config_pointer_input(Cpu *c, uint64_t menu, uint64_t pad,
                                                                       unsigned native_rows) {
    const unsigned move = catherine_pc_mouse(2), click = catherine_pc_mouse(3);
    const int moved = move != catherine_config_pc.mouse_move;
    const int clicked = click != catherine_config_pc.mouse_click;
    catherine_config_pc.mouse_move = move;
    catherine_config_pc.mouse_click = click;
    if ((!moved && !clicked) || !catherine_pc_mouse(4)) return PC_CONFIG_POINTER_NONE;

    const unsigned tab = catherine_config_pc_tab(menu), x = catherine_pc_mouse(0);
    const float y = (float)catherine_pc_mouse(1) - catherine_config_pc_float(menu + 0xe0) * 1.5f;
    // Suppress the guest Confirm edge that may follow the physical click by a frame.
    if (clicked) catherine_config_pc.capturing = 2;
    const unsigned arrow = pc_config_arrow_at((int)x, (int)y);
    if (arrow) {
        if (clicked && !catherine_config_bindings_visible(menu)) pc_config_pending_tabs |= arrow;
        return PC_CONFIG_POINTER_HANDLED;
    }
    if (y >= 960.0f && y < 1050.0f) {
        const unsigned footer = pc_config_footer_at((int)x, (int)y);
        if (clicked && footer == 2) return PC_CONFIG_POINTER_CONFIRM;
        if (clicked && footer && LD32(menu + 4) != 6)
            return footer == 3 ? PC_CONFIG_POINTER_DEFAULTS : PC_CONFIG_POINTER_RETURN;
        return clicked ? PC_CONFIG_POINTER_HANDLED : PC_CONFIG_POINTER_NONE;
    }

    if (tab == CATHERINE_CONFIG_CONTROL && y >= 660 && y < 830 &&
        ((x >= 280 && x < 940) || (x >= 1010 && x < 1800))) {
        const unsigned player = x >= 1010, row = y >= 751 ? 1 : 0;
        const int changed = !catherine_config_pc_selected(menu) || catherine_config_pc.row != row ||
            catherine_config_pc.control_player != player;
        catherine_config_pc.selected_tab = tab + 1;
        catherine_config_pc.row = row; catherine_config_pc.scroll = 0; catherine_config_pc.control_player = player;
        if (changed) ST32(menu + 0x1c, 0);
        if (clicked) { if (row) pc_bindings_open(c); else pc_control_device_open(player); }
        return PC_CONFIG_POINTER_HANDLED;
    }
    if(tab==CATHERINE_CONFIG_GAME){
        for(unsigned row=0;row<3;++row){
            const float top=pc_game_choice_y(menu,row);
            if(y<top-8||y>=top+65)continue;
            const int option=pc_game_choice_at(menu,row,(int)x,y);
            if(clicked&&option>=0)pc_game_choice_select(c,menu,row,(unsigned)option);
            // Hover is text-only. Never move either native selection brush.
            return PC_CONFIG_POINTER_HANDLED;
        }
    }
    if (x >= 250 && x < 1400) {
        const unsigned pc_rows = tab == CATHERINE_CONFIG_CONTROL ? 0 : catherine_config_pc_added_rows(menu, tab);
        const float top = tab == CATHERINE_CONFIG_AUDIO ? 211.5f : pc_list_top(menu);
        const float spacing = tab == CATHERINE_CONFIG_DISPLAY ? 93.0f : 140.0f;
        for (unsigned row = 0; row < pc_rows; ++row) {
            const float row_y = top + spacing * row;
            if (y < row_y - 10.0f || y >= row_y + 70.0f) continue;
            const int changed = catherine_config_pc.selected_tab != tab + 1 || catherine_config_pc.row != row;
            catherine_config_pc.selected_tab = tab + 1;
            catherine_config_pc.row = row;
            catherine_config_pc.scroll = 0;
            if (changed) ST32(menu + 0x1c, 0);
            if (clicked) {
                if (tab == CATHERINE_CONFIG_CONTROL) pc_bindings_open(c);
                else if (tab == CATHERINE_CONFIG_GAME) {
                    unsigned option=x>=740?(x-740)/140:5;
                    catherine_config_setting(PC_BUTTON_PROMPTS,option<5?(int)option+100:1);
                }
                else if (tab == CATHERINE_CONFIG_DISPLAY)
                    catherine_config_pc.notice = catherine_config_setting(row, 1) ? 0 : "This display setting could not be applied.";
                else {
                    Cpu adjust = *c;
                    adjust.x[0] = menu; adjust.x[1] = 2; adjust.x[2] = 1;
                    f_7000034720(&adjust);
                }
            }
            return PC_CONFIG_POINTER_HANDLED;
        }
        const float native_top = tab == CATHERINE_CONFIG_DISPLAY && LD32(menu + 4) < 6 ? 691.5f :
                                 tab == CATHERINE_CONFIG_AUDIO ? 351.5f : 211.5f;
        for (unsigned row = 0; row < native_rows; ++row) {
            const float row_y = native_top + 140.0f * row;
            if (y < row_y - 10.0f || y >= row_y + 125.0f) continue;
            const int changed = catherine_config_pc.selected_tab != 0 || LD32(menu + 0x10) != row;
            catherine_config_pc.selected_tab = 0;
            catherine_config_pc.row = catherine_config_pc.scroll = 0;
            if (changed) { ST32(menu + 0x10, row); ST32(menu + 0x1c, 0); }
            if (clicked && !(tab == CATHERINE_CONFIG_DISPLAY && row == 0 &&
                pc_config_brightness_click(c, menu, (int)x, catherine_pc_mouse(1))))
                pc_config_pointer_adjust(c, menu, pad);
            return PC_CONFIG_POINTER_HANDLED;
        }
    }
    // Clicking empty space must not activate the previously selected row.
    return clicked ? PC_CONFIG_POINTER_HANDLED : PC_CONFIG_POINTER_NONE;
}

// All subpage input is consumed here. In particular, shoulder buttons never
// reach the native tab handler, while raw shoulder capture remains available.
static inline enum CatherineConfigPcInput pc_bindings_input(Cpu *c, uint64_t menu, uint64_t pad) {
    const unsigned move_serial = catherine_pc_mouse(2);
    const unsigned click_serial = catherine_pc_mouse(3);
    const int mouse_moved = move_serial != catherine_bindings_ui.mouse_move;
    const int mouse_clicked = click_serial != catherine_bindings_ui.mouse_click;
    catherine_bindings_ui.mouse_move = move_serial;
    catherine_bindings_ui.mouse_click = click_serial;

    if (catherine_bindings_ui.save_applying) {
        const unsigned preview = catherine_config_setting(PC_PREVIEW, 0);
        if (!preview || ((LD32(pad + 4) & (1u << 14)) && catherine_config_setting(PC_RAW_KEY, 0) != 1)) {
            pc_bindings_cancel_display_save();
        } else if (preview == 2) {
            catherine_bindings_ui.save_applying = 0;
            catherine_bindings_ui.notice = 0;
            catherine_config_pc.notice = 0;
            catherine_pc_display_dialog = 1;
            pc_bindings_question(c, CATHERINE_PC_QUESTION_SAVE_CONFIG_DISPLAY);
        }
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    if (catherine_bindings_ui.dialog) {
        Cpu dialog = *c;
        const unsigned question = catherine_bindings_ui.dialog;
        const int display_save = question == CATHERINE_PC_QUESTION_SAVE_CONFIG_DISPLAY;
        const unsigned preview = display_save ? catherine_config_setting(PC_PREVIEW, 0) : 0;
        if (display_save && !preview) {
            f_70000729d0(&dialog);
            catherine_bindings_ui.dialog = 0;
            catherine_pc_question = CATHERINE_PC_QUESTION_NONE;
            catherine_pc_display_dialog = 0;
            pc_bindings_cancel_display_save();
            return CATHERINE_CONFIG_INPUT_HANDLED;
        }
        f_7000072a70(&dialog);
        if (dialog.x[0] & 1) {
            dialog = *c;
            f_7000072b10(&dialog);
            catherine_bindings_ui.dialog = 0;
            catherine_pc_question = CATHERINE_PC_QUESTION_NONE;
            catherine_pc_display_dialog = 0;
            if (display_save) {
                if (preview != 2 || (uint32_t)dialog.x[0] != 0) {
                    pc_bindings_cancel_display_save();
                    return CATHERINE_CONFIG_INPUT_HANDLED;
                }
                if (!catherine_config_setting(PC_KEEP, 1)) {
                    pc_bindings_cancel_display_save();
                    catherine_bindings_ui.notice = "Could not keep the display settings. Other changes are still unsaved.";
                    catherine_config_pc.notice = catherine_bindings_ui.notice;
                    catherine_bindings_ui.wait_release = 1;
                    return CATHERINE_CONFIG_INPUT_HANDLED;
                }
                return pc_bindings_commit_save();
            }
            if ((uint32_t)dialog.x[0] == 0) {
                if (question == CATHERINE_PC_QUESTION_DISCARD_BINDINGS) {
                    pc_bindings_close();
                    return CATHERINE_CONFIG_INPUT_HANDLED;
                }
                if (question == CATHERINE_PC_QUESTION_SAVE_BINDINGS) {
                    pc_bindings_confirm();
                    return CATHERINE_CONFIG_INPUT_HANDLED;
                }
                if (question == CATHERINE_PC_QUESTION_SAVE_CONFIG)
                    return pc_bindings_commit_save();
            }
            catherine_bindings_ui.wait_release = 1;
            catherine_config_pc.capturing = 2;
        }
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    if (catherine_bindings_ui.wait_release) {
        if (!pc_binding_buttons_held(pad)) {
            catherine_bindings_ui.wait_release = 0;
            catherine_bindings_ui.back_mouse_held = 0;
        }
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    CatherineBindingFocus *focus = &catherine_bindings_ui.focus;
    const unsigned raw_key = catherine_config_setting(PC_RAW_KEY, 0);
    const unsigned raw_pad = catherine_config_setting(PC_RAW_PAD, 0) & 0xfffu;
    const unsigned raw = focus->column ? raw_pad : raw_key;
    // The guest's Confirm edge can arrive one input frame after a pointer click.
    // Keep the entire drag, including its release edge, out of binding capture.
    if (catherine_bindings_ui.dragging && raw_key != 1 && !mouse_clicked) {
        catherine_bindings_ui.dragging = 0;
        catherine_bindings_ui.wait_release = 1;
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }
    if (catherine_bindings_ui.capturing) {
        catherine_bindings_ui.dragging = 0;
        if (mouse_clicked && catherine_pc_mouse(4) &&
            ((catherine_pc_mouse(0) >= 1170 && catherine_pc_mouse(0) < 1870 &&
              catherine_pc_mouse(1) >= 960 && catherine_pc_mouse(1) < 1050) ||
             pc_config_arrow_at(catherine_pc_mouse(0), catherine_pc_mouse(1))))
            catherine_bindings_ui.ignore_footer_mouse = 1;
        if (catherine_bindings_ui.ignore_footer_mouse) {
            if (raw_key != 1) catherine_bindings_ui.ignore_footer_mouse = 0;
            return CATHERINE_CONFIG_INPUT_HANDLED;
        }
        if (catherine_bindings_ui.capturing == 2) {
            if (!raw) catherine_bindings_ui.capturing = 0;
        } else if (!raw) catherine_bindings_ui.armed = 1;
        else if (catherine_bindings_ui.armed) {
            const unsigned value = catherine_binding_capture_value(focus->column, raw_key, raw_pad);
            if (value) {
                const CatherineBindingRow *row = catherine_binding_row(focus->column, focus->row[focus->column]);
                const unsigned field = (focus->column ? PC_BINDING : PC_KEY_BINDING) + row->index;
                catherine_bindings_ui.notice = catherine_config_setting(field, value)
                    ? 0 : "Could not change this binding.";
                catherine_bindings_ui.capturing = 2;
            }
        }
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    const uint32_t buttons = LD32(pad + 4);
    const uint32_t repeat = catherine_config_pc_repeat(menu, pad);
    const unsigned apply_held = catherine_config_setting(PC_APPLY_BUTTON, 0);
    catherine_bindings_ui.apply_held = apply_held;
    const int right_click = raw_key == 2 && !catherine_bindings_ui.back_mouse_held;
    catherine_bindings_ui.back_mouse_held = raw_key == 2;
    int confirm = !apply_held && (buttons & CATHERINE_CONFIG_CONFIRM) != 0;
    int scrollbar_gesture = catherine_bindings_ui.dragging != 0;
    const int wheel = raw_key == 256 || raw_key == 257;
    if (!wheel) catherine_bindings_ui.wheel_wait = 0;
    else {
        float delta = catherine_config_pc_float(0x70031cc1e8ull);
        if (!(delta > 0.0f && delta < .1f)) delta = 1.0f / 30.0f;
        catherine_bindings_ui.wheel_wait -= delta;
    }
    if (wheel && (raw_key != catherine_bindings_ui.wheel_held || catherine_bindings_ui.wheel_wait <= 0) &&
        catherine_pc_mouse(4)) {
        const unsigned x = catherine_pc_mouse(0), y = catherine_pc_mouse(1);
        if (y >= 255 && y < 831 && ((x >= 270 && x < 980) || (x >= 1020 && x < 1840))) {
            const unsigned column = x < 980 ? 0 : 1;
            catherine_binding_scroll_to(focus, column, (int)focus->scroll[column] + (raw_key == 256 ? -3 : 3));
            catherine_bindings_ui.notice = 0;
        }
        catherine_bindings_ui.wheel_wait = .12f;
    }
    catherine_bindings_ui.wheel_held = raw_key;
    if ((mouse_moved || mouse_clicked) && catherine_pc_mouse(4)) {
        const unsigned x = catherine_pc_mouse(0), y = catherine_pc_mouse(1);
        int hit = 0;
        const int track = y >= 255 && y < 831 &&
            ((x >= 932 && x < 980) || (x >= 1792 && x < 1840));
        if (mouse_clicked && !track) catherine_bindings_ui.dragging = 0;
        if ((track && mouse_clicked) || catherine_bindings_ui.dragging) {
            const unsigned column = mouse_clicked ? x < 980 ? 0 : 1 : catherine_bindings_ui.dragging - 1;
            const unsigned maximum = catherine_binding_rows(column) - CATHERINE_BINDING_VISIBLE;
            const unsigned clamped_y = y < 255 ? 255 : y > 830 ? 830 : y;
            const int scroll = (int)((clamped_y - 255) * maximum + 287) / 575;
            catherine_binding_scroll_to(focus, column, scroll);
            catherine_bindings_ui.dragging = column + 1;
            catherine_bindings_ui.notice = 0;
            scrollbar_gesture = 1;
            hit = 2;
        } else if (y >= 255 && y < 255 + 72 * CATHERINE_BINDING_VISIBLE &&
            ((x >= 270 && x < 940) || (x >= 1020 && x < 1800))) {
            const unsigned column = x < 940 ? 0 : 1;
            const unsigned row = catherine_bindings_ui.focus.scroll[column] + (y - 255) / 72;
            if (row < catherine_binding_rows(column)) {
                focus->column = column;
                focus->row[column] = row;
                focus->footer = 0;
                hit = 1;
            }
        } else if (y >= 960 && y < 1050) {
            const unsigned footer = pc_config_footer_at((int)x, (int)y);
            if (footer) {
                if (mouse_clicked) focus->footer = footer;
                hit = 1;
            }
        }
        if (mouse_clicked) {
            confirm = hit == 1;
            // Includes the grey L/R arrows: no delayed LMB Select may begin a
            // capture after an inert chrome/empty-space click.
            if (!hit) {
                catherine_bindings_ui.wait_release = 1;
                return CATHERINE_CONFIG_INPUT_HANDLED;
            }
        }
    }
    if (scrollbar_gesture) confirm = 0;

    if ((!apply_held && (buttons & (1u << 14)) && !mouse_clicked) || right_click) {
        if (catherine_config_setting(PC_BINDINGS_DIRTY, 0))
            pc_bindings_question(c, CATHERINE_PC_QUESTION_DISCARD_BINDINGS);
        else pc_bindings_close();
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }
    if (!scrollbar_gesture && !mouse_clicked && (buttons & (1u << 15))) {
        pc_bindings_defaults();
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }
    // Apply belongs to the parent page. It neither saves this child nor edits
    // its focused row, including an overlapping native Select mapping. Absolute
    // pointer clicks retain their own row/footer meaning regardless of remapping.
    if (apply_held && !mouse_clicked && !scrollbar_gesture) return CATHERINE_CONFIG_INPUT_HANDLED;

    if (repeat & CATHERINE_CONFIG_UP) catherine_binding_move(focus, 0);
    else if (repeat & CATHERINE_CONFIG_DOWN) catherine_binding_move(focus, 1);
    else if (repeat & CATHERINE_CONFIG_LEFT) catherine_binding_move(focus, 2);
    else if (repeat & CATHERINE_CONFIG_RIGHT) catherine_binding_move(focus, 3);

    if (confirm) {
        if (focus->footer) {
            const unsigned dirty = catherine_config_setting(PC_BINDINGS_DIRTY, 0);
            if (focus->footer == 1) {
                if (dirty) pc_bindings_question(c, CATHERINE_PC_QUESTION_DISCARD_BINDINGS);
                else pc_bindings_close();
            } else if (focus->footer == 3) pc_bindings_defaults();
            else pc_bindings_question(c, CATHERINE_PC_QUESTION_SAVE_BINDINGS);
        } else {
            catherine_bindings_ui.notice = 0;
            const CatherineBindingRow *row = catherine_binding_row(focus->column, focus->row[focus->column]);
            switch (row->kind) {
            case CATHERINE_BINDING_MOVEMENT: catherine_config_setting(PC_MOVEMENT_PRESET, 1); break;
            case CATHERINE_BINDING_SWAP: catherine_config_setting(PC_SWAP_STICKS, 1); break;
            case CATHERINE_BINDING_RESET_KEYBOARD: catherine_config_setting(PC_RESET_KEYBOARD, 1); break;
            case CATHERINE_BINDING_RESET_CONTROLLER: catherine_config_setting(PC_RESET_BINDINGS, 1); break;
            default:
                catherine_bindings_ui.capturing = 1;
                catherine_bindings_ui.armed = 0;
                break;
            }
        }
    }
    return CATHERINE_CONFIG_INPUT_HANDLED;
}

static inline enum CatherineConfigPcInput catherine_config_pc_input(Cpu *c, uint64_t menu) {
    const unsigned tab = catherine_config_pc_tab(menu);
    const unsigned native_rows = catherine_config_pc_row_count(menu, tab);
    if (catherine_config_pc.menu != menu || LD32(menu + 0xc) != CATHERINE_CONFIG_ACTIVE) {
        pc_config_previous_tab_key = catherine_config_setting(PC_RAW_KEY, 0);
        pc_config_pending_tabs = pc_config_tab_key_held = 0;
        return CATHERINE_CONFIG_INPUT_NATIVE;
    }

    catherine_config_pc_sync_tab(menu);
    pc_config_update_arrow_keys();
    // Co-op can disable every Game row; pointer tabs/footer must still work.

    const uint64_t pad = LD64(0x7000cc9070ull);
    if (catherine_config_bindings_visible(menu) || catherine_bindings_ui.save_applying ||
        catherine_bindings_ui.dialog == CATHERINE_PC_QUESTION_SAVE_CONFIG ||
        catherine_bindings_ui.dialog == CATHERINE_PC_QUESTION_SAVE_CONFIG_DISPLAY)
        return pc_bindings_input(c, menu, pad);
    const unsigned preview = catherine_config_setting(PC_PREVIEW, 0);
    if (tab == CATHERINE_CONFIG_CONTROL && catherine_config_pc.device_open && !preview &&
        pc_control_device_input(c, pad)) return CATHERINE_CONFIG_INPUT_HANDLED;
    // Header navigation wins over a gameplay action bound to the same Q/E key,
    // but remains unavailable while a display confirmation owns the input.
    const uint32_t intent = !preview && !catherine_pc_display_dialog
        ? catherine_config_pc_tab_intent(pad) : LD32(pad + 4);
    if (!preview && !catherine_pc_display_dialog && ((intent & CATHERINE_CONFIG_TAB) || pc_config_tab_key_held)) {
        catherine_config_pc.mouse_move = catherine_pc_mouse(2);
        catherine_config_pc.mouse_click = catherine_pc_mouse(3);
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }
    const unsigned apply_held = catherine_config_setting(PC_APPLY_BUTTON, 0);
    const int apply_edge = apply_held && !catherine_config_pc.apply_held;
    catherine_config_pc.apply_held = apply_held;
    enum CatherineConfigPointerInput pointer = PC_CONFIG_POINTER_NONE;
    if (catherine_pc_display_dialog || preview) {
        // A dialog owns these events; do not replay its click when the page returns.
        catherine_config_pc.mouse_move = catherine_pc_mouse(2);
        catherine_config_pc.mouse_click = catherine_pc_mouse(3);
    } else if (catherine_config_pc.capturing == 2) {
        catherine_config_pc.mouse_move = catherine_pc_mouse(2);
        catherine_config_pc.mouse_click = catherine_pc_mouse(3);
        if (!pc_binding_buttons_held(pad)) catherine_config_pc.capturing = 0;
        return CATHERINE_CONFIG_INPUT_HANDLED;
    } else {
        // Dedicated Apply never edits a row. Display uses Confirm for its
        // combined preview/save transaction and has no separate Apply action.
        if (apply_held) {
            if (apply_edge && tab != CATHERINE_CONFIG_DISPLAY) pc_bindings_begin_save(c);
            return CATHERINE_CONFIG_INPUT_HANDLED;
        }
        pointer = pc_config_pointer_input(c, menu, pad, native_rows);
        if (pointer == PC_CONFIG_POINTER_HANDLED) return CATHERINE_CONFIG_INPUT_HANDLED;
        if (pointer == PC_CONFIG_POINTER_RETURN || pointer == PC_CONFIG_POINTER_DEFAULTS) {
            pc_config_footer_question(c, menu, pointer == PC_CONFIG_POINTER_DEFAULTS ? 3 : 1);
            return CATHERINE_CONFIG_INPUT_HANDLED;
        }
        if (pointer == PC_CONFIG_POINTER_CONFIRM && catherine_config_setting(PC_APPLY, 0)) {
            pc_bindings_begin_save(c);
            return CATHERINE_CONFIG_INPUT_HANDLED;
        }
        if (pointer == PC_CONFIG_POINTER_CONFIRM &&
            !(tab == CATHERINE_CONFIG_DISPLAY && catherine_config_setting(PC_APPLY, 0))) {
            pc_config_save_question(c, menu);
            return CATHERINE_CONFIG_INPUT_HANDLED;
        }
    }
    if (!catherine_config_pc_added_rows(menu, tab)) return CATHERINE_CONFIG_INPUT_NATIVE;
    // Do not consume the physical-key edge here: the native tab callback below
    // is its sole consumer, after the native row handlers have been suppressed.
    const uint32_t buttons = intent | (pointer == PC_CONFIG_POINTER_CONFIRM ? CATHERINE_CONFIG_CONFIRM : 0);
    const uint32_t repeat = catherine_config_pc_repeat(menu, pad);
    const int up = (repeat & CATHERINE_CONFIG_UP) != 0;
    const int down = !up && (repeat & CATHERINE_CONFIG_DOWN) != 0;
    const int left = (repeat & CATHERINE_CONFIG_LEFT) != 0;
    const int right = (repeat & CATHERINE_CONFIG_RIGHT) != 0;
    const int confirm = (buttons & CATHERINE_CONFIG_CONFIRM) != 0;
    const int back = (buttons & (1u << 14)) != 0;
    if (tab == CATHERINE_CONFIG_AUDIO) {
        if (buttons & (CATHERINE_CONFIG_TAB | CATHERINE_CONFIG_OTHER_ACTION)) {
            catherine_config_pc.selected_tab = 0;
            return CATHERINE_CONFIG_INPUT_NATIVE;
        }
        const unsigned native_row = LD32(menu + 0x10);
        if (catherine_config_pc_selected(menu)) {
            if (up || down) {
                if (!native_rows) return CATHERINE_CONFIG_INPUT_HANDLED;
                catherine_config_pc.selected_tab = 0;
                ST32(menu + 0x10, up ? native_rows - 1 : 0);
            } else if (left || right) {
                Cpu adjust = *c;
                adjust.x[0] = menu;
                adjust.x[1] = 2;
                adjust.x[2] = left ? 0xff : 1;
                f_7000034720(&adjust);
            } else {
                return CATHERINE_CONFIG_INPUT_NATIVE;
            }
        } else if ((up && native_row == 0) || (down && native_row + 1 == native_rows)) {
            catherine_config_pc.selected_tab = tab + 1;
            catherine_config_pc.row = 0;
            ST32(menu + 0x10, 0);
        } else {
            return CATHERINE_CONFIG_INPUT_NATIVE;
        }
        Cpu nav = *c;
        nav.x[0] = 0xbba;
        f_7000277ff0(&nav);
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }
    const unsigned count = tab == CATHERINE_CONFIG_GAME ? 1 : tab == CATHERINE_CONFIG_DISPLAY ? PC_DISPLAY_ROWS : pc_control_rows();
    if (catherine_pc_display_dialog || preview) {
        Cpu dialog = *c;
        if (!catherine_pc_display_dialog) {
            if (preview == 2 && !confirm && !back) {
                catherine_pc_display_dialog = 1;
                dialog.x[0] = 0x25;
                f_7000072920(&dialog);
            } else if (back) {
                catherine_config_setting(PC_REVERT, 1);
                catherine_config_pc.save_after_apply = 0;
            }
        } else {
            if (!preview) {
                f_70000729d0(&dialog);
                dialog = *c;
            }
            f_7000072a70(&dialog);
            if (dialog.x[0] & 1) {
                dialog = *c;
                f_7000072b10(&dialog);
                const int keep = preview == 2 && (uint32_t)dialog.x[0] == 0;
                const int kept = preview && catherine_config_setting(keep ? PC_KEEP : PC_REVERT, 1) && keep;
                const int save = kept && catherine_config_pc.save_after_apply;
                if (keep && !kept)
                    catherine_config_pc.notice = "Could not save display settings.";
                catherine_config_pc.save_after_apply = 0;
                catherine_pc_display_dialog = 0;
                if (save) return CATHERINE_CONFIG_INPUT_SAVE;
            }
        }
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    // A failed/expired preview can finish before its dialog is shown.
    // It must never leave a deferred Config save armed for another Apply.
    catherine_config_pc.save_after_apply = 0;

    if (tab == CATHERINE_CONFIG_DISPLAY && confirm && catherine_config_setting(PC_APPLY, 0)) {
        pc_bindings_begin_save(c);
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    // Let the native menu handle its actions while retaining the Display row behind its dialogs.
    if ((tab == CATHERINE_CONFIG_DISPLAY || tab == CATHERINE_CONFIG_GAME) && catherine_config_pc_selected(menu) &&
        (buttons & (CATHERINE_CONFIG_TAB | CATHERINE_CONFIG_OTHER_ACTION | CATHERINE_CONFIG_CONFIRM)))
        return CATHERINE_CONFIG_INPUT_MENU_ONLY;

    if (buttons & (CATHERINE_CONFIG_TAB | CATHERINE_CONFIG_OTHER_ACTION)) {
        catherine_config_pc.selected_tab = 0;
        catherine_config_pc.row = catherine_config_pc.scroll = 0;
        catherine_config_pc.notice = 0;
        return CATHERINE_CONFIG_INPUT_NATIVE;
    }

    if (!catherine_config_pc_selected(menu)) {
        const unsigned native_row = LD32(menu + 0x10);
        if (!((up && native_row == 0) || (down && native_row + 1 == native_rows)))
            return CATHERINE_CONFIG_INPUT_NATIVE;
        catherine_config_pc.selected_tab = tab + 1;
        catherine_config_pc.row = up ? count - 1 : 0;
    } else if (up || down) {
        if ((up && catherine_config_pc.row == 0) || (down && catherine_config_pc.row + 1 == count)) {
            if(!native_rows)return CATHERINE_CONFIG_INPUT_HANDLED;
            catherine_config_pc.selected_tab = 0;
            catherine_config_pc.row = catherine_config_pc.scroll = 0;
            ST32(menu + 0x10, up ? native_rows - 1 : 0);
        } else {
            catherine_config_pc.row += down ? 1 : -1;
        }
    } else {
        if (tab == CATHERINE_CONFIG_DISPLAY && (left || right)) {
            const int step = left ? -1 : 1;
            catherine_config_pc.notice = catherine_config_setting(catherine_config_pc.row, step)
                ? 0 : "This display setting could not be applied.";
        } else if (tab == CATHERINE_CONFIG_GAME) {
            if(left||right||confirm)catherine_config_setting(PC_BUTTON_PROMPTS,left?-1:1);
        } else if (tab == CATHERINE_CONFIG_CONTROL) {
            if (catherine_config_pc.row == 1 && (left || right))
                catherine_config_pc.control_player = right ? 1 : 0;
            else if (!catherine_config_pc.row && (left || right)) {
                const unsigned field = catherine_config_pc.control_player ? PC_PLAYER2_DEVICE : PC_PLAYER1_DEVICE;
                const unsigned current = catherine_config_setting(field, 0), choices = pc_control_device_count(catherine_config_pc.control_player);
                const unsigned next = (current + (right ? 1 : choices - 1)) % choices;
                catherine_config_setting(field, (int)next + 1);
            } else if (confirm) {
                if (catherine_config_pc.row) pc_bindings_open(c);
                else { pc_control_device_open(catherine_config_pc.control_player); catherine_config_pc.capturing = 2; }
            }
        }
        return CATHERINE_CONFIG_INPUT_HANDLED;
    }

    catherine_config_pc.notice = 0;
    if (catherine_config_pc.row < catherine_config_pc.scroll)
        catherine_config_pc.scroll = catherine_config_pc.row;
    const unsigned visible = tab == CATHERINE_CONFIG_DISPLAY ? PC_VISIBLE_DISPLAY : pc_control_rows();
    if (catherine_config_pc.row >= catherine_config_pc.scroll + visible)
        catherine_config_pc.scroll = catherine_config_pc.row + 1 - visible;
    ST32(menu + 0x1c, 0);
    Cpu nav = *c;
    nav.x[0] = 0xbba;
    f_7000277ff0(&nav);
    return CATHERINE_CONFIG_INPUT_HANDLED;
}

static inline void pc_draw_text_scaled(Cpu *c, unsigned slot, const char *label, float x, float y,
                                      float scale, uint32_t rgba) {
    char16_t text[64] = {0};
    for (unsigned i = 0; i < 63 && label[i]; ++i) text[i] = (unsigned char)label[i];
    Cpu draw = *c;
    if (!pc_text[slot].handle || memcmp(pc_text[slot].text, text, sizeof text)) {
        pc_destroy_text(c, slot);
        memcpy(pc_text[slot].text, text, sizeof text);
        draw.x[0] = (uint64_t)(uintptr_t)pc_text[slot].text;
        draw.x[1] = 1;
        draw.x[2] = 0;
        f_70002abad0(&draw);
        pc_text[slot].handle = draw.x[0];
        draw = *c;
    }
    const uint64_t handle = pc_text[slot].handle;
    if (!handle) return;
    draw.x[0] = handle;
    draw.x[1] = rgba;
    draw.x[2] = (uint32_t)x;
    draw.x[3] = (uint32_t)y;
    draw.x[4] = 1;
    draw.x[5] = 0;
    draw.v[0] = catherine_config_pc_bits(x);
    draw.v[1] = catherine_config_pc_bits(y);
    draw.v[2] = 0;
    draw.v[3] = catherine_config_pc_bits(scale);
    draw.v[4] = catherine_config_pc_bits(scale);
    draw.v[5] = 0x3f800000u;
    draw.pc = LD64(LD64(handle) + 0x28);
    rt_dispatch(&draw);
}

static inline void pc_draw_text(Cpu *c, unsigned slot, const char *label, float x, float y, uint32_t rgba) {
    pc_draw_text_scaled(c, slot, label, x, y, 1.0f, rgba);
}

static inline void pc_bindings_brush(Cpu *c, uint64_t menu, float x, float y, float scale, uint32_t rgba) {
    Cpu draw = *c;
    draw.x[0] = LD64(menu + 0x180);
    draw.x[1] = 0x1d;
    draw.x[2] = rgba;
    draw.x[3] = 0x37;
    draw.v[0] = catherine_config_pc_bits(x);
    draw.v[1] = catherine_config_pc_bits(y);
    draw.v[2] = catherine_config_pc_bits(scale);
    draw.v[3] = 0x3f800000u;
    f_700008fd00(&draw);
}

static inline float pc_bindings_footer_width(Cpu *c, uint64_t label) {
    if (!label) return 0.0f;
    Cpu measure = *c;
    measure.x[0] = label;
    measure.x[1] = 0;
    measure.pc = LD64(LD64(label) + 0x30);
    rt_dispatch(&measure);
    const uint32_t bits = (uint32_t)measure.v[0];
    float width;
    memcpy(&width, &bits, sizeof width);
    return width;
}

static inline uint64_t pc_bindings_footer_label(Cpu *c, unsigned slot, const char *label) {
    char16_t text[64] = {0};
    for (unsigned i = 0; i < 63 && label[i]; ++i) text[i] = (unsigned char)label[i];
    if (!pc_text[slot].handle || memcmp(pc_text[slot].text, text, sizeof text)) {
        pc_destroy_text(c, slot);
        memcpy(pc_text[slot].text, text, sizeof text);
        Cpu create = *c;
        create.x[0] = (uint64_t)(uintptr_t)pc_text[slot].text;
        create.x[1] = 1;
        create.x[2] = 0;
        f_70002abad0(&create);
        pc_text[slot].handle = create.x[0];
    }
    return pc_text[slot].handle;
}

static inline void pc_bindings_footer_draw_colors(Cpu *c, uint64_t menu, uint64_t label,
                                                  float left, unsigned glyph, uint32_t rgba, uint32_t glyph_rgba) {
    if (!label) return;
    // f329d0's footer text method uses source coordinates; its glyphs use the
    // scaled canvas. Reuse its shadow, baseline and active mapped action badges.
    for (unsigned shadow = 0; shadow != 2; ++shadow) {
        Cpu draw = *c;
        draw.x[0] = label;
        draw.x[1] = shadow ? rgba : rgba & 0xff;
        draw.x[2] = 1;
        draw.x[3] = 0;
        draw.v[0] = catherine_config_pc_bits(left + (shadow ? 0.0f : 2.0f));
        draw.v[1] = catherine_config_pc_bits(shadow ? 665.0f : 667.0f);
        draw.v[2] = 0;
        draw.pc = LD64(LD64(label) + 0x18);
        rt_dispatch(&draw);
    }
    Cpu draw = *c;
    draw.x[0] = LD64(menu + 0x180);
    draw.x[1] = glyph;
    draw.x[2] = glyph_rgba;
    draw.x[3] = 0x37;
    draw.v[0] = catherine_config_pc_bits((left - 32.0f) * 1.5f);
    draw.v[1] = catherine_config_pc_bits(994.5f);
    draw.v[2] = draw.v[3] = 0x3f800000u;
    f_700008fd00(&draw);
}

static inline void pc_bindings_footer_draw(Cpu *c, uint64_t menu, uint64_t label,
                                           float left, unsigned glyph, uint32_t rgba) {
    pc_bindings_footer_draw_colors(c, menu, label, left, glyph, rgba, rgba);
}

static inline void pc_bindings_confirm_draw(Cpu *c, uint64_t menu, uint32_t rgba) {
    const uint64_t label = LD64(menu + 0x2c0);
    const float left = 1216.0f - pc_bindings_footer_width(c, label) * (2.0f / 3.0f);
    pc_bindings_footer_draw(c, menu, label, left, 0x2d, rgba);
}

static inline void pc_bindings_draw(Cpu *c, uint64_t menu) {
    const uint32_t alpha = (uint32_t)(LD8(menu + 0x3c) * catherine_config_pc_float(menu + 0x9c));
    if (!alpha) return;
    const CatherineBindingFocus *focus = &catherine_bindings_ui.focus;
    static const char *const presets[] = {"WASD", "ESDF", "Arrow Keys"};
    static const char *const xbox[] = {
        "B", "A", "Y", "X", "LS", "RS", "LB", "RB", "LT", "RT", "Menu", "View",
        "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down"
    };
    static const char *const playstation[] = {
        "Circle", "Cross", "Triangle", "Square", "L3", "R3", "L1", "R1", "L2", "R2", "Options", "Create",
        "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down"
    };
    const unsigned device = catherine_config_setting(PC_INPUT_LAYOUT, 0);
    const char *const *buttons = device == 2 || device == 4 ? playstation : xbox;
    pc_draw_text(c, 18, catherine_config_setting(PC_BINDING_PLAYER, 0) ? "Player 2: Keyboard & Mouse" : "Player 1: Keyboard & Mouse", 360.0f, 190.0f, 0xffffff00u | alpha);
    pc_draw_text(c, 19, "Controller", 1160.0f, 190.0f, 0xffffff00u | alpha);

    for (unsigned column = 0; column != 2; ++column) {
        const float left = column ? 1030.0f : 280.0f;
        const float value_x = column ? 1490.0f : 725.0f;
        const unsigned count = catherine_binding_rows(column);
        for (unsigned i = 0; i < CATHERINE_BINDING_VISIBLE; ++i) {
            const unsigned row_index = focus->scroll[column] + i;
            if (row_index >= count) break;
            const CatherineBindingRow *row = catherine_binding_row(column, row_index);
            const float y = 265.0f + 72.0f * i;
            const int selected = !focus->footer && focus->column == column && focus->row[column] == row_index;
            if (selected) pc_bindings_brush(c, menu, left - 20.0f, y - 8.0f, 1.15f, 0x76767600u | alpha);
            const uint32_t color = (selected ? 0xff369800u : 0xffffff00u) | alpha;
            const unsigned slot = 20 + column * CATHERINE_BINDING_VISIBLE * 2 + i * 2;
            pc_draw_text_scaled(c, slot, row->label, left, y, .82f, 0xffffff00u | alpha);
            const char *value = 0;
            switch (row->kind) {
            case CATHERINE_BINDING_MOVEMENT: {
                const unsigned preset = catherine_config_setting(PC_MOVEMENT_PRESET, 0);
                value = presets[preset < 3 ? preset : 0];
                break;
            }
            case CATHERINE_BINDING_SWAP: value = catherine_config_setting(PC_SWAP_STICKS, 0) ? "On" : "Off"; break;
            case CATHERINE_BINDING_KEY: value = catherine_keyboard_label(row->index); break;
            case CATHERINE_BINDING_PAD: {
                const unsigned button = catherine_config_setting(PC_BINDING + row->index, 0);
                value = button < 16 ? buttons[button] : "Button";
                break;
            }
            default: break;
            }
            if (value) pc_draw_text_scaled(c, slot + 1, value, value_x, y, .82f, color);
        }
        const float x = column ? 1810.0f : 950.0f;
        for (unsigned segment = 0; segment <= 12; ++segment) {
            const unsigned row = segment * (count - 1) / 12;
            const int thumb = row >= focus->scroll[column] && row < focus->scroll[column] + CATHERINE_BINDING_VISIBLE;
            pc_draw_text(c, 52 + column, "|", x, 260.0f + 42.0f * segment,
                         (thumb ? 0xff369800u : 0x60606000u) | alpha);
        }
    }

    const int save_enabled = !catherine_bindings_ui.capturing && !catherine_bindings_ui.wait_release &&
        !catherine_bindings_ui.save_applying && !catherine_bindings_ui.dialog;
    const uint32_t glyph_rgba = (save_enabled ? 0xffffff00u : 0x68686800u) | alpha;
    const uint32_t default_rgba = !save_enabled ? glyph_rgba :
        catherine_config_pc_footer_text_color(menu, 3, (focus->footer == 3 ? 0xff369800u : 0xffffff00u) | alpha);
    const uint32_t back_rgba = !save_enabled ? glyph_rgba :
        catherine_config_pc_footer_text_color(menu, 1, (focus->footer == 1 ? 0xff369800u : 0xffffff00u) | alpha);
    const uint32_t confirm_rgba = !save_enabled ? glyph_rgba :
        catherine_config_pc_footer_text_color(menu, 2, (focus->footer == 2 ? 0xff369800u : 0xffffff00u) | alpha);
    // Align every action to its original parent slot, using the parent's own
    // localized label widths even though this child says Default Controls/Discard.
    const float confirm_left = 1216.0f - pc_bindings_footer_width(c, LD64(menu + 0x2c0)) * (2.0f / 3.0f);
    const float discard_left = confirm_left - 58.0f - pc_bindings_footer_width(c, LD64(menu + 0x2c8)) * (2.0f / 3.0f);
    const float default_left = discard_left - 58.0f - pc_bindings_footer_width(c, LD64(menu + 0x2d0)) * (2.0f / 3.0f);
    pc_bindings_footer_draw_colors(c, menu, pc_bindings_footer_label(c, 55, "Default Controls"), default_left, 0x2f, default_rgba, glyph_rgba);
    pc_bindings_footer_draw_colors(c, menu, pc_bindings_footer_label(c, 54, "Discard"), discard_left, 0x30, back_rgba, glyph_rgba);
    pc_bindings_footer_draw_colors(c, menu, LD64(menu + 0x2c0), confirm_left, 0x2d, confirm_rgba, glyph_rgba);
    const char *help = catherine_bindings_ui.notice;
    if (catherine_bindings_ui.capturing == 1)
        help = focus->column ? "Release the controller buttons, then press a new button."
                             : "Release the keys, then press a key or mouse button.";
    else if (!help) {
        const CatherineBindingRow *row = catherine_binding_row(focus->column, focus->row[focus->column]);
        help = !focus->footer && row->kind == CATHERINE_BINDING_MOVEMENT
            ? "Movement keys are reserved. Confirm to choose a layout."
            : "Select: edit. Confirm: save controls. Back: discard.";
    }
    if (!catherine_bindings_ui.dialog) pc_draw_text_scaled(c, 56, help, 280.0f, 890.0f, .85f, 0xffffff00u | alpha);
}

static inline void pc_control_box_line(Cpu *c, uint64_t menu, float x, float y,
                                       float width, float height, uint32_t rgba) {
    Cpu draw = *c;
    draw.x[0] = LD64(menu + 0x180); draw.x[1] = 0x1d;
    draw.x[2] = rgba; draw.x[3] = 0x37;
    draw.v[0] = catherine_config_pc_bits(x); draw.v[1] = catherine_config_pc_bits(y);
    draw.v[2] = catherine_config_pc_bits(width / 468.0f);
    draw.v[3] = catherine_config_pc_bits(height / 60.0f);
    f_700008fd00(&draw);
}

typedef struct { Cpu *cpu; uint64_t menu; } PcControlRectContext;

static inline void pc_control_rect(void *ctx,float x,float y,float width,float height,uint32_t rgba) {
    const PcControlRectContext *r=(const PcControlRectContext *)ctx;
    pc_control_box_line(r->cpu,r->menu,x,y,width,height,rgba);
}
static inline void pc_control_box_outline(Cpu *c,uint64_t menu,float x,float y,float width,float height,uint32_t rgba) {
    PcControlRectContext ctx={c,menu};
    catherine_dropdown_outline(&ctx,pc_control_rect,x,y,width,height,rgba);
}

static inline void pc_control_draw(Cpu *c, uint64_t menu) {
    if (catherine_config_pc.menu != menu || !catherine_config_pc_page_visible(menu) ||
        !catherine_config_pc_added_rows(menu, CATHERINE_CONFIG_CONTROL)) return;
    const uint32_t alpha = (uint32_t)(catherine_config_page_alpha(menu) * catherine_config_pc_float(menu + 0x9c));
    const float offset = catherine_config_pc_float(menu + 0xe0) * 1.5f;
    for (unsigned player = 0; player < 2; ++player) {
        const float left = player ? 1030.0f : 322.0f;
        pc_control_box_outline(c, menu, left - 20.0f, 660.0f + offset, 650.0f, 68.0f, 0xffffff00u | alpha);
        const unsigned device = catherine_config_setting(player ? PC_PLAYER2_DEVICE : PC_PLAYER1_DEVICE, 0);
        const int selected = catherine_config_pc_selected(menu) && catherine_config_pc.control_player == player;
        pc_draw_text(c, 60 + player, player ? "Player 2" : "Player 1", left, 631.5f + offset, 0xffffff00u | alpha);
        pc_draw_text_scaled(c, 62 + player, catherine_input_device_label(player, device), left, 681.5f + offset, .85f,
            (selected && !catherine_config_pc.row ? 0xff369800u : 0xffffff00u) | alpha);
        pc_draw_text(c, 72 + player, "v", left + 570.0f, 681.5f + offset, 0xffffff00u | alpha);
        if (catherine_config_pc.device_open != player + 1)
            pc_draw_text(c, 74 + player, "Config Bindings", left, 774.5f + offset,
                (selected && catherine_config_pc.row ? 0xff369800u : 0xffffff00u) | alpha);
    }
    if (catherine_config_pc_selected(menu) && !catherine_config_pc.device_open)
        pc_draw_text(c, 16, catherine_config_pc.row ? "Confirm to configure this player's controls." :
            "Select this player's input device.", 247.0f, 915.0f + offset, 0xffffff00u | alpha);
    if (!catherine_config_pc.device_open) return;
    const unsigned player = catherine_config_pc.device_open - 1;
    const CatherineDevicePopup popup = pc_control_device_popup(menu, player);
    const unsigned visible = popup.visible;
    const float left = popup.left;
    PcControlRectContext ctx={c,menu};
    catherine_dropdown_frame(&ctx,pc_control_rect,&popup,catherine_config_pc.device_scroll,alpha);
    for (unsigned row = 0; row < visible; ++row) {
        const unsigned option = catherine_config_pc.device_scroll + row;
        pc_draw_text_scaled(c, 64 + row, catherine_input_device_label(player, option), left + 20.0f,
            popup.top + 22.0f + 55.0f * row, .85f,
            (option == catherine_config_pc.device_row ? 0xff369800u : 0xffffff00u) | alpha);
    }
}

static inline void catherine_config_pc_draw(Cpu *c, uint64_t menu) {
    if (catherine_config_bindings_visible(menu)) {
        pc_bindings_draw(c, menu);
        return;
    }
    static const char *const display[] = {
        "Frame-rate Limit", "Display Mode", "Resolution", "Anti-aliasing", "FMV Interpolation"
    };
    static const char *const values[][6] = {
        {"Auto", "30 FPS", "60 FPS", "120 FPS", "240 FPS", "Unlimited"},
        {"Windowed", "Fullscreen"},
        {"Auto", "720p", "1080p", "1440p", "2160p", "Desktop"},
        {"Off", "FXAA", "MSAA 2x", "MSAA 4x", "MSAA 8x"},
        {"Off", "60 FPS", "120 FPS", "240 FPS", "Match"}
    };
    const unsigned tab = catherine_config_pc_tab(menu);
    if(tab==CATHERINE_CONFIG_GAME) {
        if(catherine_config_pc.menu!=menu||!catherine_config_pc_page_visible(menu)||!catherine_config_pc_added_rows(menu,tab))return;
        const float offset=catherine_config_pc_float(menu+0xe0)*1.5f;
        const unsigned alpha=(unsigned)(catherine_config_page_alpha(menu)*catherine_config_pc_float(menu+0x9c));
        const float y=pc_list_top(menu)+offset;static const char*const labels[]={"Auto","Xbox","Switch","PS4","PS5"};
        pc_draw_text(c,20,"Button Prompts",322.f,y,0xffffff00u|alpha);
        const unsigned chosen=catherine_config_setting(PC_BUTTON_PROMPTS,0);
        const int hover=pc_game_choice_hover(menu,2);
        for(unsigned i=0;i<5;++i){
            pc_draw_text(c,21+i,labels[i],740.f+140.f*i,y,(i==chosen||(int)i==hover?0xff369800u:0xffffff00u)|alpha);
            if(i<4)pc_draw_text(c,26+i,"/",840.f+140.f*i,y,0xffffff00u|alpha);
        }
        // Preserve native row labels and brushes, and draw each option word
        // independently so hovering one never recolours its neighbours.
        for(unsigned row=0;row<2;++row){
            unsigned count;const CatherineGameChoice *choices=pc_game_choices(row,&count);
            const unsigned state=pc_game_choice_state(menu,row);if(state==2)continue;
            const unsigned opacity=state==1?alpha/2:alpha;
            const int hovered=pc_game_choice_hover(menu,row);
            for(unsigned i=0;i<count;++i){
                pc_draw_text(c,30+row*8+i*2,choices[i].label,choices[i].x,pc_game_choice_y(menu,row)+offset,
                    ((int)i==hovered?0xff369800u:0xffffff00u)|opacity);
            }
        }
        if(catherine_config_pc_selected(menu))pc_draw_text(c,16,"Gamepad icons only. Auto follows the active controller.",247.f,915.f+offset,0xffffff00u|alpha);
        return;
    }
    if (tab == CATHERINE_CONFIG_CONTROL) { pc_control_draw(c, menu); return; }
    if (!catherine_config_pc_added_rows(menu, tab)) return;
    if (tab == CATHERINE_CONFIG_AUDIO) {
        if (catherine_config_pc_selected(menu)) {
            const uint32_t alpha = (uint32_t)(LD8(menu + 0x3c) * catherine_config_pc_float(menu + 0x9c));
            pc_draw_text(c, 16, "Toggle the display of subtitles.", 247.0f,
                         915.0f + catherine_config_pc_float(menu + 0xe0) * 1.5f, 0xffffff00u | alpha);
        }
        return;
    }
    const int display_page = tab == CATHERINE_CONFIG_DISPLAY;
    const int display_entrance = catherine_config_display_entering(menu);
    if (catherine_config_pc.menu != menu || !catherine_config_pc_page_visible(menu) ||
        !catherine_config_pc_row_count(menu, tab)) return;
    const int selected = catherine_config_pc_selected(menu);
    const unsigned scroll = selected ? catherine_config_pc.scroll : 0;
    const unsigned rows = display_page ? PC_VISIBLE_DISPLAY : PC_VISIBLE_BINDINGS;
    const float offset = catherine_config_pc_float(menu + 0xe0) * 1.5f;
    const float fade = catherine_config_pc_float(menu + 0x9c);
    const uint32_t alpha = (uint32_t)(catherine_config_page_alpha(menu) * fade);
    if (!alpha) return;

    const float row_offset = display_entrance ? catherine_config_pc_float(menu + 0xa4) * 1.5f : offset;
    const float top = pc_list_top(menu) + row_offset;
    for (unsigned i = 0; i < rows; ++i) {
        const unsigned row = scroll + i;
        const float y = top + 93.0f * i;
        const uint32_t color = ((selected && catherine_config_pc.row == row) ? 0xff369800u : 0xffffff00u) | alpha;
        pc_draw_text(c, i * 2, display_page ? display[row] : "Config Bindings", 322.0f, y, 0xffffff00u | alpha);
        const char *value = 0;
        if (display_page) value = values[row][catherine_config_setting(row, 0)];
        if (value) pc_draw_text(c, i * 2 + 1, value, 740.0f, y, color);
    }
    const unsigned preview = display_page ? catherine_config_setting(PC_PREVIEW, 0) : 0;
    const char *help = catherine_config_pc.notice;
    if (preview == 1) help = "Applying display changes...";
    if (catherine_pc_display_dialog) return;
    if (!help && selected && display_page && catherine_config_pc.row < PC_DISPLAY_ROWS) {
        // Keep each line within the text renderer's 63-character limit. These
        // slots are separate from row labels and leave native Brightness alone.
        static const char *const descriptions[PC_DISPLAY_ROWS][2] = {
            {"Frame-rate Limit: Sets the maximum gameplay frame rate.",
             "Higher limits look smoother but use more processing power."},
            {"Display Mode: Choose a window or fill the whole screen.",
             "Windowed keeps the desktop visible; Fullscreen fills it."},
            {"Resolution: Sets how many pixels the game renders.",
             "Higher values look sharper but require more GPU power."},
            {"Anti-Aliasing: Smooths jagged edges on objects.",
             "FXAA is faster; MSAA keeps detail but uses more GPU power."},
            {"FMV Interpolation: Adds frames between cutscene video frames.",
             "Smooths video motion, but can cause blur or visual artifacts."}
        };
        const unsigned row = catherine_config_pc.row;
        pc_draw_text_scaled(c, 16, descriptions[row][0], 247.0f, 870.0f + offset, .82f, 0xffffff00u | alpha);
        pc_draw_text_scaled(c, 17, descriptions[row][1], 247.0f, 903.0f + offset, .82f, 0xffffff00u | alpha);
        pc_draw_text_scaled(c, 18, catherine_config_setting(PC_INPUT_LAYOUT, 0) == 0
            ? "Use A, D or Left Click to change setting."
            : "Use Left or Right to change setting.",
            247.0f, 936.0f + offset, .82f, 0xffffff00u | alpha);
        return;
    }
    if (!help && selected && !display_page) help = "Confirm to configure keyboard, mouse and controller bindings.";
    if (help) pc_draw_text(c, 16, help, 247.0f, 915.0f + offset, 0xffffff00u | alpha);

}
