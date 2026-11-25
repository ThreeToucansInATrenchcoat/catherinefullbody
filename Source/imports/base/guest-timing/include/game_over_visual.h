#pragma once
#include <stdint.h>

// The panel factory and both native destructor variants share this lifetime.
void catherine_game_over_visual_created(uint64_t panel);
void catherine_game_over_visual_retired(uint64_t panel);
