#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One actor on the native30Hz NPC dispatch stack. No guest state is changed. */
uint64_t catherine_npc_clock_enter(uint64_t actor);
void catherine_npc_clock_leave(uint64_t previous_actor);
int catherine_npc_clock_contains(uint64_t actor);
uint64_t catherine_npc_clock_actor(void);
#ifdef __cplusplus
}
#endif
