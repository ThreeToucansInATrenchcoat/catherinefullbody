#pragma once
#include "story_end_wait_ticks.h"

#include <stdint.h>

#include "timing_clock.h"

static inline int catherine_matches_stage_two_wait(uint64_t vm) {
    // Only the start procedure of each verified story board. The code is identical across locales.
    const uint64_t prefix = 0x305f32305f6c7a70ull;
    if (!vm || LD64(vm) != prefix || LD32(vm + 0x208) != 0) return 0;
    const uint64_t suffix = LD64(vm + 8);
    const int first_floor = suffix == 0x0074726174735f31ull;
    if (!first_floor && suffix != 0x0074726174735f32ull) return 0;

    const uint32_t pc = LD32(vm + 0x18);
    static const struct CatherineIntroWait {
        uint16_t pc, ticks;
        uint32_t before, next, following;
    } first_waits[] = {
        {0x53, 30, 0x200d0008u, 0x01c8001du, 0x20220008u},
        {0x5a, 10, 0x001b0008u, 0x001b001du, 0x001e0008u},
        {0x5f, 10, 0x001f0008u, 0x001c001du, 0x001e0008u},
        {0x64,  4, 0x001f0008u, 0x001d001du, 0x001e0008u},
        {0x72,  5, 0x20220008u, 0x001e001du, 0x001e0008u},
    }, second_waits[] = {
        {0x26, 30, 0x20360008u, 0x0028001du, 0x00190008u},
        {0x31, 18, 0x200d0008u, 0x000e001du, 0x001e0008u},
        {0x35, 48, 0x001e0008u, 0x01c9001du, 0x20220008u},
        {0x3d,  6, 0x001f0008u, 0x0010001du, 0x001e0008u},
        {0x41, 20, 0x001e0008u, 0x000a001du, 0x0001001du},
    };
    const struct CatherineIntroWait* waits = first_floor ? first_waits : second_waits;
    const unsigned count = first_floor ? sizeof first_waits / sizeof first_waits[0]
                                       : sizeof second_waits / sizeof second_waits[0];
    unsigned i = 0;
    while (i < count && waits[i].pc != pc) ++i;
    if (i == count) return 0;

    const uint64_t flow = LD64(vm + 0x1d0);
    const uint32_t offset = first_floor ? 0x630u : 0x590u;
    const uint32_t words = first_floor ? 0x804u : 0x778u;
    const uint32_t procedures = first_floor ? 0x13u : 0x11u;
    if (!flow || LD32(flow + 4) < offset + words * 4 || LD32(flow + 8) != 0x30574c46u || LD32(flow + 0x10) != 5 ||
        LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) != procedures || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4 ||
        LD32(flow + 0x48) != words || LD32(flow + 0x4c) != offset ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;

    const uint64_t entry = flow + 0x70;
    if (LD64(entry) != prefix || LD64(entry + 8) != suffix ||
        LD32(entry + 24) != 0 || LD32(entry + 56) != (first_floor ? 0x82u : 0x53u))
        return 0;

    const uint64_t instruction = flow + offset + 4u * pc;
    return LD32(instruction - 8) == waits[i].before &&
           LD32(instruction - 4) == (((uint32_t)waits[i].ticks << 16) | 0x1du) &&
           LD32(instruction) == 0x00090008u &&
           LD32(instruction + 4) == waits[i].next &&
           LD32(instruction + 8) == waits[i].following;
}

