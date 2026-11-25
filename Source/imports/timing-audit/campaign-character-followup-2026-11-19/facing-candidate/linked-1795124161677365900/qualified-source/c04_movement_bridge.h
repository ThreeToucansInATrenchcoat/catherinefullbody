#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private candidate. No model, actor or SDK-node source transform is changed. */
void catherine_c04_movement_begin(uint64_t model, uint64_t input_vector,
                                  uint64_t caller, uint64_t apply);
void catherine_c04_movement_published(uint64_t model);
void catherine_c04_model_retired(uint64_t model);
/* Fresh render copies only: palette is row-major; rigid world is SDK column-major. */
void catherine_c04_palette_copied(uint64_t sdk_parent, uint32_t pass_slot,
                                 uint64_t destination_cpu, uint32_t matrix_count);
void catherine_c04_rigid_copied(uint64_t mesh_instance, uint32_t pass_slot,
                               uint64_t destination_cpu);
/* Set/clear for EVERY native C3 matrix load; owned state cannot survive replacement. */
void catherine_c04_c3_loaded(uint64_t matrix_source_cpu);
/* Called for EVERY completed native C3 UBO upload, including unowned ones. */
void catherine_c04_c3_uploaded(uint64_t destination_cpu, uint64_t backing_nvn_buffer,
                              uint64_t buffer_offset);
/* Observe the ORIGINAL native BufferGetAddress return, without an extra lookup. */
void catherine_c04_c3_buffer_address(uint64_t backing_nvn_buffer, uint64_t gpu_base);
/* Recording-time ownership only. The result is copied into the bind command. */
int catherine_c04_c3_record_owned(uint64_t gpu_address, uint64_t size);
/* Preserve ownership transitions even when the native UBO address is unchanged. */
int catherine_c04_c3_force_bind(void);
#ifdef __cplusplus
}
#endif
