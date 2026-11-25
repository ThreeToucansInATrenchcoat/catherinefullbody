#include "npc_clock_scope.h"
static _Thread_local uint64_t current_actor;
uint64_t catherine_npc_clock_enter(uint64_t actor) {
    uint64_t previous = current_actor;
    current_actor = actor;
    return previous;
}
void catherine_npc_clock_leave(uint64_t previous_actor) { current_actor = previous_actor; }
int catherine_npc_clock_contains(uint64_t actor) { return actor && current_actor == actor; }

uint64_t catherine_npc_clock_actor(void) { return current_actor; }
