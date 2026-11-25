#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct C03Context {
    uint64_t root, manager, node, sentinel, actor, component, wrapper;
    uint64_t model, sdk, authored_root, synthetic_root;
    uint64_t actor_vptr, component_vptr, manager_vptr, model_vptr;
    uint32_t kind, variant, resource;
    uint32_t member_proven, ready;
} C03Context;
typedef struct C03Admission {
    C03Context context;
    uint64_t actor_birth, model_birth, resource_cookie;
    uint64_t association_serial, continuity_serial;
} C03Admission;

#ifdef __cplusplus
extern "C" {
#endif
void catherine_c03_actor_birth(uint64_t);
void catherine_c03_actor_retire(uint64_t);
void catherine_c03_actor_reset(uint64_t);
void catherine_c03_component_reset(uint64_t);
void catherine_c03_model_birth(uint64_t);
void catherine_c03_model_retire(uint64_t);
void catherine_c03_resource_loaded(uint64_t,uint64_t,const char*,uint32_t,uint32_t,uint32_t);
void catherine_c03_native_observe(uint64_t,uint64_t,uint64_t,uint64_t);
uint32_t catherine_c03_scalar_native(uint64_t actor,uint64_t actual_model,uint64_t source_lr,uint64_t one_clock,uint32_t authored_yaw_bits,uint32_t native_euler_bits,uint32_t ROM_factor_bits,int native60,int config_high,uint32_t framework_bits);
void catherine_c03_registry_reset(void);
#ifdef __cplusplus
}
#endif
