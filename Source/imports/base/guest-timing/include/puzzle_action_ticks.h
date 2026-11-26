#pragma once
#include "timing_clock.h"
#include "puzzle_player_timing.h"

static inline int catherine_is_floor_two_sheep(uint64_t actor) {
    if (LD64(actor) != 0x7000ac46e0ull || LD32(actor + 0x190) != 0)
        return 0;
    const uint64_t holder = LD64(0x7000cca5d8ull);
    const uint64_t manager = holder ? LD64(holder) : 0;
    const uint64_t stage = manager ? LD64(manager + 0x668) : 0;
    if (!stage || LD32(stage + 0x1c) != 2 || LD8(stage + 0x28) != 0)
        return 0;

    // The second floors of Prison of Despair and Quadrangle use this patrol.
    const uint32_t stage_number = LD32(stage + 0x18);
    return stage_number == 2 || stage_number == 5;
}

static inline int catherine_hold_floor_two_sheep_planner(uint64_t actor) {
    return !catherine_timing_tick() && LD32(actor + 0x18c) == 0 &&
           LD32(actor + 0x178) == 0xd && catherine_is_floor_two_sheep(actor);
}

static inline int catherine_hold_floor_two_sheep_queue(uint64_t actor, uint64_t node, uint64_t command) {
    if (catherine_timing_tick() || LD32(actor + 0x18c) != 0 || LD32(command + 0xc) != UINT32_MAX)
        return 0;
    const uint32_t action = LD32(actor + 0x178);
    const uint32_t opcode = LD32(command + 8);
    if ((action != 0xd && action != UINT32_MAX) || (opcode != 14 && opcode != 16) ||
        !catherine_is_floor_two_sheep(actor)) return 0;

    // The patrol queues a turn and a follow-up command for the same direction.
    const uint64_t next_node = LD64(node + 0x30);
    const uint64_t next = next_node ? LD64(next_node + 0x18) : 0;
    return next && LD32(next + 8) == opcode - 8 && LD32(next + 0xc) == UINT32_MAX;
}

static inline int catherine_hold_puzzle_action_step(uint64_t player, uint64_t steps) {
    if (catherine_timing_tick() || steps != 1) return 0;
    if (!catherine_puzzle_player_timing(player)) {
        const uint32_t state = LD32(player + 0x18c);
        if ((state != 0x10001 && state != 0x2001) || !catherine_is_floor_two_sheep(player)) return 0;
        const uint32_t action = LD32(player + 0x178);
        // The first turn starts before the patrol has recorded its movement action.
        return action == 0xd || (action == UINT32_MAX && state == 0x10001 &&
               LD32(player + 0x880) == 3 && (LD32(player + 0x8928) & 1));
    }
    const uint32_t state = LD32(player + 0x18c);
    const uint32_t state2 = LD32(player + 0x190);
    switch (state2) {
    case 0:
        // Releasing a ledge runs collision and gravity as one authored step.
        return state == 0x801 || state == 0x5 || state == 0xb || state == 0x11 || state == 0x21 ||
               state == 0x2001 || state == 0x10001;
    case 0x10000000:
    case 0x20000000:
    case 0x40000000:
    case 0x80000000:
        // A hanging move, corner turn or pull-up advances an authored action counter.
        return state == 0x281 || state == 0x4081 || state == 0x8081 ||
               (state2 == 0x40000000 && state == 0xc1);
    default:
        return 0;
    }
}
