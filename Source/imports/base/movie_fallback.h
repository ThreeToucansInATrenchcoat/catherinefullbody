#pragma once

#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>

extern uint64_t host_audio_played_samples(void);

static uint64_t catherine_movie_picture_clock(uint64_t clock, uint64_t rate, uint32_t mode,
                                              uint64_t audio) {
    if (mode != 2 || !audio || rate <= 1 || !host_audio_played_samples()) return clock;

    // CRI's audio master runs ahead of the Windows output. A 100 ms picture
    // offset compensates for the measured lead without changing the audio queue.
    const uint64_t delay = rate / 10;
    return clock > delay ? clock - delay : 0;
}

static int catherine_skip_puzzle_preview(Cpu *c) {
    const char *skip = getenv("CATHERINE_SKIP_PUZZLE_PREVIEW");
    if (!skip || strcmp(skip, "1") != 0) return 0;

    // This call site accepts an empty movie handle when the preview is unavailable.
    uint64_t *result = (uint64_t *)(uintptr_t)c->x[8];
    result[0] = result[1] = 0;
    fprintf(stderr, "Puzzle preview mov%03u.usm skipped while video decoding is unavailable\n",
            (unsigned)c->x[1]);
    return 1;
}
