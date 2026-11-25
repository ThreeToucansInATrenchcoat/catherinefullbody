#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../../../../platform/host_clock.h"
#endif
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "sync.h"
typedef struct Waiter {
    struct Waiter *next;
    uint64_t address;
    uint32_t handle;
    unsigned kind;
    int woken;
#ifdef _WIN32
    HANDLE event;
    int close_event, sleeping;
#endif
} Waiter;
#include <immintrin.h>
#ifdef _WIN32
/* cmd99: wake a registered waiter (under `lock`): SetEvent only when it is (about to be) blocked in the kernel. */
static void wake_waiter(Waiter *w) {
    __atomic_store_n(&w->woken, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&w->sleeping, __ATOMIC_SEQ_CST)) SetEvent(w->event);
}
static int64_t spin_ns(void) {
    static int64_t v = -1;
    if (v < 0) {
        const char *e = getenv("CATHERINE_PARALLEL99_SPIN");
        long us = e ? atol(e) : 150;
        v = us < 0 ? 0 : us > 2000 ? 2000000 : (int64_t)us * 1000;
    }
    return v;
}
static SRWLOCK lock = SRWLOCK_INIT;
static Waiter *waiters;
static DWORD wait_event_slot = FLS_OUT_OF_INDEXES;

static VOID CALLBACK destroy_wait_event(PVOID event) {
    CloseHandle((HANDLE)event);
}

/* The wait list lock also serializes slot initialization and event reset. */
static void prepare_wait_event(Waiter *w) {
    if (wait_event_slot == FLS_OUT_OF_INDEXES)
        wait_event_slot = FlsAlloc(destroy_wait_event);
    if (wait_event_slot != FLS_OUT_OF_INDEXES)
        w->event = (HANDLE)FlsGetValue(wait_event_slot);
    if (w->event) {
        /* A signal can race with timeout before the old waiter unlinks. */
        if (!ResetEvent(w->event)) exit(4);
        return;
    }

    w->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!w->event) exit(4);
    w->close_event = wait_event_slot == FLS_OUT_OF_INDEXES ||
                     !FlsSetValue(wait_event_slot, w->event);
}
static uint32_t sleep_waiter(Waiter *w, int64_t ns) {
    DWORD timeout=ns<0?INFINITE:(uint64_t)ns/1000000>=0xfffffffe?0xfffffffe:(DWORD)(((uint64_t)ns+999999)/1000000);
    ReleaseSRWLockExclusive(&lock);
    DWORD status=WAIT_OBJECT_0;
    int64_t spin=spin_ns();
    int64_t spun_ns127=0;
    if(spin && ns!=0){
        if(ns>0 && ns<spin) spin=ns;
        LARGE_INTEGER f,t0,t;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
        const int64_t ticks=(int64_t)((double)spin*(double)f.QuadPart/1e9);
        for(;;){
            if(__atomic_load_n(&w->woken,__ATOMIC_SEQ_CST)) break;
            QueryPerformanceCounter(&t);
            spun_ns127=(int64_t)((double)(t.QuadPart-t0.QuadPart)*1e9/(double)f.QuadPart);
            if(t.QuadPart-t0.QuadPart>=ticks) break;
            for(int k=0;k<16;k++) _mm_pause();
        }
    }
    if(!__atomic_load_n(&w->woken,__ATOMIC_SEQ_CST)){
        __atomic_store_n(&w->sleeping,1,__ATOMIC_SEQ_CST);
        if(!__atomic_load_n(&w->woken,__ATOMIC_SEQ_CST)){
            DWORD left=timeout;  /* cmd127: the spin counts toward a finite timeout */
            if(timeout!=INFINITE && spun_ns127>0){ const uint64_t ms=(uint64_t)(spun_ns127/1000000); left = ms>=timeout?0:(DWORD)(timeout-ms); }
            status=WaitForSingleObject(w->event,left);
        }
    }
    AcquireSRWLockExclusive(&lock);
    Waiter **p=&waiters;
    while (*p!=w) p=&(*p)->next;
    *p=w->next;
    if (w->close_event) CloseHandle(w->event);
    return w->woken?0:status==WAIT_TIMEOUT?0xea01:0xe401;
}
#else
typedef int32_t LONG;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Waiter *waiters;

