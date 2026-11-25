#include <vector>
#include <algorithm>
#include <cstdlib>
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
// Target map entries are never erased or reassigned. Their names and handlers
// therefore remain valid for the process lifetime, including across insertions.
static thread_local const std::string* current_name = nullptr;
static std::mutex& target_mutex() { static std::mutex mutex; return mutex; }
uint64_t hle_address_or(const char *name,HleFn fallback){std::lock_guard guard(target_mutex());auto found=addresses.find(name);if(found!=addresses.end())return found->second;uint64_t address=0x7200000000ull+addresses.size()*16;auto fn=hle_lookup(name);targets[address]={name,fn?fn:fallback};addresses[name]=address;return address;}
static void missing(CPU&){std::fprintf(stderr,"Unimplemented graphics call %s\n",(current_name ? current_name->c_str() : ""));std::exit(4);}
uint64_t hle_address(const char *name){return hle_address_or(name,missing);}
const char *hle_current_name(){return (current_name ? current_name->c_str() : "");}
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
extern "C" int graphics_has(uint64_t address){std::lock_guard guard(nx::target_mutex());return nx::targets.find(address)!=nx::targets.end();}
namespace nx {
bool g_trace=false;
const LoadedModule* module_at(uint64_t){return nullptr;}
uint64_t find_symbol(const char* name){return std::strcmp(name,"nvnBootstrapLoader")==0?0x7005454d90ull:0;}
uint64_t kernel_alloc(uint64_t size,uint64_t){void *p=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!p)fatal("Graphics memory allocation failed");return (uint64_t)p;}
}
namespace nx::plat {
void input_script(PadState&){} size_t touch_overlay(OverlayShape*,size_t){return 0;}
void perf_hint_thread(){
    thread_local bool done = false;
    if (done) return;
    done = true;
    static const bool on = [] { const char* e = std::getenv("CATHERINE_PARALLEL134"); return !(e && e[0] == '0'); }();
    if (!on) return;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    THREAD_POWER_THROTTLING_STATE throttling{};
    throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;  // never throttled
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof throttling);
    static const std::vector<ULONG> performance = [] {
        std::vector<ULONG> ids;
        ULONG length = 0;
        GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
        if (!length) return ids;
        std::vector<uint8_t> buffer(length);
        auto* info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data());
        if (!GetSystemCpuSetInformation(info, length, &length, GetCurrentProcess(), 0)) return ids;
        BYTE best = 0, worst = 255;
        for (ULONG at = 0; at < length;) {
            auto* s = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + at);
            if (s->Type == CpuSetInformation) best = std::max(best, s->CpuSet.EfficiencyClass), worst = std::min(worst, s->CpuSet.EfficiencyClass);
            at += s->Size;
        }
        if (best == worst) return ids;  // not a hybrid CPU: leave scheduling alone
        for (ULONG at = 0; at < length;) {
            auto* s = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + at);
            if (s->Type == CpuSetInformation && s->CpuSet.EfficiencyClass == best) ids.push_back(s->CpuSet.Id);
            at += s->Size;
        }
        return ids;
    }();
    if (!performance.empty()) SetThreadSelectedCpuSets(GetCurrentThread(), performance.data(), ULONG(performance.size()));
}
void perf_hint_frame(int64_t){} void frame_thread_tick(int){}
void thread_counters(uint64_t& n,long f[2],long s[2]){n=0;f[0]=f[1]=s[0]=s[1]=0;}
uint64_t memguard_overlap_ns(uint64_t,uint64_t){return 0;}
bool write_watch_add(uint64_t,uint64_t){return false;} bool write_watch_take(uint64_t){return true;} void write_watch_remove(uint64_t){}
}
namespace nx::nvn {
using RecordingFn = void (*)(const uint64_t*);
RecordingFn game_recording_handler(HleFn);
}
extern "C" void graphics_record_native(void*, nx::nvn::RecordingFn);
extern "C" void graphics_run(void*,nx::HleFn);
extern "C" int graphics_dispatch(void *cpu,uint64_t address){
    static const struct {uint64_t address;const char *name;} exports[]={
#include "exports.inc"
    };
    using Target = std::pair<std::string,nx::HleFn>;
    struct Entry {
        uint64_t address=0;
        const Target* target=nullptr;
        nx::nvn::RecordingFn recording=nullptr;
    };
    static const int direct_graphics = [] {
        const char* value = std::getenv("CATHERINE_DIRECT_GRAPHICS");
        return value ? std::atoi(value) : 1;
    }();
    static thread_local Entry cache[256];
    static const bool enabled = [] {
        const char* value=std::getenv("CATHERINE_FAST_DISPATCH");
        return !value || value[0]!='0';
    }();
    // All synthetic HLE addresses are allocated at/above 0x7200000000.
    // Recompiled indirect calls elsewhere cannot be graphics exports. Keep the
    // complete export interval as a conservative filter before exact matching.
    static const auto bounds=[] {
        std::pair<uint64_t,uint64_t> range{~0ull,0};
        for(const auto& item:exports) {
            if(item.address<range.first) range.first=item.address;
            if(item.address>range.second) range.second=item.address;
        }
        return range;
    }();
    if(enabled && address<0x7200000000ull &&
       (address<bounds.first || address>bounds.second)) return 0;
    // Only successful lookups are cached: an unknown address can be registered
    // later during bootstrap. Retain the original address as the cache key.
    const uint64_t original=address;
    Entry& entry=cache[((address>>4)^(address>>12))&255];
    const Target* target=nullptr;
    nx::nvn::RecordingFn recording=nullptr;
    if(enabled && entry.target && entry.address==original) {
        target=entry.target;
        recording=entry.recording;
    }
    if(!target) {
        for(const auto& item:exports) if(address==item.address){address=nx::hle_address(item.name);break;}
        std::lock_guard guard(nx::target_mutex());
        auto it=nx::targets.find(address);
        if(it==nx::targets.end())return 0;
        target=&it->second;
        if(direct_graphics) recording=nx::nvn::game_recording_handler(target->second);
        if(enabled) entry={original,target,recording};
    }
    nx::current_name=&target->first;
    if(recording) {
        if(direct_graphics == 2) {
            static thread_local const Target* reported[32]{};
            for(auto& item : reported) {
                if(item == target) break;
                if(!item) {
                    item=target;
                    nx::log("native renderer recording: %s", target->first.c_str());
                    break;
                }
            }
        }
        graphics_record_native(cpu,recording);
    } else {
        graphics_run(cpu,target->second);
    }
    return 1;
}


