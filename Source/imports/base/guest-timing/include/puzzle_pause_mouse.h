#pragma once
#include "menu_mouse_pc.h"
extern void f_7000088650(Cpu *c);
extern void f_700007ff60(Cpu *c);
extern void f_700021a0e0(Cpu *c);
extern void f_70001af290(Cpu *c);
extern void f_700021a310(Cpu *c);
static CatherineMenuPointer catherine_puzzle_pause_pointer;
static uint64_t catherine_puzzle_pause_view;
static uint64_t catherine_puzzle_pause_owner;
static unsigned catherine_pause_mouse_consumed;
static unsigned catherine_pause_submenu;
static uint64_t catherine_pause_pending_owner;
static unsigned catherine_pause_pending_submenu, catherine_pause_pending_row;
/* A host click arrives before its emulated pad edge. Keep that edge out of
 * subsequent native handlers, especially confirmations whose default is No. */
static uint32_t catherine_pause_filter_click(uint32_t pressed) {
    const unsigned click = (unsigned)catherine_pc_mouse(3);
    const int raw = catherine_config_setting(PC_RAW_KEY, 0);
    if (raw == 1 || catherine_pc_mouse(7) ||
        (click && click == catherine_pause_mouse_consumed && !raw &&
         !catherine_config_setting(PC_RAW_PAD, 0))) pressed &= ~0x2000u;
    return pressed;
}

static void catherine_puzzle_pause_hide(uint64_t menu) {
    if (catherine_puzzle_pause_owner != menu) return;
    catherine_menu_pointer_show(&catherine_puzzle_pause_pointer, 1, 0);
    catherine_puzzle_pause_owner = 0;
    catherine_pause_pending_owner = 0;
}
static void catherine_puzzle_pause_update(uint64_t menu) {
    int visible = LD8(menu + 0x34c) == 3 && LD8(menu + 0x34d) < 5;
    if (!visible) { catherine_puzzle_pause_hide(menu); return; }
    if (catherine_puzzle_pause_owner != menu) {
        catherine_menu_pointer_sync(&catherine_puzzle_pause_pointer);
        catherine_puzzle_pause_owner = menu;
        catherine_pause_submenu = LD8(menu + 0x340);
    }
    catherine_menu_pointer_show(&catherine_puzzle_pause_pointer, 1, 1);
    unsigned submenu = LD8(menu + 0x340);
    if (LD8(menu + 0x34d) != 1 || submenu != catherine_pause_submenu) {
        catherine_menu_pointer_sync(&catherine_puzzle_pause_pointer);
        catherine_pause_pending_owner = 0;
    }
    catherine_pause_submenu = submenu;
}

/* Project native label transforms, so hit rows follow the angled 3-D panel
 * at any resolution. Node TRS values are the same ones used by the renderer. */
static int catherine_pause_project_point(Cpu *c, uint64_t node, float local_x, float local_z, float *x, float *y) {
    float p[3] = {local_x, 0, local_z};
    for (unsigned depth = 0; node && depth < 64; ++depth) {
        float q[4], v[3], t[3];
        for (unsigned i = 0; i < 4; ++i) q[i] = catherine_menu_float(node + 0xb0 + i * 4);
        for (unsigned i = 0; i < 3; ++i) v[i] = p[i] * catherine_menu_float(node + 0xc0 + i * 4);
        t[0] = 2 * (q[1] * v[2] - q[2] * v[1]);
        t[1] = 2 * (q[2] * v[0] - q[0] * v[2]);
        t[2] = 2 * (q[0] * v[1] - q[1] * v[0]);
        p[0] = v[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1];
        p[1] = v[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2];
        p[2] = v[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0];
        for (unsigned i = 0; i < 3; ++i) p[i] += catherine_menu_float(node + 0xa0 + i * 4);
        uint64_t parent = LD64(node + 8);
        if (parent == node) break;
        node = parent;
        if (depth == 63) return 0;
    }
    for (unsigned i = 0; i < 3; ++i) if (!isfinite(p[i])) return 0;
    /* Reserve scratch below the caller's published SP; guest callees use the
     * space below it, never the current input handler's saved registers. */
    Cpu call = *c; call.sp = (c->sp - 0x60) & ~15ull;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t bits; memcpy(&bits, &p[i], 4); ST32(call.sp + 0x10 + i * 4, bits);
    }
    call.x[0] = catherine_puzzle_pause_view;
    call.x[1] = call.sp; call.x[2] = call.sp + 0x30; call.x[3] = 1;
    f_700007ff60(&call);
    *x = catherine_menu_float(((c->sp - 0x60) & ~15ull) + 0x38);
    *y = catherine_menu_float(((c->sp - 0x60) & ~15ull) + 0x3c);
    return (call.x[0] & 1) && isfinite(*x) && isfinite(*y);
}
static int catherine_pause_project(Cpu *c, uint64_t node, float local_x, float *x, float *y) {
    return catherine_pause_project_point(c, node, local_x, 0, x, y);
}

