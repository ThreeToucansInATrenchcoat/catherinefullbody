#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

typedef struct {
    uint64_t owner, component, stage, vptr, generation;
    uint32_t stage_id, floor, kind, mode, layout;
} CatherineBossCameraKey;
typedef struct {
    CatherineBossCameraKey key;
    uint64_t clock;
    uint32_t epoch, raw[6];
    float previous[6], current[6], phase;
    unsigned valid;
} CatherineBossCameraPresentation;

/* An invalidation of any boss conservatively retires every presentation history.
   The generation is global rather than TLS because construction can be on a
   different native thread from camera publication. Saturation fails closed. */
static _Atomic uint64_t catherine_boss_camera_serial = 1;
static _Atomic unsigned catherine_boss_camera_serial_exhausted;
void catherine_boss_camera_presentation_invalidate(void) {
    uint64_t prior = atomic_load_explicit(&catherine_boss_camera_serial, memory_order_relaxed);
    for (;;) {
        if (prior == UINT64_MAX) {
            atomic_store_explicit(&catherine_boss_camera_serial_exhausted, 1, memory_order_release);
            return;
        }
        if (atomic_compare_exchange_weak_explicit(&catherine_boss_camera_serial, &prior,
                prior + 1, memory_order_release, memory_order_relaxed)) return;
    }
}
static uint64_t catherine_boss_camera_generation(void) {
    if (atomic_load_explicit(&catherine_boss_camera_serial_exhausted, memory_order_acquire)) return 0;
    return atomic_load_explicit(&catherine_boss_camera_serial, memory_order_acquire);
}
static int catherine_boss_camera_known_vptr(uint64_t vptr) {
    switch (vptr) {
    case 0x7000ac2220ull: case 0x7000ac23d8ull: case 0x7000ac2588ull:
    case 0x7000ac2738ull: case 0x7000ac28e8ull: case 0x7000ac2a98ull:
    case 0x7000ac2c48ull: case 0x7000ac2e20ull: case 0x7000ac2fd0ull:
    case 0x7000ac3180ull: case 0x7000ac3330ull: case 0x7000ac34e0ull:
    case 0x7000ac3690ull: return 1;
    default: return 0;
    }
}
/* Exact ordinary-campaign record+6 boss-bit profiles from the selected image. */
static int catherine_boss_camera_stage(uint32_t stage, uint32_t floor) {
    switch (stage) {
    case 2: case 3: case 5: return floor == 3;
    case 4: return floor == 2;
    case 6: return floor == 4;
    case 7: return floor == 5;
    case 8: return floor == 1 || floor == 2;
    case 9: return floor == 5 || floor == 6;
    case 10: return floor >= 1 && floor <= 4;
    default: return 0;
    }
}
static int catherine_boss_camera_key_equal(const CatherineBossCameraKey *a, const CatherineBossCameraKey *b) {
    return a->owner == b->owner && a->component == b->component && a->stage == b->stage &&
        a->vptr == b->vptr && a->generation == b->generation && a->stage_id == b->stage_id &&
        a->floor == b->floor && a->kind == b->kind && a->mode == b->mode && a->layout == b->layout;
}
static void catherine_boss_camera_seed(CatherineBossCameraPresentation *s,
        const CatherineBossCameraKey *key, uint64_t clock, float phase, const uint32_t row[6]) {
    memset(s, 0, sizeof *s);
    s->key = *key; s->clock = clock; s->epoch = (uint32_t)(clock >> 32); s->phase = phase; s->valid = 1;
    memcpy(s->raw, row, sizeof s->raw);
    memcpy(s->previous, row, sizeof s->previous); memcpy(s->current, row, sizeof s->current);
}
static int catherine_boss_camera_sample(CatherineBossCameraPresentation *s,
        const CatherineBossCameraKey *key, uint64_t clock, unsigned high, unsigned legacy,
        unsigned flags, float jump_limit, uint32_t row[6]) {
    uint32_t phase_bits = (uint32_t)clock & 0x7fffffffu;
    float phase; memcpy(&phase, &phase_bits, 4);
    if (!high || legacy || !((uint32_t)clock >> 31) || (flags & 0x30u) != 0x10u ||
        (key->layout != 1u && key->layout != 2u) || !key->owner || !key->component ||
        !key->stage || !key->generation || !catherine_boss_camera_known_vptr(key->vptr) ||
        !catherine_boss_camera_stage(key->stage_id, key->floor) ||
        !isfinite(phase) || phase < 0.f || phase >= 1.f || !isfinite(jump_limit) || !(jump_limit > 0.f)) {
        memset(s, 0, sizeof *s); return 0;
    }
    float current[6]; memcpy(current, row, sizeof current);
    for (unsigned i = 0; i < 6; ++i) if (!isfinite(current[i])) { memset(s, 0, sizeof *s); return 0; }
    double raw_dx = (double)current[0] - current[3], raw_dy = (double)current[1] - current[4], raw_dz = (double)current[2] - current[5];
    if (!(raw_dx * raw_dx + raw_dy * raw_dy + raw_dz * raw_dz > 1.e-12)) { memset(s, 0, sizeof *s); return 0; }
    uint32_t epoch = (uint32_t)(clock >> 32), advance = epoch - s->epoch;
    unsigned reset = !s->valid || !catherine_boss_camera_key_equal(&s->key, key) || advance > 1u;
    if (!reset) {
        for (unsigned i = 0; i < 6; ++i) if (fabsf(current[i] - s->current[i]) > jump_limit) reset = 1;
        if (!advance && (phase < s->phase || memcmp(row, s->raw, sizeof s->raw))) reset = 1;
    }
    if (reset) { catherine_boss_camera_seed(s, key, clock, phase, row); return 1; }
    if (advance) {
        memcpy(s->previous, s->current, sizeof s->current);
        memcpy(s->current, current, sizeof current);
        memcpy(s->raw, row, sizeof s->raw); s->epoch = epoch;
    }
    float sampled[6];
    for (unsigned i = 0; i < 6; ++i) {
        sampled[i] = s->previous[i];
        if (phase != 0.f && s->previous[i] != s->current[i])
            sampled[i] = (float)((1.0 - (double)phase) * s->previous[i] + (double)phase * s->current[i]);
        if (!isfinite(sampled[i])) { catherine_boss_camera_seed(s, key, clock, phase, row); return 0; }
    }
    double dx = (double)sampled[0] - sampled[3], dy = (double)sampled[1] - sampled[4], dz = (double)sampled[2] - sampled[5];
    if (!(dx * dx + dy * dy + dz * dz > 1.e-12)) {
        catherine_boss_camera_seed(s, key, clock, phase, row); return 0;
    }
    memcpy(row, sampled, sizeof sampled); s->clock = clock; s->phase = phase;
    return 1;
}
static _Thread_local CatherineBossCameraPresentation catherine_boss_camera_presentation_context;
