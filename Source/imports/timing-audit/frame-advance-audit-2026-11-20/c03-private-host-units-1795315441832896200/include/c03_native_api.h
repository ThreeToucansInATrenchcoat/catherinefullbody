#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CatherineC03RigidPacket {
 uint64_t actor,component,model,sdk,mesh,node,association,continuity,serial,address,size;
 uint32_t slot,version;
 uint32_t words[16];
} CatherineC03RigidPacket;
int catherine_c03_c3_snapshot(uint64_t command,uint64_t address,uint64_t size,CatherineC03RigidPacket* out);
void catherine_c03_backing_retired(uint64_t backing);
void catherine_c03_command_retired(uint64_t command);
#ifdef __cplusplus
}
#endif
