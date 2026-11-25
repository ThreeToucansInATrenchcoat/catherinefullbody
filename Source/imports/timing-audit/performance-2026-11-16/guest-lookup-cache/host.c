#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef CATHERINE_HIGH_REFRESH_BUILD
#include "catherine_heap_watch.h"
#endif
#include <stdio.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <time.h>
#include "cpu.h"
#include "thread_affinity.h"
#include "thread_lifetime.h"
#include "layout.h"
#include "ipc.h"
#include "sync.h"

#ifdef CATHERINE_HIGH_REFRESH_BUILD
#include "catherine_refresh.h"
#endif
#include "graphics.h"
#include "guest-timing/include/catherine_npad_publish.h"
#include "movie_guest.h"
#include "asset_pak.h"
#define TABLE(n) extern const uint64_t n##_fn_addrs[], n##_fn_count; extern const GuestFn n##_fn_ptrs[];
TABLE(main) TABLE(subsdk0) TABLE(subsdk1) TABLE(sdk)
static struct {const uint64_t *addresses, *count; const GuestFn *functions;} tables[] = {
 {main_fn_addrs,&main_fn_count,main_fn_ptrs}, {subsdk0_fn_addrs,&subsdk0_fn_count,subsdk0_fn_ptrs},
 {subsdk1_fn_addrs,&subsdk1_fn_count,subsdk1_fn_ptrs}, {sdk_fn_addrs,&sdk_fn_count,sdk_fn_ptrs}};
// The linked guest tables and their module priority are immutable for this process.
// Cache only successful nonnull results. Resolve before dispatch so reentrant
// calls/fibers may replace the same TLS slot without changing the outer call.
static int guest_lookup_cache_enabled=1;
static _Thread_local struct {uint64_t address;GuestFn function;} guest_lookup_cache[1024];
static int guest_lookup_cached(uint64_t address,GuestFn *result) {
    unsigned slot=(unsigned)((address>>2)^(address>>20))&1023u;
    if(guest_lookup_cache_enabled && guest_lookup_cache[slot].function && guest_lookup_cache[slot].address==address) {
        *result=guest_lookup_cache[slot].function;return 1;
    }
    for(unsigned i=0;i<4;i++) {
        uint64_t l=0,h=*tables[i].count;
        while(l<h) {
            uint64_t m=l+(h-l)/2,a=tables[i].addresses[m];
            if(a==address) {
                GuestFn function=tables[i].functions[m];
                if(guest_lookup_cache_enabled && function) {
                    guest_lookup_cache[slot].address=address;
                    guest_lookup_cache[slot].function=function;
                }
                *result=function;return 1;
            }
            if(a<address)l=m+1;else h=m;
        }
    }
    return 0;
}
volatile int g_fnprof=0;
static _Thread_local uint64_t last_pc;
static volatile uint64_t thread_pc[65], thread_lr[65], thread_x0[65];
static _Thread_local unsigned thread_slot;
static int trace_boot;
static LONG boot_trace_lines;
static volatile LONG logo_sequence_started;

