#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int catherine_texture_callback(uint32_t kind,uint64_t address);
uint64_t catherine_engine_texture_handle(uint64_t device,uint32_t texture,uint32_t sampler);
void catherine_engine_emit_texture_binding(uint64_t command,uint32_t stage,uint32_t slot,uint64_t handle,int diagnostic);
#ifdef __cplusplus
}
#endif
