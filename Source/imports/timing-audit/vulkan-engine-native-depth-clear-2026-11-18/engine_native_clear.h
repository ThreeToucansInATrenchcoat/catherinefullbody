#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CatherineEngineDepthClear {uint32_t depth_bits,depth_enabled,stencil_value,stencil_write_mask;} CatherineEngineDepthClear;
int catherine_engine_clear_callback(uint32_t kind,uint64_t callback);
void catherine_engine_emit_depth_clear(uint64_t command,const CatherineEngineDepthClear* values,int diagnostic);
#ifdef __cplusplus
}
#endif
