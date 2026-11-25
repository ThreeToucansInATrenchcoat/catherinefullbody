#pragma once
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

// Native SDK full-key Npad ring layout; separate entries are 0x5000 bytes apart.
static inline void catherine_publish_npad(unsigned char *entry,uint64_t sample,uint64_t slot,
                                         uint64_t buttons,const int32_t *sticks,unsigned connected) {
    volatile uint64_t *header=(volatile uint64_t *)(entry+0x28);
    unsigned char *record=entry+0x28+0x20+slot*0x30;
    *(uint32_t *)entry=connected?1:0;
    *(uint64_t *)(record+8)=sample;
    *(uint64_t *)(record+16)=connected?buttons:0;
    if(connected)memcpy(record+24,sticks,16);else memset(record+24,0,16);
    *(uint32_t *)(record+40)=connected?1:0;
    MemoryBarrier();*(volatile uint64_t *)record=sample;MemoryBarrier();
    header[2]=slot;header[3]=sample<17?sample:17;
}
#ifndef _WIN32
#undef MemoryBarrier
#endif
