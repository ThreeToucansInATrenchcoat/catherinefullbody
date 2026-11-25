#pragma once

#include "config_pc_host.h"

extern void f_700001fd20(Cpu *c);
extern void f_700026e8c0(Cpu *c);
extern void f_700026eb50(Cpu *c);
extern void f_7000277ff0(Cpu *c);

/* The embedded confirmation belongs to this same native Save/Load component. */
static uint64_t catherine_pc_save_widget;

static struct {
    uint64_t menu;
    unsigned moves, clicks; int wheel, wheel_pending; int wheel_hover_lock, wheel_x, wheel_y;
    int suppress_click, list_ready;
} catherine_save_mouse;

static inline void catherine_save_mouse_begin(uint64_t menu) {
    catherine_save_mouse.menu = menu;
    catherine_pc_save_widget = menu + 0xad0;
    catherine_save_mouse.moves = (unsigned)catherine_pc_mouse(2);
    catherine_save_mouse.wheel = catherine_pc_mouse(5);
    catherine_save_mouse.wheel_pending = 0;
    catherine_save_mouse.wheel_hover_lock = 0;
    catherine_save_mouse.clicks = (unsigned)catherine_pc_mouse(3);
    catherine_save_mouse.suppress_click = 0;
    catherine_save_mouse.list_ready = 0;
}

static inline void catherine_save_mouse_end(uint64_t menu) {
    if (catherine_pc_save_widget == menu + 0xad0) catherine_pc_save_widget = 0;
    catherine_config_setting(PC_CURSOR_HIDDEN, 1);
}

/* The five native cards are on the same 1920x1080 canvas at every entry point.
 * Gaps and the scrollbar are excluded so a click cannot activate a stale card. */
static inline int catherine_save_mouse_row(int x, int y) {
    static const int top[5] = {170, 375, 545, 715, 885};
    static const int bottom[5] = {350, 530, 700, 870, 1040};
    if (x < 1040 || x > 1835) return -1;
    for (int i = 0; i < 5; ++i)
        if (y >= top[i] && y <= bottom[i]) return i;
    return -1;
}

/* Reuse the native card highlight and selection methods; retain the page's
 * current first slot instead of scrolling when the pointer crosses a card. */
static inline void catherine_save_mouse_select(Cpu *c, uint64_t menu, unsigned row, int sound) {
    const unsigned previous = LD8(menu + 0xa0c);
    if (row >= 5 || previous >= 5 || row == previous) return;
    for (unsigned i = 0; i < 2; ++i) {
        Cpu call = *c;
        const unsigned index = i ? row : previous;
        const uint64_t card = menu + 0x608 + index * 0xa0;
        call.x[0] = card;
        call.x[1] = i;
        call.pc = LD64(LD64(card) + 0xc8);
        rt_dispatch(&call);
    }
    Cpu call = *c;
    call.x[0] = menu + 0x9f8;
    call.x[1] = row;
    f_700001fd20(&call);
    ST8(menu + 0xb88, 1);
    if (sound) { call = *c; call.x[0] = 0xbd3; f_7000277ff0(&call); }
}

/* Page boundaries are absolute slots 1, 6, 11, 16, even after hover. */
static inline void catherine_save_mouse_page(Cpu *c, uint64_t menu, int up) {
    const unsigned first = LD8(menu + 0xb74);
    const unsigned row = LD8(menu + 0xa0c);
    if (first > 15 || row >= 5) return;
    const unsigned page = (first + row) / 5;
    if ((up && !page) || (!up && page >= 3)) return;
    const unsigned target = (up ? page - 1 : page + 1) * 5;
    if (first != target) {
        /* Put selection at the scrolling edge, then reuse native card rebinding. */
        catherine_save_mouse_select(c, menu, target < first ? 0 : 4, 0);
        for (unsigned i = 0; i < 20; ++i) {
            const unsigned before = LD8(menu + 0xb74);
            if (before == target) break;
            Cpu call = *c; call.x[0] = menu;
            if (before > target) f_700026e8c0(&call); else f_700026eb50(&call);
            if (LD8(menu + 0xb74) == before) break;
        }
    }
    catherine_save_mouse_select(c, menu, 0, 0);
}