// The packet may bypass only the SDK callback actually installed by the game.
// Unknown aliases/hooks keep their original tail; target entries never change.
extern "C" int catherine_engine_indexed_callback(uint64_t address) {
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached {uint64_t original=0;const Target* target=nullptr;};
    static thread_local Cached cached;
    const uint64_t original=address;
    if(cached.target&&cached.original==original) {
        nx::current_name=&cached.target->first;
        return 1;
    }
    static const struct { uint64_t address; const char* name; } exports[] = {
#include "exports.inc"
    };
    for (const auto& item : exports)
        if (item.address == address) { address = nx::hle_address(item.name); break; }
    std::lock_guard guard(nx::target_mutex());
    const auto found = nx::targets.find(address);
    if (found == nx::targets.end() ||
        found->second.second != nx::hle_lookup("nvnCommandBufferDrawElementsBaseVertex")) return 0;
    // Successful std::map entries never change or move. Unknown addresses are
    // not cached: bootstrap can register those later. Callback changes recheck.
    cached={original,&found->second};
    nx::current_name = &found->second.first;
    return 1;
}

extern "C" int catherine_engine_array_callback(uint64_t address) {
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached {uint64_t original=0;const Target* target=nullptr;};
    static thread_local Cached cached;
    const uint64_t original=address;
    if(cached.target&&cached.original==original) {
        nx::current_name=&cached.target->first;
        return 1;
    }
    static const struct { uint64_t address; const char* name; } exports[] = {
#include "exports.inc"
    };
    for (const auto& item : exports)
        if (item.address == address) { address = nx::hle_address(item.name); break; }
    std::lock_guard guard(nx::target_mutex());
    const auto found = nx::targets.find(address);
    if (found == nx::targets.end() ||
        found->second.second != nx::hle_lookup("nvnCommandBufferDrawArrays")) return 0;
    // Successful std::map entries never change or move. Unknown addresses are
    // not cached: bootstrap can register those later. Callback changes recheck.
    cached={original,&found->second};
    nx::current_name = &found->second.first;
    return 1;
}


