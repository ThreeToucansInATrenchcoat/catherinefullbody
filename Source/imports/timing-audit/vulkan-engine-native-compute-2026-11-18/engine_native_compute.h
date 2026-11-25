#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CatherineComputeCallback {
    CatherineComputeAddress=0,CatherineComputeProgram=1,CatherineComputeUniform=2,
    CatherineComputeStorage=3,CatherineComputeDispatch=4
};
typedef struct CatherineComputeRange {
    uint64_t address,size;
    uint32_t stage,slot;
} CatherineComputeRange;
typedef struct CatherineComputeDispatchSize {uint32_t x,y,z;} CatherineComputeDispatchSize;
int catherine_compute_callback(uint32_t kind,uint64_t callback);
int catherine_compute_preflight(const uint64_t* callbacks);
uint64_t catherine_compute_buffer_address(uint64_t buffer);
void catherine_compute_emit_program(uint64_t command,uint64_t program,uint32_t stages);
void catherine_compute_emit_range(uint64_t command,int storage,const CatherineComputeRange* range,int parity);
void catherine_compute_emit_dispatch(uint64_t command,const CatherineComputeDispatchSize* size,int parity);
#ifdef __cplusplus
}
#endif
