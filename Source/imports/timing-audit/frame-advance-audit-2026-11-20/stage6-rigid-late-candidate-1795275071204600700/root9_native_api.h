#pragma once
#include "root9_rigid_transport.h"
#include "root9_replay_scope.h"
extern "C" {
int catherine_root9_copy_owner(uint64_t,uint32_t,uint64_t,CatherineBlock20Packet*);
int catherine_root9_owner_live(const CatherineBlock20Packet*);
int catherine_root9_has_owners(void);
void catherine_root9_model_retired(uint64_t);
void catherine_root9_rigid_copied(uint64_t,uint32_t,uint64_t);
void catherine_root9_c3_loaded(uint64_t);
void catherine_root9_c3_uploaded(uint64_t,uint64_t,uint64_t);
void catherine_root9_c3_buffer_address(uint64_t,uint64_t);
int catherine_root9_c3_snapshot(uint64_t,uint64_t,uint64_t,catherine::root9::RigidPacket*);
int catherine_root9_c3_force_bind(void);
}
