#pragma once
#include "cpu.h"
#include <assert.h>

typedef void (*GameOverNativeRng)(Cpu *);
typedef struct {
    uint64_t panel, lifetime;
    uint64_t outputs[8][7];
    unsigned valid, sampled, scaled, tick;
} GameOverRngCache;

// One caller-owned sidecar per panel lifetime. The lifetime token must change
// when guest storage is reused; the panel address alone is not an identity.
static inline void game_over_rng_reset(GameOverRngCache *cache){
    memset(cache,0,sizeof *cache);
}

// Call once at the start of each panel draw, with that frame's clock decision.
static inline void game_over_rng_begin_frame(GameOverRngCache *cache,
    uint64_t panel,uint64_t lifetime,unsigned scaled,unsigned tick){
    if(cache->panel!=panel||cache->lifetime!=lifetime)game_over_rng_reset(cache);
    cache->panel=panel;cache->lifetime=lifetime;
    cache->sampled=0;cache->scaled=scaled;cache->tick=tick;
    if(!scaled)cache->valid=0;
}

static inline void game_over_rng_sample(GameOverRngCache *cache,Cpu *cpu,
    unsigned slot,GameOverNativeRng native_rng){
    assert(slot<8);
    if(!cache->scaled){native_rng(cpu);return;}
    const unsigned bit=1u<<slot;
    // A newly visible channel needs an initial sample even between native ticks.
    // Each eligible frame takes at most one fresh sample for each call site.
    if(!(cache->valid&bit)||(cache->tick&&!(cache->sampled&bit))){
        native_rng(cpu);
        cache->outputs[slot][0]=cpu->x[0];
        for(unsigned i=0;i<6;++i)cache->outputs[slot][i+1]=cpu->x[8+i];
        cache->valid|=bit;cache->sampled|=bit;
        return;
    }
    // Match only the real leaf's register writes. Never copy Cpu wholesale,
    // restore the shared seed, or overwrite this call site's LR/FP/vector state.
    cpu->x[0]=cache->outputs[slot][0];
    for(unsigned i=0;i<6;++i)cpu->x[8+i]=cache->outputs[slot][i+1];
}
