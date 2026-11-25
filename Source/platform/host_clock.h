#pragma once

#include <stdint.h>

#ifdef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
#ifdef __cplusplus
}
#endif
static inline uint64_t catherine_host_milliseconds(void) {
    return GetTickCount64();
}
#else
#include <time.h>

static inline uint64_t catherine_host_nanoseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static inline uint64_t catherine_host_milliseconds(void) {
    return catherine_host_nanoseconds() / UINT64_C(1000000);
}
#endif
