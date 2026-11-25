#pragma once
#include "window_icon_linux.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

enum { SETUP_WIDTH = 560, SETUP_HEIGHT = 230 };
static const SDL_Rect setup_cancel_button = {420, 165, 108, 36};

typedef struct {
    char stage[96], detail[560];
    int percent;
} CatherineSetupProgress;

static void catherine_setup_text(cairo_t *cr, const char *text, double x, double y, double size,
                                 cairo_font_weight_t weight) {
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, weight);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

static void catherine_draw_setup(SDL_Window *window, cairo_surface_t *icon,
                                 const CatherineSetupProgress *progress, int cancelling) {
    SDL_Surface *target = SDL_GetWindowSurface(window);
    if (!target) return;
    cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, target->w, target->h);
    cairo_t *cr = cairo_create(image);
    cairo_scale(cr, target->w / (double)SETUP_WIDTH, target->h / (double)SETUP_HEIGHT);
    cairo_set_source_rgb(cr, 0.98, 0.96, 0.94);
    cairo_paint(cr);
    if (cairo_surface_status(icon) == CAIRO_STATUS_SUCCESS) {
        cairo_save(cr);
        cairo_translate(cr, 28, 25);
        cairo_scale(cr, 50.0 / cairo_image_surface_get_width(icon), 50.0 / cairo_image_surface_get_height(icon));
        cairo_set_source_surface(cr, icon, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_set_source_rgb(cr, 0.22, 0.12, 0.18);
    catherine_setup_text(cr, cancelling ? "Cancelling setup…" : progress->stage, 96, 48, 20, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_source_rgb(cr, 0.35, 0.27, 0.32);
    cairo_save(cr);
    cairo_rectangle(cr, 96, 53, 432, 24);
    cairo_clip(cr);
    catherine_setup_text(cr, progress->detail, 96, 70, 12, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_restore(cr);
    cairo_set_source_rgb(cr, 0.89, 0.83, 0.85);
    cairo_rectangle(cr, 32, 111, 496, 11);
    cairo_fill(cr);
    if (progress->percent >= 0) {
        char percent[8];
        snprintf(percent, sizeof percent, "%d%%", progress->percent);
        cairo_set_source_rgb(cr, 0.57, 0.14, 0.30);
        cairo_rectangle(cr, 32, 111, 496 * progress->percent / 100.0, 11);
        cairo_fill(cr);
        catherine_setup_text(cr, percent, 32, 145, 12, CAIRO_FONT_WEIGHT_BOLD);
    }
    cairo_set_source_rgb(cr, cancelling ? 0.54 : 0.32, 0.20, 0.27);
    const SDL_Rect *button = &setup_cancel_button;
    cairo_rectangle(cr, button->x, button->y, button->w, button->h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    catherine_setup_text(cr, "Cancel", button->x + 31, button->y + 23, 13, CAIRO_FONT_WEIGHT_BOLD);
    cairo_destroy(cr);
    cairo_surface_flush(image);
    if (SDL_LockSurface(target) == 0) {
        SDL_ConvertPixels(target->w, target->h, SDL_PIXELFORMAT_RGB888,
                          cairo_image_surface_get_data(image), cairo_image_surface_get_stride(image),
                          target->format->format, target->pixels, target->pitch);
        SDL_UnlockSurface(target);
        SDL_UpdateWindowSurface(window);
    }
    cairo_surface_destroy(image);
}

static void catherine_parse_setup_progress(char *line, CatherineSetupProgress *progress) {
    if (strncmp(line, "P\t", 2)) return;
    char *number = line + 2;
    char *stage = strchr(number, '\t');
    if (!stage) return;
    *stage++ = 0;
    char *detail = strchr(stage, '\t');
    if (!detail) return;
    *detail++ = 0;
    int value = atoi(number);
    progress->percent = value < 0 ? -1 : (value <= 100 ? value : 100);
    snprintf(progress->stage, sizeof progress->stage, "%s", stage);
    snprintf(progress->detail, sizeof progress->detail, "%s", detail);
}

static pid_t catherine_monitor_setup(pid_t child, int fd, int *status) {
    SDL_Window *window = NULL;
    cairo_surface_t *icon = NULL;
    catherine_linux_video_driver();
    int video = !SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (video) {
        window = SDL_CreateWindow("Catherine Asset Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  SETUP_WIDTH, SETUP_HEIGHT, SDL_WINDOW_HIDDEN);
        if (window) {
            icon = catherine_game_icon_png();
            catherine_set_window_icon(window, icon);
            SDL_ShowWindow(window);
        }
    }
    CatherineSetupProgress progress = {"Preparing game assets", "Checking for extracted files", -1};
    int cancelling = 0, redraw = 1;
    char buffer[2048];
    size_t used = 0;
    pid_t result;
    while ((result = waitpid(child, status, WNOHANG)) == 0 || (result < 0 && errno == EINTR)) {
        ssize_t count = read(fd, buffer + used, sizeof buffer - used - 1);
        if (count > 0) {
            used += (size_t)count;
            buffer[used] = 0;
            char *line = buffer, *end;
            while ((end = strchr(line, '\n'))) {
                *end = 0;
                catherine_parse_setup_progress(line, &progress);
                line = end + 1;
                redraw = 1;
            }
            used -= (size_t)(line - buffer);
            memmove(buffer, line, used);
            if (used == sizeof buffer - 1) used = 0;
        }
        if (window) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                int cancel = event.type == SDL_QUIT ||
                    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) ||
                    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
                if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    const SDL_Point pointer = {event.button.x, event.button.y};
                    cancel = SDL_PointInRect(&pointer, &setup_cancel_button);
                }
                if (cancel && !cancelling) {
                    kill(child, SIGINT);
                    cancelling = redraw = 1;
                }
                if (event.type == SDL_WINDOWEVENT) redraw = 1;
            }
            if (redraw) {
                catherine_draw_setup(window, icon, &progress, cancelling);
                redraw = 0;
            }
        }
        if (video) SDL_Delay(35);
        else usleep(35000);
    }
    if (icon) cairo_surface_destroy(icon);
    if (window) SDL_DestroyWindow(window);
    if (video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    close(fd);
    return result;
}
