#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

static uint32_t catherine_app_elapsed_step(uint32_t native_bits, uint32_t nominal_bits,
                                          uint32_t framework_bits) {
    float native, nominal, framework;
    memcpy(&native, &native_bits, sizeof native);
    memcpy(&nominal, &nominal_bits, sizeof nominal);
    memcpy(&framework, &framework_bits, sizeof framework);

    // Preserve pauses, unusual time modes, and the native step on invalid or
    // unusually long samples. No elapsed time is caught up after a stall.
    if (!isfinite(native) || native <= 0.0f || !isfinite(nominal) || nominal <= 0.0f ||
        !isfinite(framework) || framework < 0.0f || framework > 0.100f)
        return native_bits;

    float scaled = (float)((double)native * ((double)framework / nominal));
    if (!isfinite(scaled)) return native_bits;
    uint32_t scaled_bits;
    memcpy(&scaled_bits, &scaled, sizeof scaled_bits);
    return scaled_bits;
}