static inline int catherine_matches_story_ring_wait(uint64_t vm) {
    if (!vm) return 0;
    const uint64_t goal_name = 0x6c616f675f6c7a70ull;
    const uint64_t name_mask = 0xffffffffffffull;
    const uint64_t name = LD64(vm);
    if ((uint32_t)name != 0x5f6c7a70u) return 0; // "pzl_"
    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) < 0xb0 || LD32(flow + 8) != 0x30574c46u ||
        LD32(flow + 0x10) != 5 || LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x2c) != 0x70 || LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4)
        return 0;

    const uint64_t root = flow + 0x90;
    const uint64_t prefix = LD64(root);
    const uint64_t suffix = LD64(root + 8) & name_mask;
    const unsigned stage_digits = (unsigned)(prefix >> 32) & 0xffffu;
    const unsigned floor = (unsigned)(suffix & 0xffu) - '1';

    // The story scripts share this layout and name their second procedure
    // pzl_NN_0F_end. The numbered matches exclude non-story procedures.
    if ((prefix & 0xffff0000ffffffffull) != 0x305f00005f6c7a70ull ||
        (suffix & 0xffffffffff00ull) != 0x0000646e655f00ull ||
        (stage_digits != 0x3031u && ((stage_digits & 0xffu) != '0' ||
         (stage_digits >> 8) < '1' || (stage_digits >> 8) > '9')) || floor >= 6)
        return 0;

    const uint32_t offset = LD32(flow + 0x4c);
    const uint32_t words = LD32(flow + 0x48);
    const uint32_t procedures = LD32(flow + 0x28);
    const uint32_t size = LD32(flow + 4);
    if (procedures < 4 || offset < 0x70 || procedures > (offset - 0x70) / 32 ||
        size < offset || words <= 0x314 || words > (size - offset) / 4 ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;

    const uint32_t pc = LD32(vm + 0x18);
    const int torture_chamber = stage_digits == 0x3330u;
    const uint32_t procedure = LD32(vm + 0x208);
    const uint64_t instruction = flow + offset + pc * 4;
    // Torture Chamber's first floor stages Rin's appearance inside the
    // escape sequence and then uses its own inline ring animation.
    if (torture_chamber && floor == 0 && offset == 0x530 && words == 0xa61 &&
        procedures == 15 && procedure == 1 && name == prefix &&
        (LD64(vm + 8) & name_mask) == suffix && LD32(root + 24) == 0x6a &&
        LD32(root + 56) == 0x384 && pc > 0x6a && pc < 0x384) {
        return LD32(instruction) == 0x00090008u &&
               (LD32(instruction - 4) & 0xffffu) == 0x1du;
    }

    // The boss opens the door in a nested goal procedure before the
    // defeat cutscene resumes in its root end procedure.
    const uint32_t door_start = words - 0x10a;
    const int boss_door = (torture_chamber && floor == 2 && words == 0xee9) ||
                          (stage_digits == 0x3430u && floor == 1 && words == 0xe87) ||
                          (stage_digits == 0x3530u && floor == 2 && words == 0xde6);
    const uint64_t door = flow + 0x70 + 14 * 32;
    if (boss_door && offset == 0x530 && procedures == 15 && pc > door_start && pc < words &&
        LD64(door) == goal_name && LD64(door + 8) == 0x73625f6c616f675full &&
        LD32(door + 16) == '1' && LD32(door + 24) == door_start &&
        ((procedure == 1 && name == prefix && (LD64(vm + 8) & name_mask) == suffix) ||
         (procedure == 14 && name == goal_name && LD64(vm + 8) == LD64(door + 8) &&
          LD32(vm + 16) == '1'))) {
        return LD32(instruction) == 0x00090008u &&
               (LD32(instruction - 4) & 0xffffu) == 0x1du;
    }

    const uint32_t first_goal = words - 0x314;
    const int second_ring = pc >= first_goal + 0x103;
    const uint32_t goal = procedures - 3 + second_ring;
    const uint32_t begin = first_goal + (second_ring ? 0x103 : 0);
    const uint32_t end = begin + (second_ring ? 0x107 : 0x103);
    if (pc <= begin || pc >= end) return 0;
    const uint64_t entry = flow + 0x70 + goal * 32;
    const uint64_t ring_name = second_ring ? 0x00326b636f6c625full : 0x00316b636f6c625full;
    if (LD64(entry) != goal_name || LD64(entry + 8) != ring_name ||
        LD32(entry + 24) != begin || LD32(entry + 56) != end)
        return 0;

    if (procedure == 1) {
        if (name != prefix || (LD64(vm + 8) & name_mask) != suffix) return 0;
    } else if (procedure != goal || name != goal_name || LD64(vm + 8) != LD64(entry + 8)) {
        return 0;
    }

    // The two ring variants share the first thirteen wait instructions.
    static const struct {
        uint16_t offset, ticks;
    } waits[] = {
        {0x67, 3}, {0x6c, 3}, {0x9a, 6}, {0x9c, 14}, {0xa0, 15},
        {0xa3, 20}, {0xad, 15}, {0xbf, 29}, {0xc2, 8}, {0xc6, 5},
        {0xc9, 7}, {0xde, 23}, {0xe2, 20},
    };
    const uint32_t relative_pc = pc - begin;
    uint32_t ticks = 0;
    for (unsigned i = 0; i < sizeof waits / sizeof waits[0]; ++i)
        if (relative_pc == waits[i].offset) {
            ticks = waits[i].ticks;
            break;
        }

    if (relative_pc == (second_ring ? 0xe9u : 0xebu)) ticks = 45;
    if (relative_pc == (second_ring ? 0xedu : 0xefu)) ticks = 43;
    if (second_ring && relative_pc == 0xf1) ticks = 80;
    if (second_ring && relative_pc == 0xf5) ticks = 35;

    return ticks && LD32(instruction) == 0x00090008u &&
           LD32(instruction - 4) == ((ticks << 16) | 0x1du);
}

