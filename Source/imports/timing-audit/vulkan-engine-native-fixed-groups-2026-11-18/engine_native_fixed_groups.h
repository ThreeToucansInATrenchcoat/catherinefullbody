#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CatherineFixedCallback{CatherineFixedBlend=0,CatherineFixedColor=1,CatherineFixedChannels=2,CatherineFixedBlendColor=3,CatherineFixedDepth=4,CatherineFixedStencilWrite=5,CatherineFixedStencilCompare=6,CatherineFixedStencilRef=7,CatherineFixedBounds=8,CatherineFixedStencilAny=9};
typedef struct CatherineEngineFixedValues{uint32_t words[12];}CatherineEngineFixedValues;
/* Positive result is the verified actual operation kind+1, zero means fallback. */
int catherine_fixed_callback(uint32_t expected,uint64_t callback);
void catherine_engine_emit_fixed(uint64_t command,uint32_t kind,const CatherineEngineFixedValues*values,int parity);
#ifdef __cplusplus
}
#endif
