#pragma once
#include "config_pc_host.h"

#define CATHERINE_PHONE_SAVE_DIALOG 1
static uint64_t catherine_pc_save_widget;
extern void f_700001fd20(Cpu *);
extern void f_7000025690(Cpu *);
extern void f_70000258c0(Cpu *);
extern void f_70002782c0(Cpu *);

static struct { uint64_t panel; unsigned moves, clicks; int wheel, wheel_pending; int wheel_hover_lock, wheel_x, wheel_y; int suppress_click, list_ready; } catherine_phone_save_mouse;

static inline void catherine_phone_save_begin(uint64_t panel) {
    catherine_pc_save_widget = panel + 0x8b0;
    catherine_phone_save_mouse.panel = panel;
    catherine_phone_save_mouse.moves = (unsigned)catherine_pc_mouse(2);
    catherine_phone_save_mouse.wheel = catherine_pc_mouse(5);
    catherine_phone_save_mouse.wheel_pending = 0;
    catherine_phone_save_mouse.wheel_hover_lock = 0;
    catherine_phone_save_mouse.clicks = (unsigned)catherine_pc_mouse(3);
    catherine_phone_save_mouse.suppress_click = 0;
    catherine_phone_save_mouse.list_ready = 0;
    catherine_config_setting(PC_CURSOR_HIDDEN, 0);
}

static inline void catherine_phone_save_end(uint64_t panel) {
    if (catherine_phone_save_mouse.panel != panel) return;
    catherine_phone_save_mouse.panel = 0;
    catherine_pc_save_widget = 0;
    catherine_config_setting(PC_CURSOR_HIDDEN, 1);
}

static inline void catherine_phone_save_select(Cpu *c, uint64_t panel, unsigned row, int sound) {
    const unsigned old = LD8(panel + 0x824);
    if (old >= 5 || row >= 5 || old == row) return;
    for (unsigned i = 0; i < 2; ++i) {
        Cpu call = *c;
        call.x[0] = panel + 0x5b8 + (i ? row : old) * 0x78;
        call.x[1] = i;
        call.pc = LD64(LD64(call.x[0]) + 0xc8);
        rt_dispatch(&call);
    }
    Cpu call = *c;
    call.x[0] = panel + 0x810; call.x[1] = row;
    f_700001fd20(&call);
    ST8(panel + 0x865, 1);
    if (sound) { call = *c; call.x[0] = 0x3ea; call.x[1] = (uint32_t)-1; f_70002782c0(&call); }
}

/* Page boundaries are absolute slots 1, 6, 11, 16, even after hover. */
static inline void catherine_phone_save_page(Cpu *c, uint64_t panel, int up) {
    const unsigned first = LD8(panel + 0x910);
    const unsigned row = LD8(panel + 0x824);
    if (first > 15 || row >= 5) return;
    const unsigned page = (first + row) / 5;
    if ((up && !page) || (!up && page >= 3)) return;
    const unsigned target = (up ? page - 1 : page + 1) * 5;
    if (first != target) {
        /* Put selection at the scrolling edge, then reuse native card rebinding. */
        catherine_phone_save_select(c, panel, target < first ? 0 : 4, 0);
        for (unsigned i = 0; i < 20; ++i) {
            const unsigned before = LD8(panel + 0x910);
            if (before == target) break;
            Cpu call = *c; call.x[0] = panel;
            if (before > target) f_7000025690(&call); else f_70000258c0(&call);
            if (LD8(panel + 0x910) == before) break;
        }
    }
    catherine_phone_save_select(c, panel, 0, 0);
}

