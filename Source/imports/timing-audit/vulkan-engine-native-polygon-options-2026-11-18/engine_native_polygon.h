#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CatherinePolygonCallback{CatherinePolygonState=0,CatherinePolygonBias=1,CatherinePolygonMultisample=2};
typedef struct CatherineEnginePolygonValues{uint32_t words[4];}CatherineEnginePolygonValues;
int catherine_polygon_callback(uint32_t kind,uint64_t callback);
void catherine_engine_emit_polygon(uint64_t command,uint32_t kind,const CatherineEnginePolygonValues*values,int parity);
#ifdef __cplusplus
}
#endif
