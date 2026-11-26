#pragma once
#include <stdint.h>
#include "catherine_resolution_live.h"

#ifdef __cplusplus
extern "C" {
#endif
// Startup fallback; live selections are frozen and applied by the guest boundary bridge.
// Zero preserves the game's stock handheld/docked resolution policy.
unsigned catherine_internal_render_height(void);
unsigned catherine_upscaled_render_height(unsigned output_height);
#ifdef __cplusplus
}
#endif

static inline void catherine_override_render_dimensions(uint64_t* width, uint64_t* height) {
    const unsigned requested = catherine_resolution_selected_height();
    if(requested>=360 && requested<=2160 && requested%18u==0) {
            *width = requested / 9u * 16u;
            *height = requested;
    }
}
