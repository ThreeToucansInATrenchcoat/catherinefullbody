#include <stdint.h>
#include "c02_movement_bridge.h"
#include "c04_movement_bridge.h"
#include "root15_palette_transport.h"
#include "root9_native_api.h"

extern "C" {
void catherine_p1_movement_begin(uint64_t, uint64_t, uint64_t, uint64_t);
void catherine_p1_movement_published(uint64_t);
void catherine_p1_model_retired(uint64_t);
void catherine_p1_palette_copied(uint64_t, uint32_t, uint64_t, uint32_t);
void catherine_p1_rigid_copied(uint64_t, uint32_t, uint64_t);
void catherine_p1_c3_loaded(uint64_t);
void catherine_p1_c3_uploaded(uint64_t, uint64_t, uint64_t);
void catherine_p1_c3_buffer_address(uint64_t, uint64_t);
int catherine_p1_c3_record_owned(uint64_t, uint64_t);
int catherine_p1_c3_force_bind(void);

void catherine_c02_movement_begin(uint64_t model, uint64_t input,
                                  uint64_t caller, uint64_t apply) {
    catherine_p1_movement_begin(model, input, caller, apply);
    catherine_c04_movement_begin(model, input, caller, apply);
}
void catherine_c02_movement_published(uint64_t model) {
    catherine_p1_movement_published(model);
    catherine_c04_movement_published(model);
}
void catherine_c02_model_retired(uint64_t model) {
    catherine_p1_model_retired(model);
    catherine_c04_model_retired(model);
    catherine_root9_model_retired(model);
}
void catherine_c02_palette_copied(uint64_t parent, uint32_t slot,
                                 uint64_t destination, uint32_t count) {
    catherine_p1_palette_copied(parent, slot, destination, count);
    catherine_c04_palette_copied(parent, slot, destination, count);
    catherine_block15_palette_copied(parent,slot,destination,count);
}
void catherine_c02_rigid_copied(uint64_t mesh, uint32_t slot, uint64_t destination) {
    catherine_p1_rigid_copied(mesh, slot, destination);
    catherine_c04_rigid_copied(mesh, slot, destination);
    catherine_root9_rigid_copied(mesh,slot,destination);
}
void catherine_c02_c3_loaded(uint64_t source) {
    catherine_p1_c3_loaded(source);
    catherine_c04_c3_loaded(source);
    catherine_root9_c3_loaded(source);
}
void catherine_c02_c3_uploaded(uint64_t destination, uint64_t backing, uint64_t offset) {
    catherine_p1_c3_uploaded(destination, backing, offset);
    catherine_c04_c3_uploaded(destination, backing, offset);
    catherine_root9_c3_uploaded(destination,backing,offset);
}
void catherine_c02_c3_buffer_address(uint64_t backing, uint64_t base) {
    catherine_p1_c3_buffer_address(backing, base);
    catherine_c04_c3_buffer_address(backing, base);
    catherine_root9_c3_buffer_address(backing,base);
}
int catherine_c02_c3_record_owned(uint64_t address, uint64_t bytes) {
    const int p1 = catherine_p1_c3_record_owned(address, bytes);
    const int c04 = catherine_c04_c3_record_owned(address, bytes);
    // Both calls publish their family's current ownership transition.
    return p1 | c04;
}
int catherine_c02_c3_force_bind(void) {
    const int p1 = catherine_p1_c3_force_bind();
    const int c04 = catherine_c04_c3_force_bind();
    return p1 | c04 | catherine_root9_c3_force_bind();
}
}
