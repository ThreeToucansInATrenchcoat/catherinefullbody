#pragma once

#include "timing_clock.h"

static inline unsigned catherine_phone_mail_panel_tick(uint64_t caller) {
    if (LD8(0x7000d06360ull)&1u) return 1;
    switch (caller) {
    case 0x700000ced4ull:
    case 0x700000cf8cull:
    case 0x7000018408ull:
    case 0x7000018414ull:
    case 0x70000184a0ull:
    case 0x70000184dcull:
    case 0x7000018b94ull:
    case 0x7000018bacull:
        return catherine_timing_tick();
    default:
        return 1;
    }
}