static void trace_boot_task(uint64_t pc, uint64_t task) {
    uint64_t handle, data;
    uint32_t state[2];
    HANDLE self = GetCurrentProcess();
    if (!ReadProcessMemory(self, (void *)(uintptr_t)(task + 8), &handle, sizeof handle, NULL) ||
        !ReadProcessMemory(self, (void *)(uintptr_t)(handle + 16), &data, sizeof data, NULL) ||
        !ReadProcessMemory(self, (void *)(uintptr_t)(data + 8), state, sizeof state, NULL)) return;
    static _Thread_local uint64_t previous[2] = {UINT64_MAX, UINT64_MAX};
    unsigned index = pc == 0x7000073c60ull;
    uint64_t value = (uint64_t)state[0] << 32 | state[1];
    if (value == previous[index]) return;
    previous[index] = value;
    fprintf(stderr, "Boot %s phase %u step %u\n", index ? "introduced task" : "init read task",
            state[0], state[1]);
}
typedef struct Fiber Fiber;
struct Fiber { uint64_t guest; void *host; Cpu cpu; Fiber *next; };
static _Thread_local Fiber *fibers;
static _Thread_local Fiber *current_fiber;
static _Thread_local void *root_fiber;
static void WINAPI fiber_main(void *argument);
void fnprof_hit(uint64_t pc, uint64_t lr, uint64_t x0) {
    last_pc = pc;
    thread_pc[thread_slot] = pc;
    thread_lr[thread_slot] = lr;
    thread_x0[thread_slot] = x0;
    if (!trace_boot) return;
    if (pc == 0x70000f6360ull && lr == 0x70002361fcull) {
        static _Thread_local uint64_t last_movie_state = UINT64_MAX;
        const volatile uint32_t *player = (const volatile uint32_t *)(uintptr_t)0x7000d0637cull;
        uint64_t state = (uint64_t)player[1] << 32 | player[0];
        if (state != last_movie_state) {
            last_movie_state = state;
            fprintf(stderr, "Story movie %u state %u\n", (uint32_t)(state >> 32), (uint32_t)state);
        }
    } else if (pc == 0x7000344e10ull && lr == 0x7000344b00ull) {
        static _Thread_local unsigned logged;
        if (logged++ < 4) {
            const unsigned char *planes = (const void *)(uintptr_t)x0;
            uint64_t y, uv, dest;
            uint32_t dimensions[2], stride, uv_stride, dest_layout[3];
            memcpy(&y, planes + 8, sizeof y);
            memcpy(dimensions, planes + 0x10, sizeof dimensions);
            memcpy(&stride, planes + 0x18, sizeof stride);
            memcpy(&uv, planes + 0x20, sizeof uv);
            memcpy(&uv_stride, planes + 0x30, sizeof uv_stride);
            memcpy(&dest, planes - 0x90, sizeof dest);
            memcpy(dest_layout, planes - 0x88, sizeof dest_layout);
            fprintf(stderr, "Movie converter %ux%u Y=%#llx stride=%u UV=%#llx stride=%u to %#llx %ux%u stride=%u\n",
                    dimensions[0], dimensions[1], y, stride, uv, uv_stride,
                    dest, dest_layout[0], dest_layout[1], dest_layout[2]);
        }
    } else if (pc == 0x70000f5780ull || pc == 0x7000290b60ull || pc == 0x700034cef0ull ||
               pc == 0x700033c550ull) {
        fprintf(stderr, "Movie call %#llx from %#llx x0=%#llx\n", pc, lr, x0);
    } else if (pc == 0x70000733a0ull || pc == 0x7000073c60ull) {
        trace_boot_task(pc, x0);
    } else if (pc == 0x7000296ba0ull || pc == 0x700028ff40ull) {
        InterlockedExchange(&logo_sequence_started, 1);
        fprintf(stderr, "Boot logo routine %#llx from %#llx\n", pc, lr);
    } else if ((pc == 0x70002f0460ull || pc == 0x700009ec60ull) &&
               InterlockedIncrement(&boot_trace_lines) <= 200) {
        MEMORY_BASIC_INFORMATION page;
        const char *argument = (const char *)(uintptr_t)x0;
        if (VirtualQuery(argument, &page, sizeof page) && page.State == MEM_COMMIT &&
            !(page.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            size_t readable = (uintptr_t)page.BaseAddress + page.RegionSize - x0;
            fprintf(stderr, "Boot call %#llx from %#llx: %.*s\n", pc, lr,
                    (int)(readable < 100 ? readable : 100), argument);
        }
    }
}
void rt_dispatch(Cpu *c) {
 if(c->pc==MOD_SDK_BASE+0x338e00) {
     if(getenv("CATHERINE_TRACE_THREAD_AFFINITY")){
         uint64_t current_type=0;uint32_t target_handle=UINT32_MAX,current_handle=UINT32_MAX;
         HANDLE process=GetCurrentProcess();
         ReadProcessMemory(process,(void *)(uintptr_t)(c->tpidrro+0x1f8),&current_type,sizeof current_type,NULL);
         if(current_type)ReadProcessMemory(process,(void *)(uintptr_t)(current_type+0x1b0),&current_handle,sizeof current_handle,NULL);
         if(c->x[1])ReadProcessMemory(process,(void *)(uintptr_t)(c->x[1]+0x1b0),&target_handle,sizeof target_handle,NULL);
         fprintf(stderr,"THREAD_AFFINITY target=%#llx handle=%#x core=%d mask=%#llx lr=%#llx fp=%#llx sp=%#llx tls=%#llx current_type=%#llx current_handle=%#x slot=%u\n",
             c->x[1],target_handle,(int32_t)c->x[2],c->x[3],c->x[30],c->x[29],c->sp,c->tpidrro,current_type,current_handle,thread_slot);
     }
 }
 if(c->pc==MOD_SDK_BASE+0x32eff0) {
     // InitializeFiber starts a new lifetime even when its guest storage is reused.
     // Retire the previous host context before the guest initializes that storage.
     Fiber **link=&fibers;
     while(*link && (*link)->guest!=c->x[0]) link=&(*link)->next;
     if(*link) {
         Fiber *retired=*link;
         if(retired==current_fiber) {
             fputs("Cannot reinitialize the running fiber\n",stderr);
             exit(4);
         }
         *link=retired->next;
         DeleteFiber(retired->host);
         free(retired);
     }
 }
 if(catherine_movie_guest_dispatch(c)) return;
 if(graphics_dispatch(c,c->pc)) return;
  if(c->pc==MOD_SDK_BASE+0x32f170) {
     uint64_t guest=c->x[0];
     if(!root_fiber) {
         root_fiber=ConvertThreadToFiber(NULL);
         if(!root_fiber){fprintf(stderr,"ConvertThreadToFiber failed %lu\n",GetLastError());exit(4);}
     }
     if(!guest) {
         if(!current_fiber){fputs("Fiber return without current fiber\n",stderr);exit(4);}
         current_fiber=NULL;
         *(uint64_t *)(uintptr_t)(*(uint64_t *)(uintptr_t)(c->tpidrro+0x1f8)+0x70)=0;
         SwitchToFiber(root_fiber);
         return;
     }
     Fiber *target=fibers;
     while(target && target->guest!=guest) target=target->next;
     if(!target) {
         target=calloc(1,sizeof *target);
         if(!target)exit(4);
         target->guest=guest;
         target->cpu=*c;
         target->cpu.pc=*(uint64_t *)(uintptr_t)(guest+0x28);
         target->cpu.x[0]=*(uint64_t *)(uintptr_t)(guest+0x30);
         target->cpu.sp=(*(uint64_t *)(uintptr_t)(guest+0x38)+
                         *(uint64_t *)(uintptr_t)(guest+0x48))&~15ull;
         target->host=CreateFiber(1024*1024,fiber_main,target);
         if(!target->host)exit(4);
         target->next=fibers;fibers=target;
         printf("Created host fiber guest=%#llx entry=%#llx stack=%#llx TLS=%#llx\n",
                guest,target->cpu.pc,target->cpu.sp,target->cpu.tpidrro);
     }
     Fiber *previous=current_fiber;
     current_fiber=target;
     *(uint64_t *)(uintptr_t)(*(uint64_t *)(uintptr_t)(c->tpidrro+0x1f8)+0x70)=guest;
     SwitchToFiber(target->host);
     current_fiber=previous;
     *(uint64_t *)(uintptr_t)(*(uint64_t *)(uintptr_t)(c->tpidrro+0x1f8)+0x70)=previous?previous->guest:0;
     return;
 }
 if(c->pc==0x7100000010ull) { puts("Loader callback reached"); return; }
 if(c->pc==0x7100000020ull) { uint64_t saved=c->x[30]; const uint64_t init[]={MOD_SDK_BASE+0x30,MOD_SUBSDK1_BASE+0x30,MOD_SUBSDK0_BASE+0x30,MOD_MAIN_BASE+0x30}; for(int i=0;i<4;i++) {c->pc=init[i];printf("Initializer %#llx\n",c->pc);rt_dispatch(c);} c->x[30]=saved;return; }
 GuestFn function;if(guest_lookup_cached(c->pc,&function)){function(c);return;}
 fprintf(stderr,"Missing dispatch %#llx from %#llx LR %#llx\n",c->pc,last_pc,c->x[30]);exit(3);
}
void guest_call_original(void *context,uint64_t address) {
    Cpu *c=context;
    c->pc=address;
    GuestFn function;if(guest_lookup_cached(address,&function)){function(c);return;}
    fprintf(stderr,"Missing original guest function %#llx\n",address);
    exit(3);
}
static void WINAPI fiber_main(void *argument) {
    Fiber *fiber=argument;
    printf("Entering fiber %#llx\n",fiber->guest);
    rt_dispatch(&fiber->cpu);
    uint64_t next=fiber->cpu.x[0];
    if(next){fiber->cpu.pc=MOD_SDK_BASE+0x32f170;rt_dispatch(&fiber->cpu);}
    current_fiber=NULL;
    *(uint64_t *)(uintptr_t)(*(uint64_t *)(uintptr_t)(fiber->cpu.tpidrro+0x1f8)+0x70)=0;
    SwitchToFiber(root_fiber);
    abort();
}
extern void graphics_read_pad(uint64_t *,int32_t *);
extern void graphics_read_player_pads(uint64_t *,int32_t *,uint32_t *);

struct TestPress { uint64_t begin, buttons, duration; };

// Optional unattended test input: milliseconds:hex-buttons:duration, repeated with commas.
static size_t read_test_presses(struct TestPress presses[32]) {
    char script[1024];
    DWORD length = GetEnvironmentVariableA("CATHERINE_TEST_PAD", script, sizeof script);
    if (!length || length >= sizeof script) return 0;
    const char *next = script;
    size_t count = 0;
    while (*next && count < 32) {
        struct TestPress press;
        char *end;
        press.begin = strtoull(next, &end, 10);
        if (end == next || *end != ':') break;
        next = end + 1;
        press.buttons = strtoull(next, &end, 16);
        if (end == next || *end != ':') break;
        next = end + 1;
        press.duration = strtoull(next, &end, 10);
        if (end == next || (*end && *end != ',')) break;
        presses[count++] = press;
        next = *end ? end + 1 : end;
    }
    if (*next) {
        fputs("Invalid CATHERINE_TEST_PAD input\n", stderr);
        return 0;
    }
    return count;
}
static DWORD WINAPI controller_main(void *arg) {
    unsigned char *npad=(unsigned char *)arg;

    uint64_t sample=1, slot=0;
    const char *test_confirm = getenv("CATHERINE_TEST_CONFIRM");
    uint64_t begin = GetTickCount64();
    struct TestPress presses[32];
    size_t press_count = read_test_presses(presses);
    for(;;) {
        uint64_t player_buttons[2]={0};
        int32_t player_sticks[8]={0};uint32_t player_connected[2]={1,0};
        graphics_read_player_pads(player_buttons,player_sticks,player_connected);
        uint64_t buttons=press_count?0:player_buttons[0];
        int32_t sticks[4]={0};if(!press_count)memcpy(sticks,player_sticks,sizeof sticks);
        uint64_t elapsed = GetTickCount64() - begin;
        if (test_confirm && elapsed >= 4500 && elapsed < 45000 && elapsed % 1000 < 350 &&
            !InterlockedCompareExchange(&logo_sequence_started, 0, 0)) {
            buttons |= 1;  // A, pressed in short pulses for the unattended boot test.
        }
        for (size_t i = 0; i < press_count; ++i) {
            struct TestPress press = presses[i];
            if (elapsed < press.begin || elapsed - press.begin >= press.duration) continue;
            buttons |= press.buttons;
            if (press.buttons & (1ull << 12)) sticks[0] = -32767;
            if (press.buttons & (1ull << 13)) sticks[1] = 32767;
            if (press.buttons & (1ull << 14)) sticks[0] = 32767;
            if (press.buttons & (1ull << 15)) sticks[1] = -32767;
        }
        player_buttons[0]=buttons;memcpy(player_sticks,sticks,sizeof sticks);
        slot=(slot+1)%17;++sample;
        for(unsigned player=0;player<2;++player){
            unsigned char *entry=npad+player*0x5000;
            catherine_publish_npad(entry,sample,slot,player_buttons[player],player_sticks+player*4,player_connected[player]);
        }
        Sleep(16);
    }
}
static void *heap, *alias;
static uint64_t heap_committed;

#ifdef CATHERINE_HIGH_REFRESH_BUILD
static SRWLOCK heap_watch_lock = SRWLOCK_INIT;
static uint64_t heap_watch_epoch = 1;
static int heap_watch_enabled;

static void* reserve_guest_heap(void) {
    char setting[2];
    if (GetEnvironmentVariableA("NX_LINEAR_HEAP_WRITE_WATCH", setting, sizeof(setting)) == 1 &&
        setting[0] == '1') {
        void* watched = VirtualAlloc((void*)(uintptr_t)CATHERINE_HEAP_WATCH_BASE, CATHERINE_HEAP_WATCH_SIZE,
                                     MEM_RESERVE | MEM_WRITE_WATCH, PAGE_NOACCESS);
        if (watched) {
            heap_watch_enabled = 1;
            fputs("[heap-write-watch] enabled private guest heap reservation\n", stderr);
            return watched;
        }
        fprintf(stderr, "[heap-write-watch] reserve failed (%lu), using ordinary heap\n", GetLastError());
    }
    return VirtualAlloc((void*)(uintptr_t)CATHERINE_HEAP_WATCH_BASE, CATHERINE_HEAP_WATCH_SIZE,
                        MEM_RESERVE, PAGE_NOACCESS);
}

int catherine_heap_watch_active(void) { return heap_watch_enabled; }

int catherine_heap_watch_begin(uint64_t address, uint64_t bytes, uint64_t* allocation_epoch) {
    if (!heap_watch_enabled) return 0;
    AcquireSRWLockShared(&heap_watch_lock);
    const uint64_t offset = address - CATHERINE_HEAP_WATCH_BASE;
    if (address < CATHERINE_HEAP_WATCH_BASE || offset > heap_committed || bytes > heap_committed - offset) {
        ReleaseSRWLockShared(&heap_watch_lock);
        return 0;
    }
    *allocation_epoch = heap_watch_epoch;
    return 1;
}

void catherine_heap_watch_end(void) { ReleaseSRWLockShared(&heap_watch_lock); }
#endif

static struct {uint64_t address,size;uint32_t permission;} transfers[32];
static LONG transfer_count;
typedef struct { Cpu cpu; CatherineThreadLifetime lifetime; uint64_t id; uint32_t priority; CatherineThreadAffinity affinity; } Thread;
static Thread threads[64];
static LONG thread_count;
static LONG64 thread_serial=199;
static HANDLE main_thread;
static CatherineThreadAffinity main_thread_affinity={0,7};
static SRWLOCK thread_affinity_lock=SRWLOCK_INIT;
static volatile LONG cancelled_waits[ARRAYSIZE(threads) + 1];

static VOID CALLBACK wake_guest_wait(ULONG_PTR unused) {
    (void)unused;  // The APC wakes an alertable wait; the pending bit carries the cancellation.
}

static uint32_t cancel_guest_wait(uint32_t handle) {
    unsigned slot;
    HANDLE target;
    AcquireSRWLockShared(&thread_affinity_lock);
    if (handle == 0xffff8000u) {
        slot = thread_slot;
        target = GetCurrentThread();
    } else if (handle == 1) {
        slot = 0;
        target = main_thread;
    } else if (handle >= 200 && handle - 200 < ARRAYSIZE(threads)) {
        slot = handle - 200 + 1;
        target = catherine_thread_lifetime_handle(&threads[handle - 200].lifetime);
    } else target=NULL;
    uint32_t result=0xe401;
    if(target){
        if (!InterlockedExchange(&cancelled_waits[slot], 1))QueueUserAPC(wake_guest_wait, target, 0);
        result=0;
    }
    ReleaseSRWLockShared(&thread_affinity_lock);
    return result;
}

static void wait_guest_objects(Cpu *c) {
    uint32_t count = (uint32_t)c->x[2];
    if (count > MAXIMUM_WAIT_OBJECTS) {
        c->x[0] = 0xdc01; // ResultOutOfRange (module 1, description 110).
        return;
    }
    HANDLE handles[MAXIMUM_WAIT_OBJECTS],originals[MAXIMUM_WAIT_OBJECTS];
    unsigned indices[MAXIMUM_WAIT_OBJECTS];
    const uint32_t *guest = (const uint32_t *)(uintptr_t)c->x[1];
    unsigned unique = 0;
    AcquireSRWLockShared(&thread_affinity_lock);
    for (unsigned i = 0; i < count; ++i) {
        HANDLE handle = ipc_event(guest[i]);
        if (!handle && guest[i] >= 200 && guest[i] - 200 < ARRAYSIZE(threads))
            handle = catherine_thread_lifetime_handle(&threads[guest[i] - 200].lifetime);
        if (!handle) {
            ReleaseSRWLockShared(&thread_affinity_lock);c->x[0]=0xe401;goto wait_done;
        }
        unsigned j = 0;
        while (j < unique && originals[j] != handle) ++j;
        if (j == unique) {
            // Keep an independent kernel reference across concurrent guest Close
            // and slot reuse; do not hold the registry lock while waiting.
            if(!DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&handles[unique],0,FALSE,DUPLICATE_SAME_ACCESS)){
                ReleaseSRWLockShared(&thread_affinity_lock);c->x[0]=0xe401;goto wait_done;
            }
            originals[unique]=handle;indices[unique++]=i;
        }
    }

    ReleaseSRWLockShared(&thread_affinity_lock);

    // A ready handle and a zero timeout take precedence over a pending cancellation.
    int64_t ns = (int64_t)c->x[3];
    DWORD result = unique ? WaitForMultipleObjects(unique, handles, FALSE, 0) : WAIT_TIMEOUT;
    if (result == WAIT_TIMEOUT && ns != 0) {
        uint64_t deadline = ns > 0 ? GetTickCount64() + ((uint64_t)ns + 999999) / 1000000 : 0;
        for (;;) {
            if (InterlockedExchange(&cancelled_waits[thread_slot], 0)) {
                c->x[0] = 0xec01;
                goto wait_done;
            }
            uint64_t now = GetTickCount64();
            if (deadline && now >= deadline) break;
            uint64_t remaining = deadline ? deadline - now : INFINITE;
            DWORD timeout = deadline && remaining >= INFINITE ? INFINITE - 1 : (DWORD)remaining;
            if (unique) result = WaitForMultipleObjectsEx(unique, handles, FALSE, timeout, TRUE);
            else result = SleepEx(timeout, TRUE) == WAIT_IO_COMPLETION ? WAIT_IO_COMPLETION : WAIT_TIMEOUT;
            if (result == WAIT_IO_COMPLETION) { result = WAIT_TIMEOUT; continue; }
            if (result == WAIT_TIMEOUT && deadline && remaining >= INFINITE) continue;
            break;
        }
    }
    if (result < unique) { c->x[0] = 0; c->x[1] = indices[result]; }
    else c->x[0] = result == WAIT_TIMEOUT ? 0xea01 : 0xe401;
wait_done:
    for(unsigned i=0;i<unique;++i)CloseHandle(handles[i]);
}
static DWORD WINAPI thread_main(void *argument) {
    Thread *thread=argument;
    thread_slot=1+(unsigned)(thread-threads);
    rt_dispatch(&thread->cpu);
    return 0;
}
static DWORD WINAPI watchdog(void *argument) {
    (void)argument;
    for (;;) {
        Sleep(10000);
        fprintf(stderr,"Guest thread positions:");
        for(unsigned i=0;i<=(unsigned)thread_count;i++) fprintf(stderr," %u=%#llx/%#llx/x0:%#llx",i,thread_pc[i],thread_lr[i],thread_x0[i]);
        fputc('\n',stderr);
    }
}
void rt_svc(Cpu *c,uint32_t imm,uint64_t pc) {
 if (ipc_svc(c, imm)) return;
 if(imm==0x13 && c->x[0]==301 && c->x[2]==0x40000 && c->x[3]==1 && !(c->x[1]&4095)) {
     uint64_t address=c->x[1],base=address&~0xffffull;
     size_t size=(size_t)((address-base+0x4ffff)&~0xffffull);
     MEMORY_BASIC_INFORMATION info;
     if(!VirtualQuery((void *)(uintptr_t)address,&info,sizeof info))goto unhandled;
     if(info.State==MEM_FREE && !VirtualAlloc((void *)(uintptr_t)base,size,MEM_RESERVE,PAGE_NOACCESS))goto unhandled;
     if(info.State!=MEM_FREE&&info.State!=MEM_RESERVE)goto unhandled;
     if(!VirtualAlloc((void *)(uintptr_t)address,0x40000,MEM_COMMIT,PAGE_READWRITE))goto unhandled;
     unsigned char *npad=(unsigned char *)(uintptr_t)(address+0x9a00);
     for(unsigned player=0;player<2;++player){
         unsigned char *entry=npad+player*0x5000;
         *(uint32_t *)entry=player?0:1;
         uint64_t *lifo=(uint64_t *)(entry+0x28);
         lifo[1]=17;lifo[2]=0;lifo[3]=1;lifo[4]=1;lifo[5]=1;
         *(uint32_t *)(entry+0x28+0x20+8+32)=player?0:1;
     }
     CreateThread(NULL,0,controller_main,npad,0,NULL);
     c->x[0]=0;puts("Mapped player 1 and player 2 controller state");return;
 }
 if(imm==6 && c->x[2]>=0x7000000000ull) {
     uint64_t address=c->x[2];
     const uint64_t bases[]={MOD_MAIN_BASE,MOD_SUBSDK0_BASE,MOD_SUBSDK1_BASE,MOD_SDK_BASE};
     const uint64_t sizes[]={MOD_MAIN_SIZE,MOD_SUBSDK0_SIZE,MOD_SUBSDK1_SIZE,MOD_SDK_SIZE};
     uint64_t low=0x7000000000ull,high=1ull<<39;
     struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} m={0};
     if(address>=high){low=high;high=0;m.state=0x10;}
     else for(unsigned i=0;i<4;i++){
         uint64_t end=bases[i]+sizes[i];
         if(address>=bases[i]&&address<end){low=bases[i];high=end;m.state=4;m.perm=3;break;}
         if(end<=address&&end>low)low=end;
         if(bases[i]>address&&bases[i]<high)high=bases[i];
     }
     m.base=low;m.size=high-low;memcpy((void *)(uintptr_t)c->x[0],&m,sizeof m);
     c->x[0]=c->x[1]=0;return;
 }
 if (imm==0x16 && c->x[0]>=200 && c->x[0]<200+ARRAYSIZE(threads)) {
     AcquireSRWLockExclusive(&thread_affinity_lock);
     CatherineThreadLifetime *slot=&threads[c->x[0]-200].lifetime;
     c->x[0]=catherine_thread_lifetime_handle(slot)?0:0xe401;
     if(!c->x[0])slot->closed=1;
     ReleaseSRWLockExclusive(&thread_affinity_lock);
     return;
 }
 if (imm==0x16 && c->x[0]>=400 && c->x[0]<400+(uint32_t)transfer_count) {
     c->x[0]=0;
     return;
 }
 if (imm==0x15) {
     uint64_t address=c->x[1], size=c->x[2];
     if((address&4095)||!size||(size&4095)||address+size<address){c->x[0]=0xca01;return;}
     LONG index=InterlockedIncrement(&transfer_count)-1;
     if(index>=32){c->x[0]=0xce01;return;}
     transfers[index].address=address;transfers[index].size=size;transfers[index].permission=(uint32_t)c->x[3];
     c->x[0]=0;c->x[1]=400+index;return;
 }
 if (sync_svc(c,imm,c->thread?200+(uint32_t)((Thread *)c->thread-threads):1)) return;
 if(imm==6 && c->x[2]>=0x8000000ull && c->x[2]<0x3000000000ull) {
     uint64_t address=c->x[2], low=0x8000000ull, high=0x3000000000ull;
     const struct {uint64_t base,size;uint32_t state;} ranges[]={
         {(uintptr_t)heap,heap_committed,5},{(uintptr_t)alias,64ull<<30,0},
     };
     struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} m={0};
     for(unsigned i=0;i<2;i++){
         uint64_t base=ranges[i].base,end=base+ranges[i].size;
         if(!ranges[i].size)continue;
         if(address>=base && address<end){
             MEMORY_BASIC_INFORMATION info;
             if(!VirtualQuery((void *)(uintptr_t)address,&info,sizeof info))goto unhandled;
             low=(uintptr_t)info.BaseAddress;high=low+info.RegionSize;
             m.state=info.State==MEM_COMMIT?5:0;m.perm=m.state?3:0;break;
         }
         if(end<=address && end>low)low=end;
         if(base>address && base<high)high=base;
     }
     m.base=low;m.size=high-low;
     memcpy((void *)(uintptr_t)c->x[0],&m,sizeof m);c->x[0]=c->x[1]=0;return;
 }
 if(imm==0x13 && c->x[0]==300 && c->x[2]==4096 && c->x[3]==1 && !(c->x[1]&4095)) {
     uint64_t address=c->x[1],base=address&~0xffffull;
     MEMORY_BASIC_INFORMATION region;
     if(!VirtualQuery((void *)(uintptr_t)address,&region,sizeof region))goto unhandled;
     if(region.State==MEM_FREE &&
        !VirtualAlloc((void *)(uintptr_t)base,65536,MEM_RESERVE,PAGE_NOACCESS))goto unhandled;
     if(region.State!=MEM_FREE && region.State!=MEM_RESERVE)goto unhandled;
     unsigned char *memory=VirtualAlloc((void *)(uintptr_t)address,4096,MEM_COMMIT,PAGE_READWRITE);
     if(!memory)goto unhandled;
     uint64_t context[4]={(uint64_t)time(NULL)-rt_ticks()/19200000,0,0,0};
     unsigned offsets[]={0x38,0x80};
     for(unsigned i=0;i<2;i++){memcpy(memory+offsets[i]+8,context,sizeof context);memcpy(memory+offsets[i]+40,context,sizeof context);}
     DWORD old;
     if(!VirtualProtect(memory,4096,PAGE_READONLY,&old))goto unhandled;
     c->x[0]=0;return;
 }
 if(imm==6 && c->x[2]>=0x3040000000ull && c->x[2]<0x7000000000ull) {
     uint64_t address=c->x[2];
     uint64_t base=(uintptr_t)alias, end=base+(64ull<<30);
     struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} m={0};
     if(address>=base && address<end) {
         MEMORY_BASIC_INFORMATION info;
         if(!VirtualQuery((void *)(uintptr_t)address,&info,sizeof info))goto unhandled;
         m.base=(uintptr_t)info.BaseAddress;m.size=info.RegionSize;
         if(info.State==MEM_COMMIT){m.state=5;m.perm=3;}
     } else if(address<base) {m.base=0x3100010000ull+64*0x10000ull;m.size=base-m.base;}
     else {m.base=end;m.size=0x7000000000ull-end;}
     memcpy((void *)(uintptr_t)c->x[0],&m,sizeof m);c->x[0]=c->x[1]=0;return;
 }
 if(imm==1) {
     uint64_t size=c->x[1];
     if(size>(4ull<<30)||(size&0x1fffff)){c->x[0]=0xca01;return;}
#ifdef CATHERINE_HIGH_REFRESH_BUILD
     if (heap_watch_enabled) AcquireSRWLockExclusive(&heap_watch_lock);
#endif
     const int resized = size == heap_committed ||
         (size > heap_committed
          ? VirtualAlloc((char*)heap + heap_committed, size - heap_committed, MEM_COMMIT, PAGE_READWRITE) != NULL
          : VirtualFree((char*)heap + size, heap_committed - size, MEM_DECOMMIT) != 0);
     if (resized) {
#ifdef CATHERINE_HIGH_REFRESH_BUILD
         if (heap_watch_enabled && size != heap_committed) ++heap_watch_epoch;
#endif
         heap_committed = size;
     }
#ifdef CATHERINE_HIGH_REFRESH_BUILD
     if (heap_watch_enabled) ReleaseSRWLockExclusive(&heap_watch_lock);
#endif
     if (!resized) goto unhandled;
     c->x[0]=0;c->x[1]=(uintptr_t)heap;return;
 }
 if (imm==8) {
     AcquireSRWLockExclusive(&thread_affinity_lock);
     CatherineThreadLifetime *slots[ARRAYSIZE(threads)];
     for(unsigned i=0;i<ARRAYSIZE(threads);++i)slots[i]=&threads[i].lifetime;
     int index=catherine_thread_lifetime_acquire(slots,ARRAYSIZE(threads));
     if(index<0){ReleaseSRWLockExclusive(&thread_affinity_lock);c->x[0]=0xce01;return;}
     Thread *t=&threads[index];
     t->cpu=(Cpu){0};
     void *tls=VirtualAlloc((void *)(uintptr_t)(0x3100010000ull+(unsigned)index*0x10000ull),4096,
                            MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
     t->lifetime.tls=tls;
     if(!tls){
         catherine_thread_lifetime_discard(&t->lifetime);
         ReleaseSRWLockExclusive(&thread_affinity_lock);c->x[0]=0xce01;return;
     }
     *(uint64_t *)((char *)tls+0x1f8)=(uintptr_t)tls+0x200;
     t->cpu.pc=c->x[1];t->cpu.x[0]=c->x[2];t->cpu.sp=c->x[3];
     t->cpu.tpidr=t->cpu.tpidrro=(uintptr_t)tls;t->cpu.thread=t;
     t->priority=(uint32_t)c->x[4];
     const int32_t requested_core=(int32_t)c->x[5];
     t->affinity.core=requested_core<0?0:requested_core;
     t->affinity.mask=requested_core>=0&&requested_core<3?1ull<<requested_core:7;
     HANDLE host=CreateThread(NULL,16u<<20,thread_main,t,CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION,NULL);
     if(!host){
         catherine_thread_lifetime_discard(&t->lifetime);t->cpu=(Cpu){0};
         ReleaseSRWLockExclusive(&thread_affinity_lock);c->x[0]=0xce01;return;
     }
     catherine_thread_lifetime_publish(&t->lifetime,host);
     t->id=(uint64_t)InterlockedIncrement64(&thread_serial);
     InterlockedExchange(&cancelled_waits[index+1],0);
     thread_pc[index+1]=thread_lr[index+1]=thread_x0[index+1]=0;
     if(thread_count<=index)InterlockedExchange(&thread_count,index+1);
     ReleaseSRWLockExclusive(&thread_affinity_lock);
     c->x[0]=0;c->x[1]=200+(unsigned)index;
     printf("Created guest thread %u\n",200+(unsigned)index);return;
 }
 if(imm==9 && c->x[0]>=200 && c->x[0]<200+ARRAYSIZE(threads)) {
     uint32_t id=(uint32_t)c->x[0];
     AcquireSRWLockShared(&thread_affinity_lock);
     HANDLE host=catherine_thread_lifetime_handle(&threads[id-200].lifetime);
     c->x[0]=host&&ResumeThread(host)!=(DWORD)-1?0:0xe401;
     ReleaseSRWLockShared(&thread_affinity_lock);
     printf("Started guest thread %u\n",id);return;
 }
 if(imm==0xa && c->thread) ExitThread(0);
 if(imm==0xb) {
     const int64_t ns=(int64_t)c->x[0];
#ifdef CATHERINE_HIGH_REFRESH_BUILD
     if (catherine_guest_precise_sleep(ns)) return;
#endif
     Sleep(ns>0?(DWORD)((ns+999999)/1000000):0);
     return;
 }
 if((imm==0xc||imm==0x25) && c->x[1]>=200 && c->x[1]<200+ARRAYSIZE(threads)) {
     uint32_t id=(uint32_t)c->x[1];
     AcquireSRWLockShared(&thread_affinity_lock);
     Thread *t=&threads[id-200];
     if(catherine_thread_lifetime_handle(&t->lifetime)){c->x[1]=imm==0xc?t->priority:t->id;c->x[0]=0;}
     else c->x[0]=0xe401;
     ReleaseSRWLockShared(&thread_affinity_lock);return;
 }
 if(imm==0xd && c->x[0]>=200 && c->x[0]<200+ARRAYSIZE(threads)) {
     AcquireSRWLockExclusive(&thread_affinity_lock);
     Thread *t=&threads[c->x[0]-200];
     if(catherine_thread_lifetime_handle(&t->lifetime)){t->priority=(uint32_t)c->x[1];c->x[0]=0;}
     else c->x[0]=0xe401;
     ReleaseSRWLockExclusive(&thread_affinity_lock);return;
 }
 if(imm==0x0e||imm==0x0f){
     CatherineThreadAffinity *entries[ARRAYSIZE(threads)];
     AcquireSRWLockExclusive(&thread_affinity_lock);
     unsigned count=(unsigned)thread_count;if(count>ARRAYSIZE(threads))count=ARRAYSIZE(threads);
     for(unsigned i=0;i<count;++i)entries[i]=catherine_thread_lifetime_handle(&threads[i].lifetime)?&threads[i].affinity:NULL;
     catherine_thread_affinity_svc(c,imm,&main_thread_affinity,
         c->thread?&((Thread *)c->thread)->affinity:&main_thread_affinity,entries,count);
     ReleaseSRWLockExclusive(&thread_affinity_lock);
     return;
 }
 if(imm==0x10){
     AcquireSRWLockShared(&thread_affinity_lock);
     c->x[0]=(uint32_t)(c->thread?((Thread *)c->thread)->affinity.core:main_thread_affinity.core);
     ReleaseSRWLockShared(&thread_affinity_lock);return;
 }
 if (imm == 0x18) { wait_guest_objects(c); return; }
 if (imm == 0x19) { c->x[0] = cancel_guest_wait((uint32_t)c->x[0]); return; }
 if (imm==5) {
     uint64_t dst=c->x[0],src=c->x[1],size=c->x[2]; DWORD old;
     if(!size || ((dst|src|size)&4095) || dst<0x3000100000ull || dst+size>0x3040000000ull) { c->x[0]=0xd401; return; }
     if(!VirtualProtect((void *)(uintptr_t)src,size,PAGE_READWRITE,&old))goto unhandled;
     memcpy((void *)(uintptr_t)src,(void *)(uintptr_t)dst,size);
     if(!VirtualFree((void *)(uintptr_t)dst,size,MEM_DECOMMIT))goto unhandled;
     c->x[0]=0;return;
 }
 if (imm==4) {
     uint64_t dst=c->x[0],src=c->x[1],size=c->x[2];
     MEMORY_BASIC_INFORMATION target,source;
     if (!size || (dst|src|size)&4095 || dst<0x3000100000ull ||
         dst>=0x3040000000ull || size>0x3040000000ull-dst ||
         !VirtualQuery((void *)(uintptr_t)dst,&target,sizeof target) ||
         target.State!=MEM_RESERVE || target.RegionSize<size ||
         !VirtualQuery((void *)(uintptr_t)src,&source,sizeof source) ||
         source.State!=MEM_COMMIT || source.Protect!=PAGE_READWRITE ||
         (uintptr_t)source.BaseAddress+source.RegionSize-src<size) {c->x[0]=0xd401;return;}
     void *p=VirtualAlloc((void *)(uintptr_t)dst,size,MEM_COMMIT,PAGE_READWRITE);
     DWORD old;
     if(!p)goto unhandled;
     memcpy(p,(void *)(uintptr_t)src,size);
     if(!VirtualProtect((void *)(uintptr_t)src,size,PAGE_NOACCESS,&old)) goto unhandled;
     c->x[0]=0;
     return;
 }
 if (imm==6 && c->x[2]>=0x3000100000ull && c->x[2]<0x3040000000ull) {
     MEMORY_BASIC_INFORMATION info;
     if(!VirtualQuery((void *)(uintptr_t)c->x[2],&info,sizeof info)) goto unhandled;
     struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} m={
         (uintptr_t)info.BaseAddress,info.RegionSize,info.State==MEM_COMMIT?11:0,0,
         info.State==MEM_COMMIT?3:0,0,0,0};
     memcpy((void *)(uintptr_t)c->x[0],&m,sizeof m);c->x[0]=c->x[1]=0;return;
 }
 if (imm==0x2c || imm==0x2d) {
     uint64_t address=c->x[0], size=c->x[1], base=(uintptr_t)alias;
     if (!size || (size & 4095)) {c->x[0]=0xca01;return;}
     if ((address & 4095) || address<base || size>(64ull<<30) ||
         address-base>(64ull<<30)-size) {c->x[0]=0xcc01;return;}
     BOOL ok=imm==0x2c ? VirtualAlloc((void *)(uintptr_t)address,size,MEM_COMMIT,PAGE_READWRITE)!=NULL :
                         VirtualFree((void *)(uintptr_t)address,size,MEM_DECOMMIT);
     if (!ok) {fprintf(stderr,"Alias mapping failed %lu\n",GetLastError());exit(4);}
     c->x[0]=0;
     return;
 }
 if (imm==0x1d) {
     volatile LONG *condition=(volatile LONG *)(uintptr_t)c->x[0];
     if (InterlockedCompareExchange(condition,0,0)!=0) goto unhandled;
     c->x[0]=0;
     return;
 }
 if (imm==6 && c->x[2]>=0x3000000000ull && c->x[2]<0x3000100000ull) {
 struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} m={0x3000000000ull,0x100000,11,0,3,0,0,0};
 memcpy((void *)(uintptr_t)c->x[0],&m,sizeof m);c->x[0]=c->x[1]=0;return; }
 if (imm==6 && c->x[2]<0x8000000) { struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} m={0,0x8000000,0,0,0,0,0,0};memcpy((void *)(uintptr_t)c->x[0],&m,sizeof m);c->x[0]=c->x[1]=0;return; }
 if (imm==0x0c && c->x[1]==1) {c->x[0]=0;c->x[1]=44;return;}
 if (imm==0x25 && c->x[1]==1) {c->x[0]=0;c->x[1]=1;return;}
 if (imm==0x29) {uint64_t v=0; switch((uint32_t)c->x[1]) {
 case 0:v=7;break;case 1:v=0x0ffffffff0000000ull;break;
 case 2:v=(uintptr_t)alias;break;case 3:v=64ull<<30;break;
 case 4:v=(uintptr_t)heap;break;case 5:v=4ull<<30;break;
 case 6:case 21:v=4ull<<30;break;case 7:case 22:v=0x8000000;break;
 case 8:v=0;break;case 11:if(BCryptGenRandom(NULL,(PUCHAR)&v,8,BCRYPT_USE_SYSTEM_PREFERRED_RNG))exit(4);break;
 case 12:v=0x8000000;break;case 13:v=(1ull<<39)-0x8000000;break;
 case 14:v=0x3000000000ull;break;case 15:v=1ull<<30;break;
 case 16:v=32ull<<20;break;case 17:v=0;break;case 18:v=0x0100bf00112c0000ull;break;
 default:goto unhandled; } c->x[0]=0;c->x[1]=v;return; }
