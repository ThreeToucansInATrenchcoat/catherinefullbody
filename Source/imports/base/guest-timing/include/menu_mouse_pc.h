#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "config_pc_host.h"

/* Each live menu owns one bit. Camera updates cannot hide its pointer. */
#ifdef __cplusplus
extern "C" {
#endif
void catherine_menu_cursor_set(unsigned owner, int visible);
int catherine_menu_cursor_visible(void);
#ifdef __cplusplus
}
#endif

typedef struct {
    unsigned move, click, right, active, released;
} CatherineMenuPointer;

static inline void catherine_menu_pointer_sync(CatherineMenuPointer *p) {
    p->move = (unsigned)catherine_pc_mouse(2);
    p->click = (unsigned)catherine_pc_mouse(3);
    p->right = (unsigned)catherine_pc_mouse(6);
    p->released = !catherine_pc_mouse(7);
}
static inline void catherine_menu_pointer_show(CatherineMenuPointer *p, unsigned owner, int visible) {
    if (visible && !p->active) catherine_menu_pointer_sync(p);
    if (visible || p->active) {
        catherine_menu_cursor_set(owner, visible);
        catherine_config_setting(PC_CURSOR_HIDDEN, !visible);
    }
    p->active = visible;
}
/* Poll once per input handler. A held opening click never accepts a new menu. */
static inline unsigned catherine_menu_pointer_poll(CatherineMenuPointer *p, uint32_t *pressed) {
    unsigned move = (unsigned)catherine_pc_mouse(2), click = (unsigned)catherine_pc_mouse(3);
    unsigned right = (unsigned)catherine_pc_mouse(6), events = 0;
    int raw = catherine_config_setting(PC_RAW_KEY, 0);
    if (raw == 1 || catherine_pc_mouse(7)) *pressed &= ~0x2000u;
    if (raw == 2) *pressed &= ~0x6008u;
    if (move != p->move) events |= 1;
    if (click != p->click && p->released) events |= 2;
    if (right != p->right) events |= 4;
    p->move = move; p->click = click; p->right = right;
    if (!catherine_pc_mouse(7)) p->released = 1;
    return events;
}
static inline float catherine_menu_float(uint64_t a) {
    uint32_t b = LD32(a); float f; memcpy(&f, &b, 4); return f;
}
static inline int catherine_menu_rows(int x, int y, float left, float right,
                                      float first, float pitch, unsigned count) {
    if (x < left || x > right) return -1;
    for (unsigned i = 0; i < count; ++i)
        if (y >= first + pitch * (i - .5f) && y < first + pitch * (i + .5f)) return (int)i;
    return -1;
}
extern void f_7000277ff0(Cpu *c);
static inline void catherine_menu_pointer_sound(Cpu *c) {
    Cpu call = *c; call.x[0] = 0xbba; f_7000277ff0(&call);
}