static inline int catherine_matches_boss_scene_wait(uint64_t vm) {
    if (!vm) return 0;
    const uint64_t prefix = LD64(vm);
    static const struct {
        uint64_t prefix;
        uint16_t words, intro_end, ending_end;
        uint8_t floor;
    } layouts[] = {
        {0x305f32305f6c7a70ull, 0xd9b, 0x21a, 0x67d, '3'},
        {0x305f33305f6c7a70ull, 0xee9, 0x2ce, 0x7cb, '3'},
        {0x305f34305f6c7a70ull, 0xe87, 0x31b, 0x79f, '2'},
        {0x305f35305f6c7a70ull, 0xde6, 0x244, 0x6fe, '3'},
    };
    const unsigned count = sizeof layouts / sizeof layouts[0];
    unsigned layout = 0;
    while (layout < count && layouts[layout].prefix != prefix) ++layout;
    if (layout == count) return 0;

    const uint32_t procedure = LD32(vm + 0x208);
    const int ending = procedure == 1 && layouts[layout].ending_end != 0;
    const uint64_t name_mask = ending ? 0xffffffffffffull : UINT64_MAX;
    const uint64_t suffix_base = ending ? 0x0000646e655f00ull : 0x0074726174735f00ull;
    const uint64_t suffix = suffix_base | layouts[layout].floor;
    if ((procedure != 0 && !ending) || (LD64(vm + 8) & name_mask) != suffix)
        return 0;

    const uint32_t begin = ending ? layouts[layout].intro_end : 0;
    const uint32_t end = ending ? layouts[layout].ending_end : layouts[layout].intro_end;
    const uint32_t words = layouts[layout].words;
    const uint32_t pc = LD32(vm + 0x18);
    if (pc <= begin || pc >= end) return 0;
    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) < 0x530 + words * 4 ||
        LD32(flow + 8) != 0x30574c46u || LD32(flow + 0x10) != 5 ||
        LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) != 15 || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4 ||
        LD32(flow + 0x48) != words || LD32(flow + 0x4c) != 0x530 ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + 0x530)
        return 0;

    const uint64_t entry = flow + 0x70 + 32 * procedure;
    if (LD64(entry) != prefix || (LD64(entry + 8) & name_mask) != suffix ||
        LD32(entry + 24) != begin || LD32(entry + 56) != end)
        return 0;

    // Literal waits use the 30 Hz game clock. Camera waits have their own opcode.
    const uint64_t instruction = flow + 0x530 + pc * 4;
    return LD32(instruction) == 0x00090008u && (LD32(instruction - 4) & 0xffffu) == 0x1du;
}

