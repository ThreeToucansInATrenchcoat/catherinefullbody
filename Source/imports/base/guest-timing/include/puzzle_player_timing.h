#pragma once
#include <stdint.h>

// Preserve the primary-player timing domain and admit only the registered
// second puzzle player. Other nonzero actor IDs retain their native NPC clock.
static inline int catherine_puzzle_player_timing(uint64_t actor) {
    const unsigned id=LD32(actor+0xe0ull);
    if(id==0)return 1;
    if(id!=1||LD64(actor)!=0x7000abff90ull)return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    return registry&&LD64(registry+8ull)==actor;
}
