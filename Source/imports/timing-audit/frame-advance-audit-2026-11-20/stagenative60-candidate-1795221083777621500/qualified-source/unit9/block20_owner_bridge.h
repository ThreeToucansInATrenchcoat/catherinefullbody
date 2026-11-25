#pragma once
#include <stdint.h>
typedef struct CatherineBlock20Packet{
 uint64_t valid,block,generation,lifetime,model,parent,table,entry,entry_value,palette_address,node,clock;
 uint32_t type,subtype,variant,flags;
 float world[16];
}CatherineBlock20Packet;
#ifdef __cplusplus
extern "C" {
#endif
void catherine_block20_owner_sync(uint64_t block);
void catherine_block20_owner_constructed(uint64_t block);
void catherine_block20_owner_forget(uint64_t block);
void catherine_block20_job_enter(uint64_t row,uint64_t palette_address);
void catherine_block20_job_leave(void);
int catherine_block20_job_snapshot(CatherineBlock20Packet*);
#ifdef __cplusplus
}
#endif