unhandled: if(imm==0x26) { uint64_t fp=c->x[29]; for(int i=0;i<16 && fp>=0x1000000000ull && fp<0x7000000000ull;i++){ uint64_t *f=(uint64_t *)(uintptr_t)fp; fprintf(stderr,"frame %d fp=%#llx lr=%#llx\n",i,fp,f[1]); if(f[0]<=fp || f[0]-fp>0x100000)break;fp=f[0]; } } if(imm==0x26) fprintf(stderr,"Break detail value=%#x lr=%#llx sp=%#llx\n",c->x[1] && c->x[2]>=4 ? *(uint32_t *)(uintptr_t)c->x[1] : 0,c->x[30],c->sp); fprintf(stderr,"Unhandled SVC %#x at %#llx x0=%#llx x1=%#llx x2=%#llx x3=%#llx\n",imm,pc,c->x[0],c->x[1],c->x[2],c->x[3]);exit(4);}
void rt_udf(Cpu *c,uint64_t pc) {(void)c;fprintf(stderr,"Guest trap %#llx\n",pc);exit(5);}
uint64_t rt_ticks(void) { LARGE_INTEGER x,f;QueryPerformanceCounter(&x);QueryPerformanceFrequency(&f);return (x.QuadPart/f.QuadPart)*19200000ull+(x.QuadPart%f.QuadPart)*19200000ull/f.QuadPart;}
static LONG WINAPI fault(EXCEPTION_POINTERS *e) {fprintf(stderr,"Fault %#lx at guest %#llx, address %#llx lr %#llx x0 %#llx slot %u\n",e->ExceptionRecord->ExceptionCode,last_pc,(uint64_t)e->ExceptionRecord->ExceptionInformation[1],thread_lr[thread_slot],thread_x0[thread_slot],thread_slot);return EXCEPTION_EXECUTE_HANDLER;}
static void load(const char *name,uint64_t base,size_t size) {
    void *p=VirtualAlloc((void *)(uintptr_t)base,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    int packed=asset_pak_initialize(),ok=0;
    if(p && packed==1) ok=asset_pak_read(name+7,p,size); // Existing names begin "assets/".
    else if(p && packed==0) {
        FILE *f=NULL;
        if(!fopen_s(&f,name,"rb") && f) {ok=fread(p,1,size,f)==size;fclose(f);}
    }
    if(!ok){fprintf(stderr,"Could not load %s%s%s\n",name,packed?": ":"",packed?asset_pak_error():"");exit(2);}
}
int main(void) {
    HANDLE process = GetCurrentProcess();
    if (!DuplicateHandle(process, GetCurrentThread(), process, &main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        fprintf(stderr, "Could not initialize the main guest thread (%lu)\n", GetLastError());
        return 2;
    }
    trace_boot=getenv("CATHERINE_TRACE_BOOT")!=NULL || getenv("CATHERINE_TEST_CONFIRM")!=NULL;g_fnprof=trace_boot;const char *lookup_cache_mode=getenv("CATHERINE_GUEST_LOOKUP_CACHE");guest_lookup_cache_enabled=!(lookup_cache_mode && strcmp(lookup_cache_mode,"0")==0);setvbuf(stdout,NULL,_IONBF,0);SetUnhandledExceptionFilter(fault);load("assets/main.image",MOD_MAIN_BASE,MOD_MAIN_SIZE);load("assets/subsdk0.image",MOD_SUBSDK0_BASE,MOD_SUBSDK0_SIZE);load("assets/subsdk1.image",MOD_SUBSDK1_BASE,MOD_SUBSDK1_SIZE);load("assets/sdk.image",MOD_SDK_BASE,MOD_SDK_SIZE);void *stack=VirtualAlloc((void *)0x3000000000ull,1ull<<30,MEM_RESERVE,PAGE_NOACCESS);if(stack && !VirtualAlloc(stack,0x100000,MEM_COMMIT,PAGE_READWRITE))return 2;void *tls=VirtualAlloc((void *)0x3100000000ull,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!stack||!tls)return 2;*(uint64_t *)((char *)tls+0x1f8)=(uintptr_t)tls+0x200;
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    heap=reserve_guest_heap();
#else
    heap=VirtualAlloc((void *)0x1000000000ull,4ull<<30,MEM_RESERVE,PAGE_NOACCESS);
#endif
    alias=VirtualAlloc((void *)0x2000000000ull,64ull<<30,MEM_RESERVE,PAGE_NOACCESS);if(!alias)alias=VirtualAlloc((void *)0x4000000000ull,64ull<<30,MEM_RESERVE,PAGE_NOACCESS);if(!alias)alias=VirtualAlloc((void *)0x5000000000ull,64ull<<30,MEM_RESERVE,PAGE_NOACCESS);if(!heap||!alias){fputs("Guest reservation failed\\n",stderr);return 2;}Cpu c={0};c.sp=(uintptr_t)stack+0x100000;c.tpidr=c.tpidrro=(uintptr_t)tls;c.x[0]=1;c.x[2]=0x7100000010ull;c.x[3]=0x7100000020ull;c.pc=MOD_SDK_BASE+0x29d9e0;CreateThread(NULL,0,watchdog,NULL,0,NULL);puts("Entering Catherine SDK startup");rt_dispatch(&c);return 0;}














