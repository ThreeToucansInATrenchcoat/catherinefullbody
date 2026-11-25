#define WIN32_LEAN_AND_MEAN
#include "../../../platform/host_memory.h"
#include <stdatomic.h>
#include <stdint.h>
#include "include/catherine_refresh.h"
#include "include/movie_interpolation.h"

static _Atomic unsigned startup_movie_depth;

void catherine_startup_movie_scope(int enter) {
    if (enter) atomic_fetch_add_explicit(&startup_movie_depth, 1, memory_order_release);
    else atomic_fetch_sub_explicit(&startup_movie_depth, 1, memory_order_release);
}

int catherine_fullscreen_movie_active(void) {
    if (atomic_load_explicit(&startup_movie_depth, memory_order_acquire)) return 1;

    uint64_t task = 0;
    uint32_t state = 7;
    if (!catherine_host_read((const void *)(uintptr_t)0x7000d063a8ull, &task, sizeof task) || !task)
        return 0;
    return catherine_host_read((const void *)(uintptr_t)0x7000d0637cull, &state, sizeof state) &&
           state <= 6;
}

void catherine_movie_interpolation_display_upload(uint64_t movie, uint64_t info, uint64_t descriptor) {
    const int target = catherine_requested_movie_interpolation();
    if (!target || !catherine_fullscreen_movie_active()) return;

    const uint32_t *frame = (const uint32_t *)(uintptr_t)info;
    if ((frame[6] != 30000 && frame[6] != 29970) || frame[8] != 1000) return;

    const uint32_t *dimensions = (const uint32_t *)(uintptr_t)(movie + 0x5b8);
    const uint64_t memory = *(const uint64_t *)(uintptr_t)descriptor;
    const uint32_t stride = *(const uint32_t *)(uintptr_t)(descriptor + 12);
    catherine_movie_interpolation_display(memory, dimensions[0], dimensions[1], stride, frame[0], target);
}
