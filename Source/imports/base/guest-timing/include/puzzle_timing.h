#pragma once

#include <stdlib.h>
#include <string.h>
#include "timing_clock.h"

static inline int catherine_pbd_hold_dispatch(void) {
    return !catherine_timing_tick();
}

/* Preserve the delay between removal of the old UI and the new Autoplay
   countdown. The caller still handles UI lifecycle and I/O on every frame. */
static inline int catherine_hold_autoplay_start_tick(void) {
    return !catherine_timing_tick();
}

/* Render the current countdown state on intermediate frames. */
static inline int catherine_hold_autoplay_ui_tick(void) {
    return !catherine_timing_tick();
}
