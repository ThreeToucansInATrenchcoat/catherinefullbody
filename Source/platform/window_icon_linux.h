#pragma once
#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
extern const unsigned char catherine_game_icon_start[], catherine_game_icon_end[];
#ifdef __cplusplus
}
#endif

static void catherine_linux_video_driver(void) {
    if (!getenv("SDL_VIDEODRIVER") && getenv("WAYLAND_DISPLAY") && getenv("DISPLAY"))
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11,wayland");
}

typedef struct {
    const unsigned char *bytes;
    size_t remaining;
} CatherineIconPng;

static cairo_status_t catherine_read_icon_png(void *data, unsigned char *out, unsigned int size) {
    CatherineIconPng *png = (CatherineIconPng *)data;
    if (size > png->remaining) return CAIRO_STATUS_READ_ERROR;
    memcpy(out, png->bytes, size);
    png->bytes += size;
    png->remaining -= size;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_surface_t *catherine_game_icon_png(void) {
    CatherineIconPng png = {catherine_game_icon_start,
        (uintptr_t)catherine_game_icon_end - (uintptr_t)catherine_game_icon_start};
    return cairo_image_surface_create_from_png_stream(catherine_read_icon_png, &png);
}

static unsigned catherine_straight_icon_channel(uint32_t pixel, unsigned shift, unsigned alpha) {
    unsigned value = (((pixel >> shift) & 255u) * 255u + alpha / 2) / alpha;
    return value > 255u ? 255u : value;
}

static void catherine_set_window_icon(SDL_Window *window, cairo_surface_t *png) {
    if (cairo_surface_status(png) != CAIRO_STATUS_SUCCESS) return;
    int width = cairo_image_surface_get_width(png);
    int height = cairo_image_surface_get_height(png);
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (icon && SDL_LockSurface(icon) == 0) {
        for (int y = 0; y < height; ++y) {
            const uint32_t *source = (const uint32_t *)(cairo_image_surface_get_data(png) + y * cairo_image_surface_get_stride(png));
            uint32_t *dest = (uint32_t *)((unsigned char *)icon->pixels + y * icon->pitch);
            for (int x = 0; x < width; ++x) {
                uint32_t pixel = source[x], alpha = pixel >> 24;
                dest[x] = alpha ? (alpha << 24) |
                    (catherine_straight_icon_channel(pixel, 16, alpha) << 16) |
                    (catherine_straight_icon_channel(pixel, 8, alpha) << 8) |
                    catherine_straight_icon_channel(pixel, 0, alpha) : 0;
            }
        }
        SDL_UnlockSurface(icon);
        SDL_SetWindowIcon(window, icon);
    }
    SDL_FreeSurface(icon);
}
