#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Coordinates and font heights use the 1920 x 1080 game UI canvas.
   Colors are packed RRGGBBAA. Text callbacks consume the string synchronously. */
typedef void (*CatherineOverlayRect)(void *ctx,float x,float y,float width,float height,uint32_t rgba);
typedef void (*CatherineOverlayText)(void *ctx,float x,float y,float height,const char *utf8,uint32_t rgba);
void catherine_dev_overlay_frame(void *ctx,CatherineOverlayRect rect,CatherineOverlayText text);
#ifdef __cplusplus
}
#endif
