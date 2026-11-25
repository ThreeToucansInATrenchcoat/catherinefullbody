#include "engine_native_texture.h"
uint64_t catherine_engine_texture_handle(uint64_t device,uint32_t texture,uint32_t sampler){
 (void)device;return (uint64_t)(texture&0xfffffu)|((uint64_t)(sampler&0xfffu)<<20);
}