/* Hit the actual two label quads from E_pause.GMD. Yes and No share a node;
 * checkpoint/area choices have separate nodes. Keep the panel's 3-D angle. */
static int catherine_pause_confirmation_row(Cpu *c, uint64_t menu, int x, int y) {
    static const float quads[4][4][2] = {
        {{-41.4664f,20.4197f},{-40.2002f,-2.2457f},{40.2002f,2.2457f},{38.9341f,24.9111f}},
        {{-39.7669f,-5.8732f},{-39.4866f,-28.5722f},{41.0331f,-27.5780f},{40.7528f,-4.8790f}},
        {{-101.7080f,6.5109f},{-100.6459f,-16.0409f},{101.7080f,-6.5109f},{100.6459f,16.0409f}},
        {{-101.5134f,9.0524f},{-101.0156f,-13.5189f},{101.5134f,-9.0524f},{101.0156f,13.5189f}}
    };
    if (!catherine_puzzle_pause_view || !LD64(menu + 8)) return -1;
    unsigned retry = LD8(menu + 0x340) == 2;
    for (unsigned row = 0; row < 2; ++row) {
        const char *name = retry ? (row ? "Checkpoint" : "Area") : "yes_no";
        Cpu call = *c; call.sp = (c->sp - 0x80) & ~15ull;
        for (unsigned i = 0; i <= strlen(name); ++i) ST8(call.sp + 0x50 + i, name[i]);
        call.x[0] = LD64(menu + 8); call.x[1] = call.sp + 0x50;
        f_7000088650(&call);
        if (!call.x[0]) continue;
        float px[4], py[4]; int valid = 1;
        const float (*quad)[2] = quads[row + retry * 2];
        for (unsigned i = 0; i < 4; ++i)
            if (!catherine_pause_project_point(c, call.x[0], quad[i][0], quad[i][1], &px[i], &py[i])) valid = 0;
        if (!valid) continue;
        int positive = 0, negative = 0; float area = 0;
        for (unsigned i = 0; i < 4; ++i) {
            unsigned j = (i + 1) % 4;
            float cross = (px[j]-px[i])*(y-py[i]) - (py[j]-py[i])*(x-px[i]);
            positive |= cross > 0; negative |= cross < 0;
            area += px[i]*py[j] - px[j]*py[i];
        }
        if (fabsf(area) > 8 && !(positive && negative)) return row;
    }
    return -1;
}

/* Direction bits 16/17 route through the original highlight animations.
 * If a click also changes rows, confirm on the next update after selection. */
