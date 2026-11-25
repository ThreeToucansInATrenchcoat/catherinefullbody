#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

// The spinner's color ramp has 16 phases and advances every two 30 Hz ticks.
enum { CATHERINE_SAVE_SPINNER_CYCLE_TICKS = 32 };

static inline uint32_t catherine_save_spinner_clock(void) {
    const uint64_t singleton = LD64(0x7000cc9f98ull);
    const uint64_t app = singleton ? LD64(singleton) : 0;
    return app ? LD32(app + 0x6cull) : 0;
}

static inline float catherine_save_spinner_ticks(uint64_t spinner) {
    const uint32_t start_bits = LD32(spinner + 0x44ull);
    const uint32_t current_bits = catherine_save_spinner_clock();
    float start, current;
    memcpy(&start, &start_bits, sizeof start);
    memcpy(&current, &current_bits, sizeof current);
    if (!isnormal(start) || !isfinite(current) || start < 0.0f || current < start)
        return -1.0f;
    return (current - start) * 30.0f;
}

static inline int catherine_save_spinner_in_cycle(uint64_t spinner) {
    const float ticks = catherine_save_spinner_ticks(spinner);
    return ticks >= 0.0f && ticks < CATHERINE_SAVE_SPINNER_CYCLE_TICKS;
}
