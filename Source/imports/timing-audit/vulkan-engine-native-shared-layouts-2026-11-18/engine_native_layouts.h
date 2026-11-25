#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CatherineEngineAttrib {uint32_t format,offset,stream;} CatherineEngineAttrib;
typedef struct CatherineEngineStream {uint32_t stride,divisor;} CatherineEngineStream;
int catherine_layout_callback(uint32_t kind,uint64_t address);
void catherine_engine_emit_attributes(uint64_t command,uint32_t count,const CatherineEngineAttrib*values,int diagnostic);
void catherine_engine_emit_streams(uint64_t command,uint32_t count,const CatherineEngineStream*values,int diagnostic);
#ifdef __cplusplus
}
#endif
