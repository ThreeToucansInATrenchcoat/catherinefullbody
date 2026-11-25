#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int catherine_engine_pool_callback(uint32_t kind,uint64_t callback);
void catherine_engine_emit_pool(uint64_t command,uint32_t kind,uint64_t pool,int diagnostic);
#ifdef __cplusplus
}
#endif