static inline uint32_t catherine_phone_save_input(Cpu *c, uint64_t panel, uint32_t pressed) {
    const uint32_t confirm = 1u << 13, back = 1u << 14;
    if (catherine_phone_save_mouse.panel != panel) catherine_phone_save_begin(panel);
    catherine_config_setting(PC_CURSOR_HIDDEN, 0);
    const unsigned moves = (unsigned)catherine_pc_mouse(2), clicks = (unsigned)catherine_pc_mouse(3);
    if (!catherine_phone_save_mouse.list_ready) {
        catherine_phone_save_mouse.list_ready = 1;
        catherine_phone_save_mouse.wheel = catherine_pc_mouse(5);
        catherine_phone_save_mouse.wheel_pending = 0;
        catherine_phone_save_mouse.wheel_hover_lock = 0;
        catherine_phone_save_mouse.moves = moves; catherine_phone_save_mouse.clicks = clicks;
        catherine_phone_save_mouse.suppress_click = catherine_config_setting(PC_RAW_KEY, 0) == 1;
        return catherine_phone_save_mouse.suppress_click ? pressed & ~confirm : pressed;
    }
    const int moved = moves != catherine_phone_save_mouse.moves;
    const int clicked = clicks != catherine_phone_save_mouse.clicks;
    catherine_phone_save_mouse.moves = moves; catherine_phone_save_mouse.clicks = clicks;
    if (catherine_config_setting(PC_RAW_KEY, 0) > 1 || catherine_config_setting(PC_RAW_PAD, 0))
        catherine_phone_save_mouse.suppress_click = 0;
    if (catherine_phone_save_mouse.suppress_click) {
        const uint64_t pad = LD64(0x7000cc9070ull);
        if (!((pressed | (pad ? LD32(pad) : 0)) & confirm)) catherine_phone_save_mouse.suppress_click = 0;
        pressed &= ~confirm;
    }
    if (catherine_config_setting(PC_RAW_KEY, 0) == 1) pressed &= ~confirm;
    const int wheel = catherine_pc_mouse(5);
    const int delta = (int32_t)((uint32_t)wheel - (uint32_t)catherine_phone_save_mouse.wheel);
    catherine_phone_save_mouse.wheel = wheel;
    if (delta) {
        catherine_phone_save_mouse.wheel_hover_lock = 1;
        catherine_phone_save_mouse.wheel_x = catherine_pc_mouse(0);
        catherine_phone_save_mouse.wheel_y = catherine_pc_mouse(1);
    }
    if (catherine_pc_mouse(4)) {
        // Preserve partial detents and drain rapid scrolling one five-slot page per update.
        const int bounded = delta > 2400 ? 2400 : delta < -2400 ? -2400 : delta;
        catherine_phone_save_mouse.wheel_pending += bounded;
        if (catherine_phone_save_mouse.wheel_pending > 2400) catherine_phone_save_mouse.wheel_pending = 2400;
        if (catherine_phone_save_mouse.wheel_pending < -2400) catherine_phone_save_mouse.wheel_pending = -2400;
        if (catherine_phone_save_mouse.wheel_pending >= 120 || catherine_phone_save_mouse.wheel_pending <= -120) {
            Cpu call = *c; call.x[0] = panel;
            const int up = catherine_phone_save_mouse.wheel_pending > 0;
            catherine_phone_save_mouse.wheel_pending += up ? -120 : 120;
            
            call = *c; call.x[0] = panel;
            catherine_phone_save_page(c, panel, up);
            return pressed & ~confirm;
        }
    } else catherine_phone_save_mouse.wheel_pending = 0;
    // Scrolling owns selection until a deliberate pointer move or click.
    // Small wheel-hand jitter must not reselect the card under the pointer.
    if (catherine_phone_save_mouse.wheel_hover_lock && !clicked) {
        const int dx = catherine_pc_mouse(0) - catherine_phone_save_mouse.wheel_x;
        const int dy = catherine_pc_mouse(1) - catherine_phone_save_mouse.wheel_y;
        if (!moved || (dx > -8 && dx < 8 && dy > -8 && dy < 8)) return pressed;
        catherine_phone_save_mouse.wheel_hover_lock = 0;
    } else if (clicked) catherine_phone_save_mouse.wheel_hover_lock = 0;
    if (!moved && !clicked) return pressed;
    const int x = catherine_pc_mouse(0), y = catherine_pc_mouse(1), inside = catherine_pc_mouse(4);
    int row = -1;
    if (inside && x >= 315 && x <= 972)
        for (int i = 0; i < 5; ++i)
            if (y >= 187 + 150 * i && y <= 328 + 150 * i) { row = i; break; }
    if (row >= 0) catherine_phone_save_select(c, panel, (unsigned)row, 1);
    if (!clicked) return pressed;
    catherine_phone_save_mouse.suppress_click = 1;
    pressed &= ~confirm;
    if (row >= 0) return pressed | confirm;
    if (!inside) return pressed;
    if (x >= 555 && x <= 735 && y >= 958 && y <= 1005) return pressed | confirm;
    if (x >= 780 && x <= 960 && y >= 958 && y <= 1005) return pressed | back;
    if (x >= 623 && x <= 670 && ((y >= 929 && y <= 955) || (y >= 1005 && y <= 1031))) {
        Cpu call = *c; call.x[0] = panel;
        if (y < 958) f_7000025690(&call); else f_70000258c0(&call);
    }
    return pressed;
}
