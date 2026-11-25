#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CatherineViewportCallback{CatherineDepthScalar=0,CatherineViewports=1,CatherineScissors=2,CatherineDepthRanges=3};
/* Exact float/int bits copied while guest values are valid. No guest pointers. */
typedef struct CatherineEngineRasterValues{uint32_t bits[4];}CatherineEngineRasterValues;
int catherine_viewport_callback(uint32_t kind,uint64_t callback);
void catherine_engine_emit_raster(uint64_t command,uint32_t kind,const CatherineEngineRasterValues* values,int parity);
#ifdef __cplusplus
}
#endif
