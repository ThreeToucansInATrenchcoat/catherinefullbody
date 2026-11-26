#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "timing_clock.h"

static float catherine_menu_frame_step(uint32_t frame_delta_bits, int *scaled) {
    *scaled = catherine_high_refresh_active();
    if (!*scaled) return 1.0f;
    float delta;
    memcpy(&delta, &frame_delta_bits, sizeof delta);
    // Sub-millisecond frames are valid at unlimited FPS; zero pauses the timeline.
    if (!isfinite(delta) || delta < 0.0f || delta > 0.100f) return 1.0f;
    return delta * 30.0f;
}

static float catherine_menu_eased_position(float position, float end_velocity, float acceleration, float step) {
    // This extends the original discrete velocity-then-position update to a fractional step.
    return position + end_velocity * step + acceleration * step * (1.0f - step) * 0.5f;
}

static void catherine_menu_midpoint_step(float total, float remaining, float step,
                                         float acceleration, float *velocity, float *position) {
    // The integer update accelerates while the pre-update elapsed frame is <= total / 2.
    const float boundary = floorf(total * 0.5f) + 1.0f;
    const float rising = fminf(step, fmaxf(0.0f, remaining - (total - boundary)));
    const float falling = step - rising;
    if (rising > 0.0f) {
        *velocity += acceleration * rising;
        *position = catherine_menu_eased_position(*position, *velocity, acceleration, rising);
    }
    if (falling > 0.0f) {
        *velocity -= acceleration * falling;
        *position = catherine_menu_eased_position(*position, *velocity, -acceleration, falling);
    }
}

static float catherine_menu_mode1_terminal(float unit_step, float threshold) {
    const float initial = 0.5f;
    if (unit_step >= initial) return initial + unit_step;

    // Until the threshold, every native add uses the same 2^-24 float grid. A halfway
    // tie rounds to the same even increment on each add because the initial grid index is even.
    const float increment = nearbyintf(unit_step * 0x1p24f) * 0x1p-24f;
    if (increment == 0.0f) return initial;
    const double frames = ceil((double)(threshold - initial) / increment);
    const float previous = (float)(initial + (frames - 1.0) * increment);
    // The last add can cross 1.0, where the float spacing changes.
    return previous + unit_step;
}