static inline uint32_t catherine_save_mouse_input(Cpu *c, uint64_t menu, uint32_t pressed) {
    const uint32_t confirm = 1u << 13, back = 1u << 14;
    catherine_config_setting(PC_CURSOR_HIDDEN, 0);
    if (catherine_save_mouse.menu != menu) catherine_save_mouse_begin(menu);
    const unsigned moves = (unsigned)catherine_pc_mouse(2);
    const unsigned clicks = (unsigned)catherine_pc_mouse(3);
    if (!catherine_save_mouse.list_ready) {
        catherine_save_mouse.list_ready = 1;
        catherine_save_mouse.wheel = catherine_pc_mouse(5);
        catherine_save_mouse.wheel_pending = 0;
        catherine_save_mouse.wheel_hover_lock = 0;
        catherine_save_mouse.moves = moves; catherine_save_mouse.clicks = clicks;
        catherine_save_mouse.suppress_click = catherine_config_setting(PC_RAW_KEY, 0) == 1;
        return catherine_save_mouse.suppress_click ? pressed & ~confirm : pressed;
    }
    const int moved = moves != catherine_save_mouse.moves;
    const int clicked = clicks != catherine_save_mouse.clicks;
    catherine_save_mouse.moves = moves;
    catherine_save_mouse.clicks = clicks;
    if (catherine_config_setting(PC_RAW_KEY, 0) > 1 || catherine_config_setting(PC_RAW_PAD, 0))
        catherine_save_mouse.suppress_click = 0;
    if (catherine_save_mouse.suppress_click) {
        const uint64_t pad = LD64(0x7000cc9070ull);
        const uint32_t held = pad ? LD32(pad) : 0;
        if (!((held | pressed) & confirm)) catherine_save_mouse.suppress_click = 0;
        pressed &= ~confirm;
    }
    if (catherine_config_setting(PC_RAW_KEY, 0) == 1) pressed &= ~confirm;
    const int wheel = catherine_pc_mouse(5);
    const int delta = (int32_t)((uint32_t)wheel - (uint32_t)catherine_save_mouse.wheel);
    catherine_save_mouse.wheel = wheel;
    if (delta) {
        catherine_save_mouse.wheel_hover_lock = 1;
        catherine_save_mouse.wheel_x = catherine_pc_mouse(0);
        catherine_save_mouse.wheel_y = catherine_pc_mouse(1);
    }
    if (catherine_pc_mouse(4)) {
        // Preserve partial detents and drain rapid scrolling one five-slot page per update.
        const int bounded = delta > 2400 ? 2400 : delta < -2400 ? -2400 : delta;
        catherine_save_mouse.wheel_pending += bounded;
        if (catherine_save_mouse.wheel_pending > 2400) catherine_save_mouse.wheel_pending = 2400;
        if (catherine_save_mouse.wheel_pending < -2400) catherine_save_mouse.wheel_pending = -2400;
        if (catherine_save_mouse.wheel_pending >= 120 || catherine_save_mouse.wheel_pending <= -120) {
            Cpu call = *c; call.x[0] = menu;
            const int up = catherine_save_mouse.wheel_pending > 0;
            catherine_save_mouse.wheel_pending += up ? -120 : 120;
            call.x[0] = 0xbd3; f_7000277ff0(&call);
            call = *c; call.x[0] = menu;
            catherine_save_mouse_page(c, menu, up);
            return pressed & ~confirm;
        }
    } else catherine_save_mouse.wheel_pending = 0;
    // Scrolling owns selection until a deliberate pointer move or click.
    // Small wheel-hand jitter must not reselect the card under the pointer.
    if (catherine_save_mouse.wheel_hover_lock && !clicked) {
        const int dx = catherine_pc_mouse(0) - catherine_save_mouse.wheel_x;
        const int dy = catherine_pc_mouse(1) - catherine_save_mouse.wheel_y;
        if (!moved || (dx > -8 && dx < 8 && dy > -8 && dy < 8)) return pressed;
        catherine_save_mouse.wheel_hover_lock = 0;
    } else if (clicked) catherine_save_mouse.wheel_hover_lock = 0;
    if (!moved && !clicked) return pressed;
    const int inside = catherine_pc_mouse(4);
    const int x = catherine_pc_mouse(0), y = catherine_pc_mouse(1);
    const int row = inside ? catherine_save_mouse_row(x, y) : -1;
    if (row >= 0) catherine_save_mouse_select(c, menu, (unsigned)row, 1);
    if (clicked) {
        catherine_save_mouse.suppress_click = 1;
        pressed &= ~confirm;
        if (row >= 0) pressed |= confirm;
        else if (inside && x >= 325 && x <= 495 && y >= 990 && y <= 1060) pressed |= back;
        else if (inside && x >= 90 && x <= 280 && y >= 990 && y <= 1060) pressed |= confirm;
    }
    return pressed;
}