static inline int catherine_matches_puzzle_dialogue_wait(uint64_t vm) {
    if (!vm || LD32(vm + 0x208) != 0) return 0;
    const uint64_t prefix = LD64(vm);
    const uint64_t suffix = LD64(vm + 8);
    if ((prefix & 0xffff0000ffffffffull) != 0x305f00005f6c7a70ull ||
        (suffix & 0xffffffffffffff00ull) != 0x0074726174735f00ull)
        return 0;

    const unsigned tens = (unsigned)((prefix >> 32) & 0xff) - '0';
    const unsigned ones = (unsigned)((prefix >> 40) & 0xff) - '0';
    const unsigned floor = (unsigned)(suffix & 0xff) - '0';
    if (tens > 1 || ones > 9 || floor < 1 || floor > 6) return 0;
    const unsigned stage = 10 * tens + ones;
    const uint32_t pc = LD32(vm + 0x18);
    if (!pc) return 0;

    // Story floor openings verified against each localized script.
    static const struct {
        uint8_t stage, floor, procedures;
        uint16_t offset, words, end, first_wait;
        uint32_t first_duration;
    } layouts[] = {
        { 3, 1, 15, 0x530, 0xa61, 0x6a, 0x4d, 0x001e001du},
        { 3, 2, 15, 0x550, 0x75b, 0x53, 0x26, 0x001e001du},
        { 4, 1, 16, 0x550, 0x77b, 0x83, 0x5b, 0x001e001du},
        { 5, 1, 15, 0x530, 0x765, 0x84, 0x4e, 0x001e001du},
        { 5, 2, 15, 0x530, 0x734, 0x53, 0x26, 0x001e001du},
        { 6, 1, 15, 0x530, 0x76d, 0x8c, 0x56, 0x001e001du},
        { 6, 2, 15, 0x530, 0x734, 0x53, 0x26, 0x001e001du},
        { 6, 3, 15, 0x530, 0x734, 0x53, 0x26, 0x001e001du},
        { 6, 4, 15, 0x530, 0xe2a, 0x22b, 0xb6, 0x0005001du},
        { 7, 1, 15, 0x530, 0x76c, 0x8b, 0x5c, 0x001e001du},
        { 7, 2, 15, 0x530, 0x734, 0x53, 0x26, 0x001e001du},
        { 7, 3, 15, 0x530, 0x735, 0x53, 0x26, 0x001e001du},
        { 7, 4, 15, 0x530, 0x735, 0x53, 0x26, 0x001e001du},
        { 7, 5, 15, 0x530, 0xc54, 0x1fd, 0x6f, 0x000f001du},
        { 8, 1, 16, 0x550, 0xaa7, 0x376, 0x5d, 0x0002001du},
        { 8, 2, 16, 0x550, 0xb15, 0x3e4, 0x60, 0x0002001du},
        { 9, 1, 16, 0x590, 0x928, 0x23b, 0x9e, 0x0006001du},
        { 9, 2, 15, 0x530, 0x734, 0x53, 0x26, 0x001e001du},
        { 9, 3, 15, 0x530, 0x734, 0x53, 0x26, 0x001e001du},
        { 9, 4, 15, 0x570, 0x741, 0x5f, 0x28, 0x001e001du},
        { 9, 5, 16, 0x6f0, 0xb45, 0x34f, 0x88, 0x000f001du},
        { 9, 6, 23, 0x670, 0xc00, 0x49e, 0x65, 0x0002001du},
        {10, 1, 16, 0x550, 0x87f, 0x18f, 0xce, 0x000a001du},
        {10, 2, 15, 0x530, 0x739, 0x58, 0x26, 0x001e001du},
        {10, 3, 15, 0x530, 0x775, 0x94, 0x34, 0x001e001du},
        {10, 4, 21, 0x5f0, 0xa69, 0x29b, 0xfc, 0x001e001du},
    };
    unsigned i = 0;
    while (i < sizeof layouts / sizeof layouts[0] &&
           (layouts[i].stage != stage || layouts[i].floor != floor)) ++i;
    if (i == sizeof layouts / sizeof layouts[0] || pc >= layouts[i].end) return 0;

    const uint32_t offset = layouts[i].offset;
    const uint32_t words = layouts[i].words;
    const uint32_t procedures = layouts[i].procedures;
    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) < offset + words * 4 ||
        LD32(flow + 8) != 0x30574c46u || LD32(flow + 0x10) != 5 ||
        LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) != procedures || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4 ||
        LD32(flow + 0x48) != words || LD32(flow + 0x4c) != offset ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;

    const uint64_t entry = flow + 0x70;
    if (LD64(entry) != prefix || LD64(entry + 8) != suffix ||
        LD32(entry + 24) != 0 || LD32(entry + 56) != layouts[i].end)
        return 0;

    const uint64_t first_wait = flow + offset + 4u * layouts[i].first_wait;
    const uint64_t instruction = flow + offset + 4u * pc;
    return LD32(first_wait) == 0x00090008u && LD32(first_wait - 4) == layouts[i].first_duration &&
           LD32(instruction) == 0x00090008u && (LD32(instruction - 4) & 0xffffu) == 0x1du;
}

