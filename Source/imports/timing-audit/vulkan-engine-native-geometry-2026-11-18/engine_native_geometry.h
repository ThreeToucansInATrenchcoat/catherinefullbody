#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CatherineEngineVertexRange {uint64_t address,size;uint32_t binding;} CatherineEngineVertexRange;
int catherine_engine_vertex_callback(uint64_t callback);
void catherine_engine_emit_vertex_range(uint64_t command,const CatherineEngineVertexRange* range,int parity);
#ifdef __cplusplus
}
#endif
