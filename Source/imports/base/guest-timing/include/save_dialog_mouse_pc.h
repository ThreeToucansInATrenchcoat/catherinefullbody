#pragma once
#include <math.h>
#include <string.h>
#include "config_pc_host.h"

static struct {
    uint64_t widget;
    unsigned moves, clicks, kind, last_clock;
} catherine_save_dialog_mouse;

static inline void catherine_save_dialog_begin(uint64_t widget) {
    if (widget != catherine_pc_save_widget) return;
    memset(&catherine_save_dialog_mouse, 0, sizeof catherine_save_dialog_mouse);
}

static inline float catherine_save_dialog_float(uint64_t address) {
    uint32_t bits = LD32(address);
    float value; memcpy(&value, &bits, sizeof value); return value;
}

/* Only the Save/Load component's embedded small dialog owns this pointer path.
 * Reuse its selected byte; native code still applies its delay, sound and close. */
static inline uint32_t catherine_save_dialog_input(uint64_t widget, uint32_t pressed, int choices) {
    if (!widget || widget != catherine_pc_save_widget) return pressed;
    catherine_config_setting(PC_CURSOR_HIDDEN, 0);
#ifdef CATHERINE_PHONE_SAVE_DIALOG
    catherine_phone_save_mouse.list_ready = 0;
#else
    catherine_save_mouse.list_ready = 0;
#endif
    const unsigned moves = (unsigned)catherine_pc_mouse(2), clicks = (unsigned)catherine_pc_mouse(3);
    const unsigned clock = LD32(widget + 8), kind = LD8(widget + 0x51);
    if (catherine_save_dialog_mouse.widget != widget ||
        catherine_save_dialog_mouse.kind != kind || clock < catherine_save_dialog_mouse.last_clock) {
        catherine_save_dialog_mouse.widget = widget;
        catherine_save_dialog_mouse.kind = kind;
        catherine_save_dialog_mouse.moves = moves;
        catherine_save_dialog_mouse.clicks = clicks;
    }
    catherine_save_dialog_mouse.last_clock = clock;
    const int moved = moves != catherine_save_dialog_mouse.moves;
    const int clicked = clicks != catherine_save_dialog_mouse.clicks;
    catherine_save_dialog_mouse.moves = moves;
    catherine_save_dialog_mouse.clicks = clicks;
    const uint32_t confirm = 1u << 13;
    if (catherine_config_setting(PC_RAW_KEY, 0) == 1 || clicked) pressed &= ~confirm;
    if (!moved && !clicked) return pressed;
#ifdef CATHERINE_PHONE_SAVE_DIALOG
    const float base_x = catherine_save_dialog_float(widget + 0x38);
    const float base_y = catherine_save_dialog_float(widget + 0x3c);
    const float first_y = (base_y + (choices ? 41.0f : 55.5f)) * 1.5f + 16.0f;
    const float pitch = 37.5f;
    const float left = (base_x + 6.0f) * 1.5f, right = (base_x + 441.0f) * 1.5f;
#else
    const float base_x = catherine_save_dialog_float(widget + 0x60);
    const float base_y = catherine_save_dialog_float(widget + 0x64);
    const unsigned layout = LD8(widget + 0x68);
    const float shift = layout != 1 && layout != 2 && LD64(widget + 0x20) ? 30.0f : 0.0f;
    const float first_y = 337.5f + (base_y + shift) * 1.5f;
    const float pitch = 45.0f;
    const float left = 283.0f + base_x * 1.5f, right = 503.0f + base_x * 1.5f;
#endif
    if (!isfinite(base_x) || !isfinite(base_y) || !catherine_pc_mouse(4)) return pressed;
    const int x = catherine_pc_mouse(0), y = catherine_pc_mouse(1);
    if (x < left || x > right) return pressed;
    for (unsigned row = 0; row < (choices ? 2u : 1u); ++row) {
        const float centre = first_y + (choices ? pitch * row : 0.0f);
        if (y < centre - 18 || y >= centre + 18) continue;
        if (choices) ST8(widget + 0x50, row);
        if (clicked && clock >= (choices ? 4u : 9u)) pressed |= confirm;
        break;
    }
    return pressed;
}
