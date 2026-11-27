#pragma once
#include <stdint.h>

// Runtime presentation target and startup-only internal rendering height.
#ifdef __cplusplus
extern "C" {
#endif
int catherine_requested_refresh_rate(void);
unsigned catherine_internal_render_height(void);
// Supplied by Catherine's timing hooks when rendering can exceed 30 FPS.
int catherine_fullscreen_movie_active(void);
int catherine_requested_movie_interpolation(void);
// Startup renderer choice; false uses the CPU skinning fallback.
int catherine_native_gpu_skinning_enabled(void);
int catherine_guest_precise_sleep(int64_t ns);
#ifdef __cplusplus
}
#endif
