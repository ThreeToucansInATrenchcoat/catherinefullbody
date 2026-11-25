#include <nx/runtime.h>
#include <map>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <mutex>
#include <new>
#include <process.h>
#include <windows.h>
#include "platform.h"
#include "periodic_render_log.h"
namespace nx {
static std::map<std::string,HleFn>& registry() {static std::map<std::string,HleFn> m;return m;}
HleRegistrar::HleRegistrar(const char *name,HleFn fn){registry()[name]=fn;}
HleFn hle_lookup(const char *name){auto it=registry().find(name);return it==registry().end()?nullptr:it->second;}
static std::map<std::string,uint64_t> addresses;
static std::map<uint64_t,std::pair<std::string,HleFn>> targets;
static thread_local std::string current_name;
uint64_t hle_address_or(const char *name,HleFn fallback){auto found=addresses.find(name);if(found!=addresses.end())return found->second;uint64_t address=0x7200000000ull+addresses.size()*16;auto fn=hle_lookup(name);targets[address]={name,fn?fn:fallback};addresses[name]=address;return address;}
static void missing(CPU&){std::fprintf(stderr,"Unimplemented graphics call %s\n",current_name.c_str());std::exit(4);}
uint64_t hle_address(const char *name){return hle_address_or(name,missing);}
const char *hle_current_name(){return current_name.c_str();}
struct PeriodicRenderLogBatch {
    char text[8192];
    size_t used;
    bool overflow;
};

struct PeriodicRenderLogQueue {
    static constexpr uint64_t capacity = 32;
    PeriodicRenderLogBatch batches[capacity];
    // GPU and guest recording/submission producers claim a batch without waiting.
    std::atomic<PeriodicRenderLogScope*> writer{nullptr};
    // Producer ownership serializes head. The independent sink only owns tail.
    alignas(64) std::atomic<uint64_t> head{0};
    alignas(64) std::atomic<uint64_t> tail{0};
    std::atomic<unsigned> dropped{0};
    HANDLE wake = nullptr;
};

namespace {
std::atomic<PeriodicRenderLogQueue*> g_periodic_log{nullptr};
thread_local PeriodicRenderLogScope* g_periodic_scope = nullptr;

unsigned __stdcall write_periodic_render_log(void* opaque) {
    auto& queue = *static_cast<PeriodicRenderLogQueue*>(opaque);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    uint64_t tail = 0;
    for (;;) {
        while (tail != queue.head.load(std::memory_order_acquire)) {
            const auto& batch = queue.batches[tail % queue.capacity];
            std::fwrite(batch.text, 1, batch.used, stderr);
            queue.tail.store(++tail, std::memory_order_release);
        }
        if (unsigned dropped = queue.dropped.exchange(0, std::memory_order_relaxed))
            std::fprintf(stderr, "nvn: renderer statistics dropped %u complete batches\n", dropped);
        if (WaitForSingleObject(queue.wake, INFINITE) != WAIT_OBJECT_0) return 1;
    }
}
}  // namespace

void start_periodic_render_log() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char* sync = std::getenv("NX_PERIODIC_RENDER_LOG_SYNC");
        if (sync && sync[0] == '1') return;
        auto* queue = new (std::nothrow) PeriodicRenderLogQueue;
        if (!queue) return;
        queue->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!queue->wake) {
            delete queue;
            return;
        }
        const uintptr_t worker = _beginthreadex(nullptr, 0, write_periodic_render_log, queue, 0, nullptr);
        if (!worker) {
            CloseHandle(queue->wake);
            delete queue;
            return;
        }
        CloseHandle(reinterpret_cast<HANDLE>(worker));
        // Windows closes the process-lifetime event and worker handle on exit.
        g_periodic_log.store(queue, std::memory_order_release);
    });
}

PeriodicRenderLogScope::PeriodicRenderLogScope() {
    if (g_periodic_scope) return;
    queue_ = g_periodic_log.load(std::memory_order_acquire);
    if (!queue_) return;
    g_periodic_scope = this;
    PeriodicRenderLogScope* vacant = nullptr;
    if (!queue_->writer.compare_exchange_strong(vacant, this, std::memory_order_acquire)) return;
    const uint64_t head = queue_->head.load(std::memory_order_relaxed);
    if (head - queue_->tail.load(std::memory_order_acquire) >= queue_->capacity) return;
    batch_ = &queue_->batches[head % queue_->capacity];
    batch_->used = 0;
    batch_->overflow = false;
}