static uint32_t catherine_pause_confirmation_input(uint64_t menu, int row, uint32_t pressed) {
    pressed = catherine_pause_filter_click(pressed);
    unsigned events = catherine_menu_pointer_poll(&catherine_puzzle_pause_pointer, &pressed);
    unsigned submenu = LD8(menu + 0x340);
    unsigned selected = LD32(menu + (submenu == 1 ? 0x344 : 0x348));
    if (events & 4) {
        catherine_pause_pending_owner = 0;
        return (pressed & ~0x2000u) | 0x4000u;
    }
    if (catherine_pause_pending_owner) {
        int accept = catherine_pause_pending_owner == menu &&
            catherine_pause_pending_submenu == submenu && catherine_pause_pending_row == selected;
        catherine_pause_pending_owner = 0;
        if (accept) return (pressed & ~0x4000u) | 0x2000u;
    }
    if (!(events & 3)) return pressed;
    if (events & 2) {
        catherine_pause_mouse_consumed = (unsigned)catherine_pc_mouse(3);
        pressed &= ~0x2000u;
    }
    if (row < 0) return pressed;
    if (selected != (unsigned)row) {
        if (events & 2) {
            catherine_pause_pending_owner = menu;
            catherine_pause_pending_submenu = submenu;
            catherine_pause_pending_row = row;
        }
        return (pressed & ~0x2000u) | (row ? 0x20000u : 0x10000u);
    }
    if (events & 2) pressed |= 0x2000u;
    return pressed;
}
static uint32_t catherine_pause_confirmation_mouse(Cpu *c, uint64_t menu, uint32_t pressed) {
    int row = catherine_pc_mouse(4) ? catherine_pause_confirmation_row(c, menu,
        catherine_pc_mouse(0), catherine_pc_mouse(1)) : -1;
    return catherine_pause_confirmation_input(menu, row, pressed);
}
static int catherine_pause_mouse_row(Cpu *c, uint64_t menu, unsigned variant, int x, int y) {
    /* Optional-mode pause uses a fixed, angled screen-space panel. Match its
     * five visible rows directly on the normalized 1920x1080 canvas. Looking
     * up every model node made the whole menu depend on hidden/disabled art. */
    if (variant == 1) {
        const float panel_y = y + .12f * (x - 1000);
        return catherine_menu_rows(x, panel_y, 780, 1240, 360, 48, 5);
    }
    if (!catherine_puzzle_pause_view || variant > 2 || !LD64(menu + 8)) return -1;
    unsigned count = variant == 2 ? 4 : 5;
    float px[5], py[5], ax[5], ay[5];
    uint64_t names = LD64(0x7000ccb5d0ull);
    if (!names) return -1;
    for (unsigned row = 0; row < count; ++row) {
        unsigned id = LD8(0x70007cd1e4ull + variant * 25 + row);
        Cpu call = *c; call.x[0] = LD64(menu + 8); call.x[1] = LD64(names + id * 8);
        f_7000088650(&call);
        if (!call.x[0] || !catherine_pause_project(c, call.x[0], 0, &px[row], &py[row]) ||
            !catherine_pause_project(c, call.x[0], 1, &ax[row], &ay[row])) return -1;
        float length = hypotf(ax[row] - px[row], ay[row] - py[row]);
        if (length < .0001f) return -1;
        ax[row] = (ax[row] - px[row]) / length; ay[row] = (ay[row] - py[row]) / length;
    }
    for (unsigned row = 0; row < count; ++row) {
        unsigned next = row + 1 < count ? row + 1 : row - 1;
        float pitch = fabsf((px[next] - px[row]) * -ay[row] + (py[next] - py[row]) * ax[row]);
        float across = (x - px[row]) * ax[row] + (y - py[row]) * ay[row];
        float down = (x - px[row]) * -ay[row] + (y - py[row]) * ax[row];
        if (pitch >= 10 && pitch <= 200 && fabsf(down) < pitch * .5f && fabsf(across) < pitch * 4.5f) return row;
    }
    return -1;
}
static uint32_t catherine_puzzle_pause_mouse(Cpu *c, uint64_t menu, unsigned variant, uint32_t pressed) {
    unsigned events = catherine_menu_pointer_poll(&catherine_puzzle_pause_pointer, &pressed);
    if (events & 4) return (pressed & ~0x2000u) | 0x4000u;
    if (!(events & 3)) return pressed;
    int row = catherine_pc_mouse(4) ? catherine_pause_mouse_row(c, menu, variant,
        catherine_pc_mouse(0), catherine_pc_mouse(1)) : -1;
    if (events & 2) pressed &= ~0x2000u;
    if (row < 0) return pressed;
    /* The native Skip predicate determines whether its grey label is enabled. */
    if (variant == 0 && row == 3) {
        Cpu call = *c; f_700021a0e0(&call);
        if (!(call.x[0] & 1)) return pressed;
    }
    if (variant == 1 && row == 1) {
        Cpu call = *c; f_70001af290(&call);
        if (call.x[0] & 1) return pressed;
    }
    if (LD8(menu + 0x18) != (unsigned)row) {
        ST8(menu + 0x18, row); ST8(menu + 0x35c, 2);
        Cpu call = *c; call.x[0] = menu; f_700021a310(&call);
        catherine_menu_pointer_sound(c);
    }
    if (events & 2) {
        catherine_pause_mouse_consumed = (unsigned)catherine_pc_mouse(3);
        pressed |= 0x2000u;
    }
    return pressed;
}
