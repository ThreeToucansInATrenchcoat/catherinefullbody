#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t catherine_summon_clock_enter(uint64_t wrapper,uint64_t actor);
void catherine_summon_clock_leave(uint64_t previous);
void catherine_summon_clock_forget(uint64_t wrapper);
int catherine_summon_clock_held(uint64_t actor);
int catherine_summon_clock_grace_held(uint64_t actor,uint32_t age);
#ifdef __cplusplus
}
#endif