void PeriodicRenderLogScope::append(const char* format, va_list args) {
    if (!batch_ || batch_->overflow) return;
    const size_t available = sizeof(batch_->text) - batch_->used;
    const int length = std::vsnprintf(batch_->text + batch_->used, available, format, args);
    if (length < 0 || size_t(length) >= available) {
        batch_->overflow = true;
        return;
    }
    batch_->used += size_t(length);
    batch_->text[batch_->used++] = '\n';
}

PeriodicRenderLogScope::~PeriodicRenderLogScope() {
    if (!queue_) return;
    g_periodic_scope = nullptr;
    if (!batch_ || batch_->overflow) {
        queue_->dropped.fetch_add(1, std::memory_order_relaxed);
    } else if (batch_->used) {
        const uint64_t head = queue_->head.load(std::memory_order_relaxed);
        queue_->head.store(head + 1, std::memory_order_release);
    }
    const bool wake = !batch_ || batch_->overflow || batch_->used;
    if (queue_->writer.load(std::memory_order_relaxed) == this)
        queue_->writer.store(nullptr, std::memory_order_release);
    if (wake) SetEvent(queue_->wake);
}

void periodic_log(const char* format, ...) {
    PeriodicRenderLogScope scope;
    va_list args;
    va_start(args, format);
    if (g_periodic_scope) g_periodic_scope->append(format, args);
    va_end(args);
}

void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (g_periodic_scope) {
        g_periodic_scope->append(format, args);
    } else {
        std::vfprintf(stderr, format, args);
        std::fputc('\n', stderr);
    }
    va_end(args);
}
[[noreturn]] void fatal(const char *format,...){va_list args;va_start(args,format);std::vfprintf(stderr,format,args);va_end(args);std::fputc('\n',stderr);std::exit(4);}
std::string describe(uint64_t address){char text[32];std::snprintf(text,sizeof text,"%#llx",(unsigned long long)address);return text;}
std::atomic<uint64_t> g_frame{0};
static void vi_ok(CPU& c){c.x[0]=0;}
static void vi_open(CPU& c){*reinterpret_cast<uint64_t *>(c.x[0])=1;c.x[0]=0;}
static void vi_layer(CPU& c){void *w=plat::create_window();if(!w)fatal("Unable to create graphics window");*reinterpret_cast<uint64_t *>(c.x[0])=1;c.x[0]=0;}
static void vi_native(CPU& c){void *w=plat::create_window();if(!w)fatal("Unable to create graphics window");*reinterpret_cast<uint64_t *>(c.x[0])=(uint64_t)w;c.x[0]=0;}
static void nv_noop(CPU&){}
NX_HLE("_ZN2nn2vi10InitializeEv",vi_ok);
NX_HLE("_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE",vi_open);
NX_HLE("_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE",vi_layer);
NX_HLE("_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE",vi_native);
NX_HLE("_ZN2nn2vi19SetLayerScalingModeEPNS0_5LayerENS0_11ScalingModeE",vi_ok);
NX_HLE("_ZN2nv18InitializeGraphicsEPvm",nv_noop);
NX_HLE("_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_",nv_noop);
NX_HLE("_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_",nv_noop);
}
extern "C" int graphics_has(uint64_t address){return nx::targets.find(address)!=nx::targets.end();}
namespace nx {
bool g_trace=false;
const LoadedModule* module_at(uint64_t){return nullptr;}
uint64_t find_symbol(const char* name){return std::strcmp(name,"nvnBootstrapLoader")==0?0x7005454d90ull:0;}
uint64_t kernel_alloc(uint64_t size,uint64_t){void *p=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!p)fatal("Graphics memory allocation failed");return (uint64_t)p;}
}
namespace nx::plat {
void input_script(PadState&){} size_t touch_overlay(OverlayShape*,size_t){return 0;}
void perf_hint_thread(){} void perf_hint_frame(int64_t){} void frame_thread_tick(int){}
void thread_counters(uint64_t& n,long f[2],long s[2]){n=0;f[0]=f[1]=s[0]=s[1]=0;}
uint64_t memguard_overlap_ns(uint64_t,uint64_t){return 0;}
bool write_watch_add(uint64_t,uint64_t){return false;} bool write_watch_take(uint64_t){return true;} void write_watch_remove(uint64_t){}
}
extern "C" void graphics_run(void*,nx::HleFn);
extern "C" int graphics_dispatch(void *cpu,uint64_t address){
    static const struct {uint64_t address;const char *name;} exports[]={
#include "exports.inc"
    };
    for(const auto& item:exports) if(address==item.address){address=nx::hle_address(item.name);break;}
    auto it=nx::targets.find(address);
    if(it==nx::targets.end())return 0;
    nx::current_name=it->second.first;
    graphics_run(cpu,it->second.second);
    return 1;
}
