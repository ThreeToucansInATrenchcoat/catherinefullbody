#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern int catherine_high_refresh_active(void);

static _Thread_local double elapsed_in_tick;
static _Thread_local uint32_t announcement_frame_epoch;
static _Atomic uint64_t announcement_frame_clock;
static _Atomic unsigned game_tick = 1;
static _Atomic uint64_t visual_clock;
static uint32_t visual_tick;
static _Atomic uint32_t puzzle_visual_tick;
static _Atomic unsigned puzzle_visual_active;

uint64_t catherine_announcement_frame_clock(void) {
 return atomic_load_explicit(&announcement_frame_clock,memory_order_acquire);
}

void catherine_timing_publish(uint32_t delta_bits) {
    uint32_t epoch=++announcement_frame_epoch;if(!epoch)epoch=++announcement_frame_epoch;
    atomic_store_explicit(&announcement_frame_clock,((uint64_t)epoch<<32)|delta_bits,memory_order_release);
    float delta;
    memcpy(&delta, &delta_bits, sizeof delta);
    unsigned tick = 1;
    // A short high-refresh frame contributes its measured time, even below one millisecond.
    if (!catherine_high_refresh_active() || !isfinite(delta) || delta < 0.0f || delta > 0.100f) {
        elapsed_in_tick = 0;
    } else {
        const double period = 1.0 / 30.0;
        elapsed_in_tick += delta;
        tick = elapsed_in_tick + 1e-8 >= period;
        if (tick) {
            elapsed_in_tick -= period;
            if (elapsed_in_tick >= period || elapsed_in_tick < 0) elapsed_in_tick = 0;
        }
    }
    atomic_store_explicit(&game_tick, tick, memory_order_release);
    visual_tick += tick;
    float fraction=(float)(elapsed_in_tick*30.0);uint32_t fraction_bits;memcpy(&fraction_bits,&fraction,4);
    const unsigned interpolate=catherine_high_refresh_active()&&isfinite(delta)&&delta>0.f&&delta<0.031f;
    atomic_store_explicit(&visual_clock,((uint64_t)visual_tick<<32)|fraction_bits|(interpolate?0x80000000u:0u),memory_order_release);
}

unsigned catherine_timing_tick(void) {
    return atomic_load_explicit(&game_tick, memory_order_acquire);
}

void catherine_publish_puzzle_visual_actor(void) {
 uint64_t clock=atomic_load_explicit(&visual_clock,memory_order_acquire);
 atomic_store_explicit(&puzzle_visual_tick,(uint32_t)(clock>>32),memory_order_relaxed);
 atomic_store_explicit(&puzzle_visual_active,1,memory_order_release);
}
uint64_t catherine_animation_render_clock(void) {
 uint64_t clock=atomic_load_explicit(&visual_clock,memory_order_acquire);
 if(!atomic_load_explicit(&puzzle_visual_active,memory_order_acquire)||(uint32_t)((clock>>32)-atomic_load_explicit(&puzzle_visual_tick,memory_order_relaxed))>1)clock&=~0x80000000ull;
 return clock;
}

/* Camera presentation is not restricted to recent puzzle actor ownership. */
uint64_t catherine_camera_render_clock(void) {
 return atomic_load_explicit(&visual_clock,memory_order_acquire);
}