static inline int catherine_matches_prison_landing_wait(uint64_t vm) {
    const uint64_t name = 0x746e6576655f666dull; // "mf_event"
    const uint64_t suffix = 0x0000006e69616d5full; // "_main"
    const uint64_t suffix_mask = 0xffffffffffffull;
    if (!vm || LD32(vm + 0x208) != 2 || LD64(vm) != name ||
        (LD64(vm + 8) & suffix_mask) != suffix)
        return 0;

    const uint32_t pc = LD32(vm + 0x18);
    const uint32_t offset = 0xd0;

    // Each church landing has the same code across all five locales.
    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) < offset || LD32(flow + 8) != 0x30574c46u ||
        LD32(flow + 0x10) != 5 || LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) != 3 || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4 ||
        LD32(flow + 0x4c) != offset ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;

    const uint64_t entry = flow + 0xb0;
    const uint64_t code = flow + offset;
    const uint32_t start = LD32(entry + 24);
    const uint32_t words = LD32(flow + 0x48);
    if (LD64(entry) != name || LD64(entry + 8) != suffix ||
        pc <= start || pc >= words || words > (LD32(flow + 4) - offset) / 4)
        return 0;

    static const struct {
        uint16_t words, start, cue;
        uint32_t command, frames;
    } landings[] = {
        {0x283, 0x72, 0x210, 0x001e0008u, 0x0050001du}, // Prison entrance
        {0x290, 0x64, 0x0a9, 0x001e0008u, 0x0027001du},
        {0x4de, 0xff, 0x18a, 0x001e0008u, 0x000a001du},
        {0x346, 0x9b, 0x0f9, 0x20350008u, 0x0007001du},
    };
    unsigned i = 0;
    while (i < sizeof landings / sizeof landings[0] && landings[i].words != words) ++i;
    if (i == sizeof landings / sizeof landings[0] || landings[i].start != start)
        return 0;

    const uint64_t cue = code + 4u * landings[i].cue;
    if (LD32(cue) != landings[i].command || LD32(cue + 4) != landings[i].frames ||
        LD32(cue + 8) != 0x00090008u)
        return 0;

    // Keep the authored pauses between camera, animation and voice cues. MSG_SYNC polls as usual.
    const uint64_t instruction = code + pc * 4;
    return LD32(instruction) == 0x00090008u && (LD32(instruction - 4) & 0xffffu) == 0x1du;
}

static inline int catherine_matches_landing_tutorial_wait(uint64_t vm) {
    const uint64_t prefix = 0x6f7475745f63666dull; // "mfc_tuto"
    if (!vm || LD64(vm) != prefix) return 0;
    const uint32_t procedure = LD32(vm + 0x208);
    if (procedure < 2 || procedure > 5) return 0;

    static const uint64_t suffixes[] = {0x70757465735full, 0x3831u, 0x3931u, 0x3032u};
    const uint64_t suffix = suffixes[procedure - 2];
    if ((LD64(vm + 8) & 0xffffffffffffffull) != suffix) return 0;

    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 4) < 0x150 || LD32(flow + 8) != 0x30574c46u ||
        LD32(flow + 0x10) != 5 || LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) < 7 || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4)
        return 0;

    const uint32_t offset = LD32(flow + 0x4c);
    const uint32_t words = LD32(flow + 0x48);
    const uint32_t size = LD32(flow + 4);
    if (offset < 0x150 || offset > size || words > (size - offset) / 4 ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;

    const uint64_t entry = flow + 0x70 + 32u * procedure;
    const uint32_t pc = LD32(vm + 0x18);
    const uint32_t end = LD32(entry + 56);
    const int setup = procedure == 2;
    if (LD64(entry) != prefix || LD64(entry + 8) != suffix ||
        pc < 2 || pc >= words || end > words || pc <= LD32(entry + 24) ||
        end - pc != (setup ? 2u : 3u))
        return 0;

    // Preserve the pause after the fade and the hold before the tutorial closes.
    const uint64_t instruction = flow + offset + 4u * pc;
    return LD32(instruction - 8) == (setup ? 0x001b0008u : 0x001f0008u) &&
           LD32(instruction - 4) == (setup ? 0x0014001du : 0x0008001du) &&
           LD32(instruction) == 0x00090008u &&
           LD32(instruction + 4) == (setup ? 0x00000009u : 0x20250008u);
}

