#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CatherinePolygonOption {CatherinePolygonSampleMask=0,CatherinePolygonDepthClamp=1,CatherinePolygonDiscard=2,CatherinePolygonConservative=3};
int catherine_polygon_option_callback(uint32_t kind,uint64_t address);
void catherine_engine_emit_polygon_option(uint64_t command,uint32_t kind,uint32_t value,int diagnostic);
#ifdef __cplusplus
}
#endif