extern "C" int catherine_compute_callback(uint32_t kind,uint64_t address) {
    static const char* names[]={"nvnBufferGetAddress","nvnCommandBufferBindProgram",
        "nvnCommandBufferBindUniformBuffer","nvnCommandBufferBindStorageBuffer","nvnCommandBufferDispatchCompute"};
    if(kind>=5)return 0;
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached{uint64_t original=0;const Target* target=nullptr;};
    static thread_local Cached cached[5];
    auto& entry=cached[kind];const uint64_t original=address;
    if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
    static const struct{uint64_t address;const char* name;} exports[]={
#include "exports.inc"
    };
    for(const auto& item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
    std::lock_guard guard(nx::target_mutex());
    const auto found=nx::targets.find(address);
    if(found==nx::targets.end()||found->second.second!=nx::hle_lookup(names[kind]))return 0;
    entry={original,&found->second};nx::current_name=&found->second.first;return 1;
}
extern "C" int catherine_compute_preflight(const uint64_t* callbacks) {
    for(uint32_t kind=0;kind<5;++kind)if(!catherine_compute_callback(kind,callbacks[kind]))return 0;
    return 1;
}

extern "C" int catherine_engine_vertex_callback(uint64_t address) {
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached {uint64_t original=0;const Target* target=nullptr;};
    static thread_local Cached cached;
    const uint64_t original=address;
    if(cached.target&&cached.original==original) {
        nx::current_name=&cached.target->first;
        return 1;
    }
    static const struct { uint64_t address; const char* name; } exports[] = {
#include "exports.inc"
    };
    for (const auto& item : exports)
        if (item.address == address) { address = nx::hle_address(item.name); break; }
    std::lock_guard guard(nx::target_mutex());
    const auto found = nx::targets.find(address);
    if (found == nx::targets.end() ||
        found->second.second != nx::hle_lookup("nvnCommandBufferBindVertexBuffer")) return 0;
    // Successful std::map entries never change or move. Unknown addresses are
    // not cached: bootstrap can register those later. Callback changes recheck.
    cached={original,&found->second};
    nx::current_name = &found->second.first;
    return 1;
}




extern "C" int catherine_viewport_callback(uint32_t kind,uint64_t address){
    static const char* names[]={"nvnCommandBufferSetDepthRange","nvnCommandBufferSetViewports","nvnCommandBufferSetScissors","nvnCommandBufferSetDepthRanges"};
    if(kind>=4)return 0;using Target=std::pair<std::string,nx::HleFn>;
    struct Cached{uint64_t original=0;const Target*target=nullptr;};static thread_local Cached cached[4];
    auto&entry=cached[kind];const uint64_t original=address;
    if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
    static const struct{uint64_t address;const char*name;} exports[]={
#include "exports.inc"
    };
    for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
    std::lock_guard guard(nx::target_mutex());const auto found=nx::targets.find(address);
    if(found==nx::targets.end()||found->second.second!=nx::hle_lookup(names[kind]))return 0;
    entry={original,&found->second};nx::current_name=&found->second.first;return 1;
}


extern "C" int catherine_polygon_callback(uint32_t kind,uint64_t address){
    static const char*names[]={"nvnCommandBufferBindPolygonState","nvnCommandBufferSetPolygonOffsetClamp","nvnCommandBufferBindMultisampleState"};
    if(kind>=3)return 0;using Target=std::pair<std::string,nx::HleFn>;
    struct Cached{uint64_t original=0;const Target*target=nullptr;};static thread_local Cached cached[3];
    auto&entry=cached[kind];const uint64_t original=address;
    if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
    static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
    };
    for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
    std::lock_guard guard(nx::target_mutex());const auto found=nx::targets.find(address);
    if(found==nx::targets.end()||found->second.second!=nx::hle_lookup(names[kind]))return 0;
    entry={original,&found->second};nx::current_name=&found->second.first;return 1;
}


