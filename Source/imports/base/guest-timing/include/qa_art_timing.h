#pragma once

#include "timing_clock.h"

// This C unit runs the native frontend synchronously on the game thread.
// Restore the scope before returning so unrelated menus retain their timing.
static unsigned catherine_qa_art_scope;

static int catherine_qa_art_scaled(void) {
    return catherine_qa_art_scope && catherine_high_refresh_active() &&
           !(LD8(0x7000d06360ull) & 1u);
}

static unsigned catherine_qa_art_tick(void) {
    return !catherine_qa_art_scaled() || catherine_timing_tick();
}
