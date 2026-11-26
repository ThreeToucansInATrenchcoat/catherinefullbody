#pragma once
#include <stdint.h>
#include "catherine_resolution_live.h"

#ifdef __cplusplus
extern "C" {
#endif
// Startup fallback; live selections are frozen and applied by the guest boundary bridge.
// Zero preserves the game's stock handheld/docked resolution policy.
unsigned catherine_internal_render_height(void);
#ifdef __cplusplus
}
#endif

static inline void catherine_override_render_dimensions(uint64_t* width, uint64_t* height) {
    const unsigned requested = catherine_resolution_selected_height();
    switch (requested) {
        case 720: case 1080: case 1440: case 2160:
            *width = requested / 9u * 16u;
            *height = requested;
            break;
        default: break;
    }
}
