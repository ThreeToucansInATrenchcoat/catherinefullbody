#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include "C:/catherine/recomp/Source/platform/guest_host.h"
#endif

typedef struct {
    HANDLE host;
    void *tls;
    int closed;
    int reserved;
} CatherineThreadLifetime;

// Callers serialize registry operations. A closed guest handle does not end its
// host thread: its Cpu and TLS remain owned until Windows reports completion.
static inline void catherine_thread_lifetime_discard(CatherineThreadLifetime *slot){
    if(slot->host)CloseHandle(slot->host);
    if(slot->tls)VirtualFree(slot->tls,0,MEM_RELEASE);
    *slot=(CatherineThreadLifetime){0};
}

static inline int catherine_thread_lifetime_acquire(CatherineThreadLifetime *const *slots,unsigned count){
    for(unsigned i=0;i<count;++i){
        CatherineThreadLifetime *slot=slots[i];
        if(slot->reserved)continue;
        if(slot->host&&(!slot->closed||WaitForSingleObject(slot->host,0)!=WAIT_OBJECT_0))continue;
        catherine_thread_lifetime_discard(slot);
        slot->reserved=1;
        return (int)i;
    }
    return -1;
}

static inline HANDLE catherine_thread_lifetime_handle(const CatherineThreadLifetime *slot){
    return slot->closed||slot->reserved?NULL:slot->host;
}

static inline void catherine_thread_lifetime_publish(CatherineThreadLifetime *slot,HANDLE host){
    slot->host=host;slot->closed=0;slot->reserved=0;
}