static inline int catherine_matches_landing_enter_wait(uint64_t vm) {
    // CALL keeps the original VM name/procedure while tutorial children execute.
    const uint64_t prefix = 0x65746e655f63666dull; // "mfc_ente"
    const uint64_t suffix = 0x6f7475745f72ull; // "r_tuto"
    if (!vm || LD32(vm + 0x208) != 1 || LD64(vm) != prefix ||
        (LD64(vm + 8) & 0xffffffffffffffull) != suffix) return 0;
    const uint64_t flow = LD64(vm + 0x1d0);
    if (!flow || LD32(flow + 8) != 0x30574c46u || LD32(flow + 0x10) != 5 ||
        LD32(flow + 0x20) != 0 || LD32(flow + 0x24) != 32 ||
        LD32(flow + 0x28) < 7 || LD32(flow + 0x2c) != 0x70 ||
        LD32(flow + 0x40) != 2 || LD32(flow + 0x44) != 4) return 0;
    const uint32_t offset = LD32(flow + 0x4c), words = LD32(flow + 0x48);
    const uint32_t size = LD32(flow + 4), pc = LD32(vm + 0x18);
    if (offset < 0x150 || offset > size || words < 248 || words > (size - offset) / 4 ||
        LD64(vm + 0x1e0) != flow + 0x70 || LD64(vm + 0x1f0) != flow + offset)
        return 0;
    const uint64_t root = flow + 0x90, code = flow + offset;
    if (LD64(root) != prefix || LD64(root + 8) != suffix ||
        LD32(root + 24) != 138 || LD32(root + 56) != 181 ||
        LD32(code + 4 * 0x9e) != 0x0002000bu || LD32(code + 4 * 0x9f) != 0x0003000bu ||
        LD32(code + 4 * 0xb1) != 0x0002000bu || LD32(code + 4 * 0xb2) != 0x0005000bu)
        return 0;
    unsigned procedure;
    if (pc == 0xba) procedure = 2;
    else if (pc == 0xcd) procedure = 3;
    else if (pc == 0xf5) procedure = 5;
    else return 0;
    const uint64_t child = flow + 0x70 + 32u * procedure;
    const uint64_t child_suffix = procedure == 2 ? 0x70757465735full : procedure == 3 ? 0x3831u : 0x3032u;
    const uint32_t start = procedure == 2 ? 181 : procedure == 3 ? 188 : 228;
    const uint32_t end = procedure == 2 ? 188 : procedure == 3 ? 208 : 248;
    const uint64_t instruction = code + 4u * pc;
    return LD64(child) == 0x6f7475745f63666dull && LD64(child + 8) == child_suffix &&
           LD32(child + 24) == start && LD32(child + 56) == end &&
           LD32(instruction - 8) == (procedure == 2 ? 0x001b0008u : 0x001f0008u) &&
           LD32(instruction - 4) == (procedure == 2 ? 0x0014001du : 0x0008001du) &&
           LD32(instruction) == 0x00090008u &&
           LD32(instruction + 4) == (procedure == 2 ? 0x00000009u : 0x20250008u);
}

static inline int catherine_hold_stage_two_wait(uint64_t vm) {
    return !catherine_timing_tick() &&
           (catherine_matches_story_end_wait(vm) || catherine_matches_stage_two_wait(vm) || catherine_matches_story_ring_wait(vm) ||
            catherine_matches_boss_scene_wait(vm) || catherine_matches_puzzle_dialogue_wait(vm) ||
            catherine_matches_prison_landing_wait(vm) || catherine_matches_landing_tutorial_wait(vm) ||
            catherine_matches_landing_enter_wait(vm));
}
