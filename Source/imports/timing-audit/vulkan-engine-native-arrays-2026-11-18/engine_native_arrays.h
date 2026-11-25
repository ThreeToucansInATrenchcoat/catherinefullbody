#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CatherineEngineArrayDraw {
    uint32_t primitive;
    int32_t first_vertex;
    int32_t vertex_count;
} CatherineEngineArrayDraw;
int catherine_engine_array_callback(uint64_t callback);
void catherine_engine_array_emit(uint64_t command_buffer,
    const CatherineEngineArrayDraw* draw, int parity);
#ifdef __cplusplus
}
#endif
