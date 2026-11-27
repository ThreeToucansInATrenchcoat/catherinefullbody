#pragma once
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <immintrin.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace experimental_sync {
// BasicLockable mutex: one atomic fast path, a short bounded spin, then park.
// State 2 retains the wake obligation for other sleepers after acquisition.
class alignas(64) BridgeMutex {
    static_assert(std::atomic<uint32_t>::is_always_lock_free&&sizeof(std::atomic<uint32_t>)==4);
    alignas(4) std::atomic<uint32_t> state{0};std::mutex original;
    static bool native(){
#ifdef _WIN32
        static const bool on=[] {const auto* e=std::getenv("CATHERINE_ROOT9_FAST_MUTEX");return !e||e[0]!='0';}();return on;
#else
        return false;
#endif
    }
public:
    void lock(){
        if(!native()){original.lock();return;}
        uint32_t expected=0;
        if(state.compare_exchange_strong(expected,1,std::memory_order_acquire,std::memory_order_relaxed))return;
        for(unsigned n=0;n<8;++n){
            _mm_pause();expected=0;
            if(state.compare_exchange_weak(expected,1,std::memory_order_acquire,std::memory_order_relaxed))return;
        }
        uint32_t previous=state.exchange(2,std::memory_order_acquire);
        while(previous){
#ifdef _WIN32
            uint32_t waiting=2;if(!WaitOnAddress(&state,&waiting,sizeof waiting,INFINITE))SwitchToThread();
#else
            state.wait(2,std::memory_order_relaxed);
#endif
            previous=state.exchange(2,std::memory_order_acquire);
        }
    }
    void unlock(){
        if(!native()){original.unlock();return;}
        if(state.exchange(0,std::memory_order_release)==2){
#ifdef _WIN32
            WakeByAddressSingle(&state);
#else
            state.notify_one();
#endif
        }
    }
    BridgeMutex()=default;BridgeMutex(const BridgeMutex&)=delete;BridgeMutex& operator=(const BridgeMutex&)=delete;
};
}
