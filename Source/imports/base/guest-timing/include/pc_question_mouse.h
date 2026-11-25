#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "config_pc_host.h"
#include "pc_dialogs.h"

extern unsigned catherine_pc_display_dialog;
extern void f_7000072ef0(Cpu *c);
extern void f_7000277ff0(Cpu *c);
extern void f_70002f38a0(Cpu *c);

static struct {
    unsigned move_serial, click_serial, ready, right_click_serial;
} catherine_pc_question_mouse;

/* Use the same authored choice count as the native Up/Down handler. This
 * includes New Game and other Yes/No questions, not just Config questions.
 * One-button notices and questions with other layouts remain untouched. */
static inline int catherine_pc_question_mouse_eligible(unsigned type) {
    return (int16_t)LD16(0x70007a1210ull + (uint64_t)type * 0x14ull + 4) == 2;
}

/* Call when an eligible question opens, before its opening animation.
 * The click which opened it belongs to the previous screen. */
static inline void catherine_pc_question_mouse_begin(void) {
    catherine_pc_question_mouse.move_serial = (unsigned)catherine_pc_mouse(2);
    catherine_pc_question_mouse.click_serial = (unsigned)catherine_pc_mouse(3);
    catherine_pc_question_mouse.right_click_serial = (unsigned)catherine_pc_mouse(6);
    catherine_pc_question_mouse.ready = 1;
}

static inline float catherine_pc_question_mouse_float(uint64_t address) {
    const uint32_t bits = LD32(address);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static inline int catherine_pc_question_mouse_row(uint64_t dialog, int x, int y) {
    /* f_7000071390's native choice brush uses x=565, y=191+base+23*row,
     * scaled by 1.5. The 156x40 sprite's visual centre is 19 source pixels
     * below its origin. The base at +0x5c accounts for native message lines.
     * Non-overlapping half-pitch hit rows follow the Yes/No text centres.
     * The authored 720p captures independently verify these sprite bounds. */
    const float base = catherine_pc_question_mouse_float(dialog + 0x5c);
    if (!isfinite(base) || base < -100 || base > 500) return -1;
    if (x < 565.f * 1.5f || x > (565.f + 156.f) * 1.5f) return -1;
    const float first = (191.f + base + 19.f) * 1.5f;
    const float pitch = 23.f * 1.5f;
    for (int row = 0; row < 2; ++row) {
        const float centre = first + row * pitch;
        if (y >= centre - pitch * .5f && y < centre + pitch * .5f) return row;
    }
    return -1;
}

static inline void catherine_pc_question_mouse_select(Cpu *c, uint64_t dialog, unsigned row) {
    const unsigned old = LD32(dialog + 0xec);
    if (old == row || old >= 2 || row >= 2) return;

    /* Mirror the native Up/Down branch, including its brush fade and cursor
     * refresh. The original confirm branch remains responsible for the result. */
    ST32(dialog + 0x128 + old * 4, 1);
    ST32(dialog + 0x138 + old * 4, 0);
    ST32(dialog + 0xec, row);
    ST32(dialog + 0xf8 + row * 4, 1);
    ST32(dialog + 0x108 + row * 4, 0);
    Cpu call = *c;
    f_70002f38a0(&call);
    ST32(dialog + 0x118 + row * 4, (uint32_t)call.x[0] % 100);
    ST32(dialog + 0x128 + row * 4, 0);
    ST32(dialog + 0x138 + row * 4, 0);
    call = *c;
    call.x[0] = dialog;
    call.x[1] = row;
    f_7000072ef0(&call);
    call = *c;
    call.x[0] = 0xbba;
    f_7000277ff0(&call);
}

/* Hook only the native confirm word immediately before its bit-13 check.
 * Motion changes the selection once, clicks confirm their own hit row, and
 * stationary pointers leave subsequent keyboard/controller navigation alone. */
static inline uint32_t catherine_pc_question_mouse_input(Cpu *c, uint64_t dialog, uint32_t pressed) {
    if (!catherine_pc_question_mouse_eligible(LD32(dialog + 0xc))) return pressed;
    if (catherine_config_pc_is_open() || catherine_pc_question || catherine_pc_display_dialog)
        catherine_config_pc_question_pointer_consumed();

    const uint32_t confirm = 1u << 13;
    /* LMB can also arrive through the ordinary Grab binding. It must never
     * confirm the default selection without a fresh, in-bounds click below. */
    const int raw_key = catherine_config_setting(PC_RAW_KEY, 0);
    if (raw_key == 1) pressed &= ~confirm;
    if (raw_key == 2) pressed &= ~(confirm | 0x4008u);
    if (!catherine_pc_question_mouse.ready) {
        catherine_pc_question_mouse_begin();
        return pressed;
    }
    const unsigned moves = (unsigned)catherine_pc_mouse(2);
    const unsigned clicks = (unsigned)catherine_pc_mouse(3);
    const unsigned right_clicks = (unsigned)catherine_pc_mouse(6);
    const int moved = moves != catherine_pc_question_mouse.move_serial;
    const int clicked = clicks != catherine_pc_question_mouse.click_serial;
    const int right_clicked = right_clicks != catherine_pc_question_mouse.right_click_serial;
    catherine_pc_question_mouse.move_serial = moves;
    catherine_pc_question_mouse.click_serial = clicks;
    catherine_pc_question_mouse.right_click_serial = right_clicks;
    /* RMB is an unconditional No action, independent of pointer position,
     * highlighted choice and user-configured gameplay bindings. */
    if (right_clicked) {
        pressed &= ~(confirm | 0x4008u);
        catherine_pc_question_mouse_select(c, dialog, 1);
        if (LD32(dialog + 0xe8) >= 15 && !LD32(dialog + 0xf0)) pressed |= confirm;
        return pressed;
    }
    if (!moved && !clicked) return pressed;

    const int row = catherine_pc_mouse(4) ? catherine_pc_question_mouse_row(
        dialog, catherine_pc_mouse(0), catherine_pc_mouse(1)) : -1;
    if (clicked) pressed &= ~confirm;
    if (row < 0) return pressed;
    catherine_pc_question_mouse_select(c, dialog, (unsigned)row);
    /* Keep the authored opening delay. A click during it is consumed rather
     * than queued to unexpectedly accept the question when it becomes ready. */
    if (clicked && LD32(dialog + 0xe8) >= 15 && !LD32(dialog + 0xf0)) pressed |= confirm;
    return pressed;
}
