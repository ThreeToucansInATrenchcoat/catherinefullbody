#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <new>
namespace submit_audit29 {
enum Counter { submits,texture_waits,texture_wait_ns,replay_jobs,replay_ns,
    texture_copies,nested_calls,prepare_ns,present_waits,present_wait_ns,queue_delay_ns, count };
struct alignas(64) State {
    uint64_t magic=0x3932544944554153ull;
    uint32_t version=1,pid=GetCurrentProcessId();
    uint64_t reserved[6]{};
    std::atomic<uint64_t> counters[32]{};
};
static_assert(offsetof(State,counters)==64);
inline State* state() {
    static State* s=[]()->State* {
        const char* env=std::getenv("CATHERINE_SUBMIT_AUDIT");
        if(!env || env[0]!='1')return nullptr;
        wchar_t name[128];std::swprintf(name,128,L"Local\\CatherineSubmitAudit29-%lu",GetCurrentProcessId());
        HANDLE h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(State),name);
        if(!h)return nullptr;
        void* memory=MapViewOfFile(h,FILE_MAP_ALL_ACCESS,0,0,sizeof(State));
        if(!memory){CloseHandle(h);return nullptr;}
        // Owned process-lifetime diagnostic map, never reused by another PID.
        return new(memory)State;
    }();return s;
}
inline void add(Counter field,uint64_t value=1){if(auto*s=state())s->counters[field].fetch_add(value,std::memory_order_relaxed);}
inline thread_local uint64_t copies=0,calls=0;
inline void begin(){copies=calls=0;}
inline void end(uint64_t elapsed){if(state()){add(replay_jobs);add(replay_ns,elapsed);add(texture_copies,copies);add(nested_calls,calls);}}
inline void copy(){if(state())++copies;}
inline void call(){if(state())++calls;}
}
