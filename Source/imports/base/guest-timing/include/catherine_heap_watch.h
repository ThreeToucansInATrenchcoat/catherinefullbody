#pragma once

#include <stdint.h>
#include <stddef.h>

#define CATHERINE_HEAP_WATCH_BASE UINT64_C(0x1000000000)
#define CATHERINE_HEAP_WATCH_SIZE (UINT64_C(4) << 30)

#ifdef __cplusplus
extern "C" {
#endif

int catherine_heap_watch_active(void);
int catherine_heap_watch_begin(uint64_t address, uint64_t size, uint64_t* allocation_epoch);
void catherine_heap_watch_end(void);
#ifndef _WIN32
int catherine_heap_watch_query(uint64_t address, uint64_t bytes, void **pages, size_t *count);
#endif

#ifdef __cplusplus
}
#endif
