#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values, never guest pointers or register-array borrows. The producer converts
 * the two compatibility enums once into the owned Vulkan draw payload. */
typedef struct CatherineEngineIndexedDraw {
    uint64_t index_address;
    uint32_t primitive;
    uint32_t index_type;
    uint32_t index_count;
    int32_t base_vertex;
} CatherineEngineIndexedDraw;

int catherine_engine_indexed_callback(uint64_t callback);
void catherine_engine_indexed_emit(uint64_t command_buffer,
    const CatherineEngineIndexedDraw* draw, int parity);

#ifdef __cplusplus
}
#endif
