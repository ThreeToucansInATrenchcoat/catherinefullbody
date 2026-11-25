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
typedef struct CatherineC03RigidStatus {uint64_t association,continuity,upload_ranges;uint32_t reserved,ready_copies,armed,padding;uint64_t first_scalar_clock,last_scalar_clock,scalar_samples;uint32_t scalar_kind,scalar_native_bits,scalar_result_bits,diagnostic_padding;} CatherineC03RigidStatus;
int catherine_c03_c3_status(uint64_t actor,CatherineC03RigidStatus* out);
void catherine_c03_rigid_copied(uint64_t,uint32_t,uint64_t);
void catherine_c03_c3_loaded(uint64_t);
void catherine_c03_c3_uploaded(uint64_t,uint64_t,uint64_t,int);
int catherine_c03_c3_force_bind(uint64_t);
void catherine_c03_c3_buffer_address(uint64_t,uint64_t,uint64_t,uint64_t);
void catherine_c03_backing_retired(uint64_t);
void catherine_c03_command_retired(uint64_t);
#ifdef __cplusplus
}
#endif
