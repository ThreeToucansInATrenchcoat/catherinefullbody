#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CatherineResolutionStatus {
    CATHERINE_RESOLUTION_IDLE = 0,
    CATHERINE_RESOLUTION_PENDING = 1,
    CATHERINE_RESOLUTION_APPLYING = 2,
    CATHERINE_RESOLUTION_APPLIED = 3,
    CATHERINE_RESOLUTION_REJECTED = 4
};
/* UI thread: Auto must first be resolved to one supported height. */
uint64_t catherine_resolution_request(unsigned height);
unsigned catherine_resolution_applied_height(void);
unsigned catherine_resolution_status(void);
uint64_t catherine_resolution_completed_generation(void);

/* Guest main thread only. No calls to guest code from the UI thread. */
int catherine_resolution_claim_event(void);
int catherine_resolution_before_apply(void *cpu);
void catherine_resolution_after_apply(void *cpu);
unsigned catherine_resolution_selected_height(void);

/* Integration gates: preflight must make allocation success provable BEFORE
 * destruction. The provider reserves all nine backing blocks using native getters
 * and the native allocator before allowing teardown. Both callbacks preserve the
 * complete guest CPU/register state. */
int catherine_resolution_heap_prepare(void *cpu, unsigned height);
int catherine_resolution_heap_finish(void *cpu, unsigned height);
void catherine_resolution_allocate_heap(void *cpu);
/* prepare:1 reserved,0 insufficient capacity,-1 invalid/unsupported metadata. */
int catherine_resolution_take_allocation(void *cpu);
int catherine_resolution_heap_active(void);
unsigned catherine_resolution_native_height(void); /* guest-thread only */
int catherine_resolution_trace_enabled(void);
int catherine_resolution_drain(void);
#ifdef __cplusplus
}
#endif
