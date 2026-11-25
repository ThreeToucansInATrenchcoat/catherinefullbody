#pragma once
#include <stdlib.h>
#include <string.h>

#include "timing_clock.h"

static inline int catherine_matches_prison_command(uint64_t vm, int initial_fade) {
    // The VM owns this copied procedure name and points at its original FLW0 sections.
    if (LD64(vm) != 0x305f31305f6c7a70ull)
        return 0;

    const uint32_t procedure = LD32(vm + 0x208);
    const uint64_t suffix = LD64(vm + 8);
    const int start_scene = procedure == 0 && suffix == 0x0074726174735f31ull;
    const int end_scene = procedure == 1 && (suffix & 0xffffffffffffull) == 0x00646e655f31ull;
    const int tutorial = procedure >= 5 && procedure <= 9 &&
                         (suffix & 0xffffffffffffull) ==
                             0x303030305f31ull + ((uint64_t)(procedure - 5) << 40);
    if (initial_fade ? !start_scene : (!start_scene && !end_scene && !tutorial))
        return 0;

    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 8) != 0x30574c46u || LD32(flow + 0x10) < 3)
        return 0;
    const uint32_t size = LD32(flow + 4);
    if (size < 0x50 || LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) < procedure + 2 || LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4)
        return 0;

    const uint32_t procedures = LD32(flow + 0x2c);
    const uint32_t code_offset = LD32(flow + 0x4c);
    const uint32_t instruction_count = LD32(flow + 0x48);
    if (procedures < 0x50 || procedures > size || size - procedures < (procedure + 2) * 32 ||
        code_offset < 0x50 || code_offset > size || instruction_count > (size - code_offset) / 4 ||
        LD64(vm + 0x1e0) != flow + procedures || LD64(vm + 0x1f0) != flow + code_offset)
        return 0;

    const uint32_t pc = LD32(vm + 0x18);
    const uint64_t entry = flow + procedures + (uint64_t)procedure * 32;
    const uint32_t start = LD32(entry + 24);
    const uint32_t end = LD32(entry + 56);
    if (pc < start || pc >= end || end > instruction_count)
        return 0;

    const uint64_t instruction = flow + code_offset + (uint64_t)pc * 4;
    if (initial_fade)
        return start == 0 && pc == 4 && end > 9 &&
               LD32(instruction - 8) == 0x00090008u &&
               LD32(instruction - 4) == 0x0000001du &&
               LD32(instruction) == 0x00190008u &&
               LD32(instruction + 4) == 0x0001001du &&
               LD32(instruction + 8) == 0x00090008u &&
               LD32(instruction + 20) == 0x001a0008u;
    if (LD32(instruction) != 0x00090008u)
        return 0;
    if (!tutorial)
        return start == (start_scene ? 0u : 0x3b2u);

    // First-climb tutorial panels precede their message by 20 authored ticks.
    static const uint16_t tutorial_start[] = {0x73c, 0x771, 0x7f7, 0x812, 0x832};
    static const uint16_t tutorial_wait[] = {0x744, 0x779, 0x804, 0x81a, 0x83a};
    const uint32_t index = procedure - 5;
    return start == tutorial_start[index] && pc == tutorial_wait[index] && end - pc > 2 &&
           LD32(instruction - 8) == 0x20230008u && LD32(instruction - 4) == 0x0014001du &&
           LD32(instruction + 8) == 0x00300008u;
}

static inline int catherine_is_prison_transition_flash(uint64_t vm) {
    return vm && catherine_matches_prison_command(vm, 1);
}

static inline int catherine_hold_prison_wait(uint64_t vm) {
    return !catherine_timing_tick() && catherine_matches_prison_command(vm, 0);
}