static void AcquireSRWLockExclusive(pthread_mutex_t *mutex) { pthread_mutex_lock(mutex); }
static void ReleaseSRWLockExclusive(pthread_mutex_t *mutex) { pthread_mutex_unlock(mutex); }
static LONG InterlockedCompareExchange(volatile LONG *word, LONG value, LONG expected) {
    __atomic_compare_exchange_n(word, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
static void InterlockedExchange(volatile LONG *word, LONG value) {
    __atomic_exchange_n(word, value, __ATOMIC_SEQ_CST);
}
static void wake_waiter(Waiter *w) {
    __atomic_store_n(&w->woken, 1, __ATOMIC_SEQ_CST);
    syscall(SYS_futex, &w->woken, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}

static uint32_t sleep_waiter(Waiter *w, int64_t ns) {
    const uint64_t start = catherine_host_nanoseconds();
    const uint64_t end = ns < 0 ? 0 : start + (uint64_t)ns;
    uint32_t status = 0xe401;
    pthread_mutex_unlock(&lock);
    while (!__atomic_load_n(&w->woken, __ATOMIC_SEQ_CST)) {
        struct timespec remaining;
        const struct timespec *timeout = NULL;
        if (ns >= 0) {
            uint64_t now = catherine_host_nanoseconds();
            if (now >= end) { status = 0xea01; break; }
            uint64_t left = end - now;
            remaining.tv_sec = left / UINT64_C(1000000000);
            remaining.tv_nsec = left % UINT64_C(1000000000);
            timeout = &remaining;
        }
        long result = syscall(SYS_futex, &w->woken, FUTEX_WAIT_PRIVATE, 0, timeout, NULL, 0);
        if (result < 0 && errno != EAGAIN && errno != EINTR) {
            if (errno == ETIMEDOUT) status = 0xea01;
            break;
        }
    }
    pthread_mutex_lock(&lock);
    Waiter **entry = &waiters;
    while (*entry != w) entry = &(*entry)->next;
    *entry = w->next;
    return w->woken ? 0 : status;
}
#endif

static void unlock_guest(uint64_t address) {
    Waiter *selected=NULL;unsigned count=0;
    for(Waiter *w=waiters;w;w=w->next) if(w->kind==1 && w->address==address && !w->woken){selected=w;count++;}
    InterlockedExchange((volatile LONG *)(uintptr_t)address, selected?selected->handle|(count>1?0x40000000u:0):0);
    if(selected) wake_waiter(selected);
}
static void lock_guest(uint64_t address, uint32_t handle) {
    volatile LONG *word = (volatile LONG *)(uintptr_t)address;
    for (;;) {
        LONG current = InterlockedCompareExchange(word, handle, 0);
        if (!current || ((uint32_t)current & 0x3fffffff) == handle) return;
        if (InterlockedCompareExchange(word, current | 0x40000000, current) != current) continue;

        Waiter w = {.address = address, .kind = 1, .handle = handle};

#ifdef _WIN32
        prepare_wait_event(&w);
#endif
        w.next = waiters;
        waiters = &w;
        sleep_waiter(&w, -1);
        return;
    }
}
enum { ADDRESS_WAITER = 3 };

static int address_can_wait(uint64_t address, uint32_t type, uint64_t value) {
    if (type == 3)
        return __atomic_load_n((volatile uint64_t *)(uintptr_t)address, __ATOMIC_SEQ_CST) == value;

    volatile LONG *word = (volatile LONG *)(uintptr_t)address;
    const LONG wanted = (LONG)value;
    LONG current = __atomic_load_n(word, __ATOMIC_SEQ_CST);
    if (type == 2) return current == wanted;
    if (type == 0) return current < wanted;
    while (current < wanted) {
        const LONG previous = InterlockedCompareExchange(word, (LONG)((uint32_t)current - 1), current);
        if (previous == current) return 1;
        current = previous;
    }
    return 0;
}

static uint32_t wait_address(uint64_t address, uint32_t type, uint64_t value, int64_t timeout) {
    if (type > 3) return 0xf001;
    if (address & (type == 3 ? 7 : 3)) return 0xcc01;
    if (!address) return 0xd401;
    AcquireSRWLockExclusive(&lock);
    uint32_t result = 0xfa01;
    if (address_can_wait(address, type, value)) {
        result = 0xea01;
        if (timeout) {
            Waiter w = {.address = address, .kind = ADDRESS_WAITER};

#ifdef _WIN32
            prepare_wait_event(&w);
#endif
            // Register before releasing the lock used by SignalToAddress.
            Waiter **tail = &waiters;
            while (*tail) tail = &(*tail)->next;
            *tail = &w;
            result = sleep_waiter(&w, timeout);
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return result;
}

static int waiting_on_address(const Waiter *w, uint64_t address) {
    return w->kind == ADDRESS_WAITER && w->address == address && !w->woken;
}

static uint32_t signal_address(uint64_t address, uint32_t type, LONG value, int32_t count) {
    if (type > 2) return 0xf001;
    if (address & 3) return 0xcc01;
    if (!address && type != 0) return 0xd401;
    AcquireSRWLockExclusive(&lock);
    uint32_t result = 0;
    if (type != 0) {
        uint32_t updated = (uint32_t)value + 1;
        if (type == 2) {
            int waiting = 0;
            for (Waiter *w = waiters; w; w = w->next)
                waiting += waiting_on_address(w, address);
            // With no waiter, leave the signal pending for the next wait.
            if (waiting) {
                updated = (uint32_t)value;
                if (count <= 0 || count >= waiting) --updated;
            }
        }
        volatile LONG *word = (volatile LONG *)(uintptr_t)address;
        const LONG previous = updated == (uint32_t)value
            ? __atomic_load_n(word, __ATOMIC_SEQ_CST)
            : InterlockedCompareExchange(word, (LONG)updated, value);
        if (previous != value) result = 0xfa01;
    }
    if (!result) {
        for (Waiter *w = waiters; w; w = w->next) {
            if (!waiting_on_address(w, address)) continue;
            wake_waiter(w);
            if (count > 0 && --count == 0) break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return result;
}

int sync_svc(Cpu *c,uint32_t svc,uint32_t handle) {
    if (svc == 0x34) { c->x[0] = wait_address(c->x[0], (uint32_t)c->x[1], c->x[2], (int64_t)c->x[3]); return 1; }
    if (svc == 0x35) { c->x[0] = signal_address(c->x[0], (uint32_t)c->x[1], (LONG)c->x[2], (int32_t)c->x[3]); return 1; }
    if(svc==0x1a){AcquireSRWLockExclusive(&lock);lock_guest(c->x[1],(uint32_t)c->x[2]);ReleaseSRWLockExclusive(&lock);c->x[0]=0;return 1;}
    if(svc==0x1b){AcquireSRWLockExclusive(&lock);unlock_guest(c->x[0]);ReleaseSRWLockExclusive(&lock);c->x[0]=0;return 1;}
    if(svc==0x1c){uint64_t mutex=c->x[0],condition=c->x[1];if((uint32_t)c->x[2]!=handle){c->x[0]=0xe401;return 1;}
        Waiter w={.address=condition,.kind=2,.handle=handle};
        AcquireSRWLockExclusive(&lock);
#ifdef _WIN32
        prepare_wait_event(&w);
#endif
        w.next=waiters;waiters=&w;InterlockedExchange((volatile LONG *)(uintptr_t)condition,1);
        unlock_guest(mutex);uint32_t result=sleep_waiter(&w,(int64_t)c->x[3]);lock_guest(mutex,handle);ReleaseSRWLockExclusive(&lock);c->x[0]=result;return 1;}
    if(svc==0x1d){uint64_t address=c->x[0];int count=(int32_t)c->x[1];unsigned signaled=0;int remaining=0;
        AcquireSRWLockExclusive(&lock);for(Waiter *w=waiters;w;w=w->next)if(w->kind==2&&w->address==address&&!w->woken&&(count<=0||signaled<(unsigned)count)){wake_waiter(w);signaled++;}
        for(Waiter *w=waiters;w;w=w->next)remaining|=w->kind==2&&w->address==address&&!w->woken;
        InterlockedExchange((volatile LONG *)(uintptr_t)address,remaining);ReleaseSRWLockExclusive(&lock);c->x[0]=0;return 1;}
    return 0;
}
