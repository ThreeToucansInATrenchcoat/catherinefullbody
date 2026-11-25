#pragma once

#include "timing_clock.h"

static inline int catherine_is_first_lounge_wait(uint64_t vm) {
    const uint32_t pc = LD32(vm + 0x18);
    const uint32_t procedure = LD32(vm + 0x208);
    const int end_of_day = procedure == 74 && pc == 0x16d8;
    if (!end_of_day && procedure != 76)
        return 0;
    if (!end_of_day && (pc < 0x16e3 || pc >= 0x1827)) {
        switch (pc) {
        // Only the WAITs in helpers called by this monologue. The VM keeps the root procedure ID.
        case 0x6e5: case 0x6f0: case 0x710: case 0x732: case 0x75f: case 0x76e:  // Incoming call.
        case 0xfbd: case 0xfc6: case 0xfc9:                                    // Phone tutorial.
        case 0xfe2: case 0xfeb:                                                // Save tutorial.
        case 0x109c:                                                           // Rapunzel tutorial.
        case 0x1156:                                                           // Voice-change tutorial.
            break;
        default:
            return 0;
        }
    }

    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) != 0x8720 || LD32(flow + 8) != 0x30574c46 ||
        LD32(flow + 0x10) != 5 || LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) != 79 || LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4)
        return 0;

    const uint32_t procedures = LD32(flow + 0x2c);
    const uint32_t code = LD32(flow + 0x4c);
    const uint32_t count = LD32(flow + 0x48);
    if (procedures < 0x50 || procedures > 0x8720 - 79 * 32 ||
        code < 0x50 || code > 0x8720 || count <= 0x1827 || count > (0x8720 - code) / 4 ||
        LD64(vm + 0x1e0) != flow + procedures || LD64(vm + 0x1f0) != flow + code)
        return 0;

    const uint64_t entry = flow + procedures + procedure * 32;
    const uint64_t instructions = flow + code;
    if (end_of_day) {
        // Hold the authored 60 ticks between the bar's last shot and fade-out.
        // FADE_SYNC still polls the native fade on every update.
        return LD64(entry) == 0x6e655f65745f676cull &&
               LD32(entry + 24) == 0x16d3 && LD32(entry + 56) == 0x16e0 &&
               LD32(instructions + 0x16d5 * 4) == 0x0002001d &&
               LD32(instructions + 0x16d6 * 4) == 0x001e001d &&
               LD32(instructions + 0x16d7 * 4) == 0x00000010 &&
               LD32(instructions + 0x16d8 * 4) == 0x00090008;
    }
    // The first Stray Sheep monologue includes the entrances, phone and drink actions.
    return LD64(entry) == 0x6f6d5f65745f676cull &&
           LD32(entry + 24) == 0x16e3 && LD32(entry + 56) == 0x1827 &&
           LD32(instructions + 0x16fe * 4) == 0x10750008 &&
           LD32(instructions + 0x16ff * 4) == 0x0078001d &&
           LD32(instructions + 0x1715 * 4) == 0x01cd001d &&
           LD32(instructions + 0x1716 * 4) == 0x10020008 &&
           LD32(instructions + (uint64_t)pc * 4) == 0x00090008;
}

static inline int catherine_is_later_lounge_wait(uint64_t vm) {
    const uint32_t pc = LD32(vm + 0x18);
    static const struct {
        uint16_t pc, procedure, procedures, offset, words, start, end;
    } scenes[] = {
        {0x1b17, 113, 118, 0x35f0, 0x1b43, 0x1b0a, 0x1b1f},
        {0x1c05, 113, 118, 0x3450, 0x1c1c, 0x1bf6, 0x1c0a},
        {0x1bbb, 113, 118, 0x3410, 0x1c8c, 0x1bac, 0x1bc0},
        {0x1a11, 111, 116, 0x31d0, 0x1a2b, 0x1a07, 0x1a19},
    };
    unsigned i = 0;
    while (i < sizeof scenes / sizeof scenes[0] && pc != scenes[i].pc) ++i;
    if (i == sizeof scenes / sizeof scenes[0] || LD32(vm + 0x208) != scenes[i].procedure)
        return 0;

    const uint32_t offset = scenes[i].offset;
    const uint32_t words = scenes[i].words;
    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) < offset + words * 4 || LD32(flow + 8) != 0x30574c46u ||
        LD32(flow + 0x10) != 5 || LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) != scenes[i].procedures || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4 ||
        LD32(flow + 0x48) != words || LD32(flow + 0x4c) != offset ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;

    const uint64_t entry = flow + 0x70 + 32u * scenes[i].procedure;
    const uint64_t wait = flow + offset + 4u * pc;
    // The monologue sets up the next shot under black before its fade-in.
    return LD64(entry) == 0x6f6d5f65745f676cull &&
           LD32(entry + 24) == scenes[i].start && LD32(entry + 56) == scenes[i].end &&
           LD32(wait - 4) == 0x001e001du && LD32(wait) == 0x00090008u &&
           LD32(wait + 4) == 0x000f001du && LD32(wait + 8) == 0x00190008u &&
           LD32(wait + 12) == 0x001b0008u;
}

static inline int catherine_hold_first_lounge_wait(uint64_t vm) {
    return !catherine_timing_tick() &&
           (catherine_is_first_lounge_wait(vm) || catherine_is_later_lounge_wait(vm));
}
