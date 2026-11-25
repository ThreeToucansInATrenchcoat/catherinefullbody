#pragma once

#include <stdint.h>

unsigned catherine_timing_tick(void);
void catherine_timing_publish(uint32_t delta_bits);
int catherine_high_refresh_active(void);
void catherine_startup_movie_scope(int enter);
