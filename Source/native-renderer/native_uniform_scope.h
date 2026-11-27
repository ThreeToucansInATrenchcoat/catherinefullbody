#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
extern uint64_t catherine_native_record_scope_begin(void);
extern void catherine_native_record_scope_end(uint64_t);
extern void catherine_native_record_scope_abandon(void);
static int catherine_uniform_scope_mode(void) {
    static _Thread_local int mode = -1;
    if (mode < 0) {
        const char *value = getenv("CATHERINE_NATIVE_UNIFORM_SCOPE");
        mode = value ? atoi(value) : 4;
    }
    return mode;
}
static uint64_t catherine_uniform_scope_begin(unsigned kind) {
    const int mode=catherine_uniform_scope_mode();
    return (mode==1 || (mode==4&&kind==0)) ? catherine_native_record_scope_begin() : 0;
}
static void catherine_uniform_scope_after_mask(uint64_t *token, uint32_t mask, unsigned kind) {
    const int mode = catherine_uniform_scope_mode();
    if (mode >= 2 && !(mode==4&&kind==0) && (mask & (mask - 1))) *token = catherine_native_record_scope_begin();
    if (mode == 3) {
        static _Thread_local uint64_t groups[2], counts[2][33];
        ++counts[kind][__builtin_popcount(mask)];
        if (++groups[kind] % 1000000 == 0) {
            fprintf(stderr,"Native uniform masks: kind %u groups %llu counts",kind,(unsigned long long)groups[kind]);
            for (unsigned k=0;k<=32;++k) fprintf(stderr," %llu",(unsigned long long)counts[kind][k]);
            fprintf(stderr,"\n");
        }
    }
}
static void catherine_uniform_scope_texture_boundary(uint64_t token) {
    if (catherine_uniform_scope_mode()) {
        catherine_native_record_scope_end(token);
        // Texture construction can allocate guest memory. Cancel any inherited
        // lease too; an outer token must never restore it after this boundary.
        catherine_native_record_scope_abandon();
    }
}
