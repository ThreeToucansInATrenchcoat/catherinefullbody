#pragma once

#include "app_elapsed.h"
#include "timing_clock.h"

static inline uint32_t catherine_phone_elapsed_step(uint32_t step) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u))
        return step;
    const uint32_t sign = step & 0x80000000u;
    return sign | catherine_app_elapsed_step(step & 0x7fffffffu, 0x3d088889u, LD32(0x70031cc1e8ull));
}
