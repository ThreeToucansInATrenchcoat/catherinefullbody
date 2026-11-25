#pragma once
#include "timing_clock.h"
#include "npc_clock_scope.h"
static inline int catherine_hold_p1_reaction_counter(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull) & 1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor) != 0x7000abff90ull || LD32(actor + 0xe0ull) != 0 ||
        LD32(actor + 0x240ull) != 1 ||
        (LD32(actor + 0x178ull) != 0xcu &&
         !(LD32(actor + 0x18cull) == 1u && LD32(actor + 0x190ull) == 0x40u) &&
         !(LD32(actor + 0x18cull) == 0x80000001u &&
           LD32(actor + 0x190ull) == 0x48u &&
           (LD32(actor + 0x178ull) == 4u || LD32(actor + 0x178ull) == 0xdu ||
            LD32(actor + 0x178ull) == UINT32_MAX)))) return 0;
    const uint64_t registry = LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry) != actor) return 0;
    const uint64_t holder = LD64(0x7000cca5d8ull);
    const uint64_t manager = holder ? LD64(holder) : 0;
    const uint64_t stage = manager ? LD64(manager + 0x668ull) : 0;
    if (!stage || (uint32_t)(LD32(stage + 0x18ull) - 1u) >= 10u) return 0;
    const uint32_t state = LD32(actor + 0x18cull);
    const uint32_t state2 = LD32(actor + 0x190ull);
    return (state == 1u && state2 == 0x40u) ||
           (state == 0x80000001u && state2 == 0x48u);
}

/* Conditional clean ledge-release motion7c: keep its counter and expiry on
   the same authored clock, without suppressing common presentation services. */
static inline int catherine_hold_p1_motion7c_counter(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor)!=0x7000abff90ull || LD32(actor+0xe0ull)!=0 ||
        LD32(actor+0x240ull)!=1 || LD32(actor+0x18cull)!=0x81u ||
        LD32(actor+0x178ull)!=UINT32_MAX) return 0;
    const uint32_t state2=LD32(actor+0x190ull);
    if (state2!=0x10002000u && state2!=0x20002000u &&
        state2!=0x40002000u && state2!=0x80002000u) return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry)!=actor) return 0;
    const uint64_t holder=LD64(0x7000cca5d8ull);
    const uint64_t manager=holder?LD64(holder):0;
    const uint64_t stage=manager?LD64(manager+0x668ull):0;
    return stage && (uint32_t)(LD32(stage+0x18ull)-1u)<10u;
}
/* Clean primary contact reaction: keep its counter and expiry on
   the same authored clock, without suppressing common presentation services. */
static inline int catherine_hold_p1_contact_counter(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor)!=0x7000abff90ull || LD32(actor+0xe0ull)!=0 ||
        LD32(actor+0x240ull)!=1 || LD32(actor+0x18cull)!=0x400001u ||
        LD32(actor+0x178ull)!=0xcu) return 0;
    const uint32_t state2=LD32(actor+0x190ull);
    if (state2!=0) return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry)!=actor) return 0;
    const uint64_t holder=LD64(0x7000cca5d8ull);
    const uint64_t manager=holder?LD64(holder):0;
    const uint64_t stage=manager?LD64(manager+0x668ull):0;
    return stage && (uint32_t)(LD32(stage+0x18ull)-1u)<10u;
}
/* Primary motion8 contact action: keep its counter and expiry on
   the same authored clock, without suppressing common presentation services. */
static inline int catherine_hold_p1_action15_counter(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor)!=0x7000abff90ull || LD32(actor+0xe0ull)!=0 ||
        LD32(actor+0x240ull)!=1 || LD32(actor+0x18cull)!=0x200001u ||
        LD32(actor+0x178ull)!=0x15u) return 0;
    const uint32_t state2=LD32(actor+0x190ull);
    if (state2!=0) return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry)!=actor) return 0;
    const uint64_t holder=LD64(0x7000cca5d8ull);
    const uint64_t manager=holder?LD64(holder):0;
    const uint64_t stage=manager?LD64(manager+0x668ull):0;
    return stage && (uint32_t)(LD32(stage+0x18ull)-1u)<10u;
}

/* Source-qualified ordinary campaign items: age and effect eligibility share
   the authored clock. Existing actor action is intentionally unrestricted. */
static inline int catherine_hold_p1_itemuse_counter(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor)!=0x7000abff90ull || LD32(actor+0xe0ull)!=0 ||
        LD32(actor+0x240ull)!=1 || LD32(actor+0x190ull)!=0) return 0;
    const uint32_t state=LD32(actor+0x18cull), item=LD32(actor+0x5f0ull);
    if (!((state==0x10000001u && item==1) ||
          (state==0x40001u && (item==4 || item==5 || item==7)))) return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry)!=actor) return 0;
    const uint64_t holder=LD64(0x7000cca5d8ull);
    const uint64_t manager=holder?LD64(holder):0;
    const uint64_t stage=manager?LD64(manager+0x668ull):0;
    return stage && (uint32_t)(LD32(stage+0x18ull)-1u)<10u;
}
/* Primary contact follow-up peer eligibility: keep its counter and expiry on
   the same authored clock, without suppressing common presentation services. */
static inline int catherine_hold_p1_peer_wait(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor)!=0x7000abff90ull || LD32(actor+0xe0ull)!=0 ||
        LD32(actor+0x240ull)!=1 || LD32(actor+0x18cull)!=1u ||
        LD32(actor+0x178ull)!=0xcu) return 0;
    const uint32_t state2=LD32(actor+0x190ull);
    if (state2!=0x4000u) return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry)!=actor) return 0;
    const uint64_t holder=LD64(0x7000cca5d8ull);
    const uint64_t manager=holder?LD64(holder):0;
    const uint64_t stage=manager?LD64(manager+0x668ull):0;
    return stage && LD8(stage+0x28ull)==0 && (uint32_t)(LD32(stage+0x18ull)-1u)<10u;
}
/* Primary type6 support follow-up flag release: keep its counter and expiry on
   the same authored clock, without suppressing common presentation services. */
static inline int catherine_hold_p1_type6_wait(uint64_t actor) {
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) ||
        catherine_timing_tick() || catherine_npc_clock_contains(actor)) return 0;
    if (LD64(actor)!=0x7000abff90ull || LD32(actor+0xe0ull)!=0 ||
        LD32(actor+0x240ull)!=1 || LD32(actor+0x18cull)!=1u ||
        LD32(actor+0x178ull)!=0xdu) return 0;
    const uint32_t state2=LD32(actor+0x190ull);
    if (state2!=0x400u) return 0;
    const uint64_t registry=LD64(0x7000ccadf0ull);
    if (!registry || LD64(registry)!=actor) return 0;
    const uint64_t holder=LD64(0x7000cca5d8ull);
    const uint64_t manager=holder?LD64(holder):0;
    const uint64_t stage=manager?LD64(manager+0x668ull):0;
    return stage && LD8(stage+0x28ull)==0 && (uint32_t)(LD32(stage+0x18ull)-1u)<10u;
}