extern "C" int catherine_fixed_callback(uint32_t expected,uint64_t address){
 static const char*names[]={"nvnCommandBufferBindBlendState","nvnCommandBufferBindColorState","nvnCommandBufferBindChannelMaskState","nvnCommandBufferSetBlendColor","nvnCommandBufferBindDepthStencilState","nvnCommandBufferSetStencilMask","nvnCommandBufferSetStencilValueMask","nvnCommandBufferSetStencilRef","nvnCommandBufferSetDepthBounds"};
 if(expected>9)return 0;using Target=std::pair<std::string,nx::HleFn>;
 struct Cached{uint64_t original=0;const Target*target=nullptr;uint32_t expected=0,actual=0;};static thread_local Cached cached[64];
 const uint64_t original=address;auto&entry=cached[((address>>4)^(address>>12)^(expected*13))&63];
 if(entry.target&&entry.original==original&&entry.expected==expected){nx::current_name=&entry.target->first;return int(entry.actual+1);}
 static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
 };
 for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
 std::lock_guard guard(nx::target_mutex());const auto found=nx::targets.find(address);if(found==nx::targets.end())return 0;
 uint32_t actual=9;
 if(expected<9){if(found->second.second==nx::hle_lookup(names[expected]))actual=expected;}
 else for(uint32_t kind=5;kind<8;++kind)if(found->second.second==nx::hle_lookup(names[kind])){actual=kind;break;}
 if(actual>=9)return 0;entry={original,&found->second,expected,actual};nx::current_name=&found->second.first;return int(actual+1);
}


extern "C" int catherine_is_ignored_polygon_callback(nx::HleFn fn,const char*name);
extern "C" int catherine_polygon_option_callback(uint32_t kind,uint64_t address){
 static const char*names[]={"nvnCommandBufferSetSampleMask","nvnCommandBufferSetDepthClamp","nvnCommandBufferSetRasterizerDiscard","nvnCommandBufferSetConservativeRasterEnable"};
 if(kind>=4)return 0;using Target=std::pair<std::string,nx::HleFn>;
 struct Cached{uint64_t original=0;const Target*target=nullptr;uint32_t actual=0;};static thread_local Cached cached[4];
 auto&entry=cached[kind];const uint64_t original=address;
 if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return int(entry.actual+1);}
 static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
 };
 for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
 std::lock_guard guard(nx::target_mutex());const auto found=nx::targets.find(address);if(found==nx::targets.end())return 0;
 const auto&target=found->second;
 uint32_t actual=4;
 for(uint32_t candidate=0;candidate<4;++candidate){
  if(candidate==0||candidate==3){if(target.first==names[candidate]&&catherine_is_ignored_polygon_callback(target.second,target.first.c_str())){actual=candidate;break;}}
  else{const auto wanted=nx::hle_lookup(names[candidate]);if(wanted&&target.second==wanted){actual=candidate;break;}}
 }
 if(actual==4)return 0;entry={original,&target,actual};nx::current_name=&target.first;return int(actual+1);
}


