#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int catherine_native_fixed_value_skip(uint64_t command,uint32_t kind,const void* values);
void catherine_native_fixed_value_learn(uint64_t command,uint32_t kind,const void* values);
#ifdef __cplusplus
}
#endif
