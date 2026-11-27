#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// 0 = Auto; fixed sizes preserve an explicit user's choice across settings saves.
int catherine_shadow_resolution_setting(void);
unsigned catherine_resolution_applied_height(void);
#ifdef __cplusplus
}
#endif
static inline unsigned catherine_shadow_resolution_for_height(int setting, unsigned height) {
    if (setting == 2048 || setting == 4096 || setting == 8192) return (unsigned)setting;
    return height > 1080 ? 8192u : 4096u;
}
static inline unsigned catherine_shadow_resolution_with_limit(unsigned requested, unsigned limit) {
    while (requested > limit && requested > 2048) requested = requested > 4096 ? 4096 : 2048;
    return requested;
}