extern "C" int catherine_layout_callback(uint32_t kind,uint64_t address){
 static const char*names[]={"nvnCommandBufferBindVertexAttribState","nvnCommandBufferBindVertexStreamState"};
 if(kind>=2)return 0;using Target=std::pair<std::string,nx::HleFn>;struct Cached{uint64_t original=0;const Target*target=nullptr;};static thread_local Cached cached[2];
 const uint64_t original=address;auto&entry=cached[kind];if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
 static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
 };
 for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
 std::lock_guard guard(nx::target_mutex());const auto found=nx::targets.find(address);if(found==nx::targets.end())return 0;
 const auto wanted=nx::hle_lookup(names[kind]);if(!wanted||found->second.second!=wanted)return 0;entry={original,&found->second};nx::current_name=&entry.target->first;return 1;
}


extern "C" int catherine_texture_callback(uint32_t kind,uint64_t address){
 static const char*names[]={"nvnDeviceGetTextureHandle","nvnCommandBufferBindTexture"};if(kind>=2)return 0;
 using Target=std::pair<std::string,nx::HleFn>;struct Cached{uint64_t original=0;const Target*target=nullptr;};static thread_local Cached cached[2];
 const uint64_t original=address;auto&entry=cached[kind];if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
 static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
 };
 for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
 std::lock_guard guard(nx::target_mutex());const auto found=nx::targets.find(address);if(found==nx::targets.end())return 0;const auto wanted=nx::hle_lookup(names[kind]);if(!wanted||found->second.second!=wanted)return 0;
 entry={original,&found->second};nx::current_name=&entry.target->first;return 1;
}

extern "C" int catherine_engine_buffer_map_callback(uint64_t address) {
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached{uint64_t original=0;const Target* target=nullptr;};
    static thread_local Cached cached;
    const uint64_t original=address;
    if(cached.target&&cached.original==original){nx::current_name=&cached.target->first;return 1;}
    static const struct{uint64_t address;const char* name;} exports[]={
#include "exports.inc"
    };
    for(const auto& item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
    std::lock_guard guard(nx::target_mutex());
    const auto found=nx::targets.find(address);
    if(found==nx::targets.end()||found->second.second!=nx::hle_lookup("nvnBufferMap"))return 0;
    cached={original,&found->second};nx::current_name=&found->second.first;return 1;
}

extern "C" int catherine_engine_pool_callback(uint32_t kind,uint64_t address){
    if(kind!=1&&kind!=2)return 0;
    static const char*names[]={"nvnCommandBufferSetTexturePool","nvnCommandBufferSetSamplerPool"};
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached{uint64_t original=0;const Target*target=nullptr;};static thread_local Cached cache[2];
    auto&entry=cache[kind-1];const uint64_t original=address;
    if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
    static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
    };
    for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
    std::lock_guard lock(nx::target_mutex());const auto found=nx::targets.find(address);
    if(found==nx::targets.end()||found->second.second!=nx::hle_lookup(names[kind-1]))return 0;
    entry={original,&found->second};nx::current_name=&found->second.first;return 1;
}

extern "C" int catherine_engine_clear_callback(uint32_t kind,uint64_t address){
    if(kind>1)return 0;
    static const char*names[]={"nvnCommandBufferSetScissor","nvnCommandBufferClearDepthStencil"};
    using Target=std::pair<std::string,nx::HleFn>;
    struct Cached{uint64_t original=0;const Target*target=nullptr;};static thread_local Cached cache[2];
    auto&entry=cache[kind];const uint64_t original=address;
    if(entry.target&&entry.original==original){nx::current_name=&entry.target->first;return 1;}
    static const struct{uint64_t address;const char*name;}exports[]={
#include "exports.inc"
    };
    for(const auto&item:exports)if(item.address==address){address=nx::hle_address(item.name);break;}
    std::lock_guard lock(nx::target_mutex());const auto found=nx::targets.find(address);
    if(found==nx::targets.end()||found->second.second!=nx::hle_lookup(names[kind]))return 0;
    entry={original,&found->second};nx::current_name=&found->second.first;return 1;
}
