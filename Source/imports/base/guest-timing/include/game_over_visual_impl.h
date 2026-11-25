#pragma once
#include "game_over_visual.h"
#include "game_over_rng.h"
#include "timing_clock.h"

// Frontend panel construction, drawing and destruction run on the game thread.
// Keep separate storage for simultaneous panels and invalidate address reuse.
static GameOverRngCache catherine_game_over_visuals[16];
static uint64_t catherine_game_over_lifetime;

void catherine_game_over_visual_created(uint64_t panel) {
    GameOverRngCache *free_slot = 0;
    for (unsigned i=0;i<16;++i) {
        GameOverRngCache *cache=&catherine_game_over_visuals[i];
        if (cache->panel==panel) { free_slot=cache; break; }
        if (!cache->panel && !free_slot) free_slot=cache;
    }
    // If the bounded sidecar table is full, leave native behavior available.
    if (!panel || !free_slot) return;
    game_over_rng_reset(free_slot);
    free_slot->panel=panel;
    free_slot->lifetime=++catherine_game_over_lifetime;
}

void catherine_game_over_visual_retired(uint64_t panel) {
    for (unsigned i=0;i<16;++i)
        if (catherine_game_over_visuals[i].panel==panel)
            game_over_rng_reset(&catherine_game_over_visuals[i]);
}

static GameOverRngCache *catherine_game_over_visual_begin(uint64_t panel) {
    for (unsigned i=0;i<16;++i) {
        GameOverRngCache *cache=&catherine_game_over_visuals[i];
        if (cache->panel!=panel) continue;
        unsigned scaled=catherine_high_refresh_active() &&
                        !(LD8(0x7000d06360ull)&1u);
        game_over_rng_begin_frame(cache,panel,cache->lifetime,scaled,
                                  !scaled || catherine_timing_tick());
        return cache;
    }
    return 0;
}

static void catherine_game_over_visual_sample(GameOverRngCache *cache,Cpu *cpu,
                                             unsigned slot,GameOverNativeRng rng) {
    if (cache) game_over_rng_sample(cache,cpu,slot,rng);
    else rng(cpu);
}
