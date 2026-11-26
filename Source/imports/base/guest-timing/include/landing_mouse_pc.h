#pragma once
#include "menu_mouse_pc.h"
static CatherineMenuPointer catherine_landing_pointer;

static uint32_t catherine_landing_mouse(Cpu *c, uint64_t menu, uint32_t pressed, int question) {
    unsigned events = catherine_menu_pointer_poll(&catherine_landing_pointer, &pressed);
    if (events & 4) return (pressed & ~0x2000u) | 0x4000u;
    if (!(events & 3)) return pressed;
    int row = catherine_pc_mouse(4) ? catherine_menu_rows(catherine_pc_mouse(0), catherine_pc_mouse(1),
        (question ? 565.f : 428.f) * 1.5f, (question ? 721.f : 858.f) * 1.5f,
        ((question ? 240.f : 186.f) + 19.f) * 1.5f, 23.f * 1.5f, question ? 2 : 4) : -1;
    if (events & 2) pressed &= ~0x2000u;
    if (row < 0) return pressed;
    unsigned selected = question ? 0x328 : 0x298;
    unsigned fade_in = question ? 0x334 : 0x2a4;
    unsigned in_age = question ? 0x33c : 0x2b4;
    unsigned seed = question ? 0x344 : 0x2c4;
    unsigned fade_out = question ? 0x34c : 0x2d4;
    unsigned out_age = question ? 0x354 : 0x2e4;
    unsigned old = LD32(menu + selected);
    if (old != (unsigned)row && old < (question ? 2u : 4u)) {
        ST32(menu + fade_out + old * 4, 1); ST32(menu + out_age + old * 4, 0);
        ST32(menu + selected, row);
        ST32(menu + fade_in + row * 4, 1); ST32(menu + in_age + row * 4, 0);
        ST32(menu + seed + row * 4, 75);
        ST32(menu + fade_out + row * 4, 0); ST32(menu + out_age + row * 4, 0);
        catherine_menu_pointer_sound(c);
    }
    if (events & 2) pressed |= 0x2000u;
    return pressed;
}
