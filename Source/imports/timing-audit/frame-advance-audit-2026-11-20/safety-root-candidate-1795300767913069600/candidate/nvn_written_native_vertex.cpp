// Compute results within a submit (docs/nvn/GLITCH-PLAN.md G5). A storage buffer a compute shader writes is a copy in
// the upload buffer, written back to guest memory once the submit completed. Each binding used to get a copy of its
// own: later draws and dispatches of the submit read the guest bytes from before the dispatch, two dispatches writing
// one buffer overwrote each other at write-back, and indirect dispatch arguments were read before the GPU wrote them.
// Here the submit keeps one copy per written guest range (the ranges are disjoint, keyed by host address). Writers and
// readers of a range bind that copy; ranges that meet are merged into a new copy on the GPU (vkCmdCopyBuffer, so in
// command order); each range is written back once. NX_COMPUTE_SEPARATE=1: the previous behaviour.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <mutex>

#if defined(_WIN32) && defined(__x86_64__)
#include <windows.h>
#include <immintrin.h>
#include <intrin.h>
#endif

#include "nvn_draw_state.h"
#include "watched-geometry.h"
#include "vincent_pose_upload.h"
#include "spike_pose_upload.h"
#include "campaign_block_pose_upload.h"
#include "block20_pose_upload.h"
#include "root12_translation.h"
#include "root15_family.h"
#include "root15_command_packet.h"
#include "root9_translation.h"
#include "written-page-presence.h"
#include "interpolated-skin-output.h"
#include "pose_replay_context.h"
#include "parallel_pool80.h"

namespace cmdthread86 { void install_on_replay_thread(); }
namespace nx::nvn { Upload frame_skin_vertex_alloc(uint64_t size); }

namespace nx::nvn::replay {
// parallel80: large copies split across the helper pool (memcpy of disjoint 32 KB spans: same bytes).
inline void copy80(void* dst, const void* src, size_t size) {
    constexpr size_t kSpan = 32 << 10;
    if (size < 4 * kSpan || !parallel80::Pool::get().on()) {
        std::memcpy(dst, src, size);
        return;
    }
    struct Ctx { uint8_t* d; const uint8_t* s; size_t n; } ctx{static_cast<uint8_t*>(dst), static_cast<const uint8_t*>(src), size};
    parallel80::Pool::get().run(uint32_t((size + kSpan - 1) / kSpan), [](void* c, uint32_t k) {
        const Ctx& x = *static_cast<const Ctx*>(c);
        const size_t at = size_t(k) * kSpan, left = x.n - at, n = left < kSpan ? left : kSpan;
        std::memcpy(x.d + at, x.s + at, n);
    }, &ctx);
}

using namespace nx::vk;

namespace {

constexpr uint64_t kAlign = 256;          // the largest buffer offset alignment Vulkan allows (all descriptor kinds)
constexpr uint64_t kTail = 0x10000 + 16;  // zeros after a range: storage bindings are padded to 64 KB (nvn_draw.cpp)

struct AsyncSkinJob;
enum class SkinJoinReason {Read,Write,Provenance,Seal,Erase,Submit,Reset};

struct Region {
    uint64_t size = 0;  // guest bytes from the key
    uint64_t cap = 0;   // bytes of the copy from up.offset (size, then zeros)
    Upload up;          // the copy of the key's first byte
    std::shared_ptr<std::vector<uint8_t>> before;  // the guest bytes copied in (what the GPU did not change)
    bool gpu_accessed = true;  // an earlier recorded GPU command can already read or write the upload
    bool gpu_written = true;   // the GPU may write the upload, so write-back needs its fence
    bool skin_vertex = false;  // this exact host region was produced by the CPU skin kernel
    Upload skin_mirror{};       // device-local vertex-only copy of this version
    uint64_t skin_mirror_cap = 0;
    uint64_t morph_identity_count=0;uint32_t morph_identity_stride=0;
    std::shared_ptr<AsyncSkinJob> async_skin;
};
using Regions = std::map<const uint8_t*, Region>;
using It = Regions::iterator;
Regions g_regions;
void wait_async_skin_region(Region&,SkinJoinReason);
void wait_async_skin_jobs(SkinJoinReason);
void async_skin_log();
void trim_async_skin_arena();
WrittenPagePresence g_written_pages;
VerifiedSkinOutputs g_verified_skin_outputs;
bool g_dispatch_palette_interpolated = false, g_dispatch_world_morph_base = false;
#include "zero_morph_core.inc"
#include "skin_device_mirror_core.inc"
uint64_t g_page_queries = 0, g_page_rejects = 0, g_page_tree_empty = 0, g_page_mismatches = 0;

int written_page_mode() {
    static const int mode = [] {
        const char* value = std::getenv("NX_WRITTEN_PAGE_PREFILTER");
        return value && (value[0] == '1' || value[0] == '2') && !value[1] ? value[0] - '0' : 0;
    }();
    return mode;
}

void mark_written_region(const uint8_t* p, uint64_t size) {
    if (written_page_mode()) g_written_pages.mark(reinterpret_cast<uintptr_t>(p), size);
}

void clear_regions() {
    wait_async_skin_jobs(SkinJoinReason::Reset);
    g_skin_mirror_pending=false;
    g_regions.clear();
    trim_async_skin_arena();
    if (written_page_mode()) g_written_pages.clear();
}
bool g_read = false;  // a copy was bound for reading since the last write_after_read
bool g_graphics_storage_seen = false;  // a draw could have written an upload outside g_regions
uint64_t g_binds = 0;  // written_bind calls, ever (nvn_frames.cpp: did a dispatch write storage after the split)
uint64_t g_shared = 0, g_merged = 0, g_composed = 0, g_indirect = 0;  // for written_log
uint64_t g_morph_seen = 0, g_morph_done = 0, g_morph_vertices = 0;
uint64_t g_skin_seen = 0, g_skin_done = 0, g_skin_vertices = 0, g_skin_nan_vertices = 0, g_skin_nan_positions = 0;

struct RegionSpan {
    It first, last;
    bool empty() const { return first == last; }
    bool single() const { return !empty() && std::next(first) == last; }
};

// Ordered non-overlapping regions form one contiguous span of the map.
RegionSpan overlapping(const uint8_t* p, uint64_t size) {
    const int mode = written_page_mode();
    const bool reject = mode && !g_written_pages.may_overlap(reinterpret_cast<uintptr_t>(p), size);
    if (mode == 1 && reject) return {g_regions.end(), g_regions.end()};
    It last = g_regions.upper_bound(p);
    It first = last;
    if (last != g_regions.begin()) {
        It prev = std::prev(last);
        if (prev->first + prev->second.size > p) first = prev;
    }
    while (last != g_regions.end() && last->first < p + size) ++last;
    if (mode == 2) {
        ++g_page_queries;
        g_page_rejects += reject;
        g_page_tree_empty += first == last;
        g_page_mismatches += reject && first != last;
    }
    return {first, last};
}

// `bytes` of upload memory whose byte `anchor` is kAlign aligned (so it can start a binding).
Upload alloc_anchored(uint64_t bytes, uint64_t anchor) {
    Upload u = upload_alloc(bytes + kAlign, kAlign);
    uint64_t shift = (kAlign - anchor % kAlign) % kAlign;
    u.offset += shift, u.ptr += shift;
    return u;
}

struct BeforeSnapshots {
    using Snapshot = std::shared_ptr<std::vector<uint8_t>>;
    struct Bucket {
        std::vector<Snapshot> storage;
        size_t next = 0;
    };

    Snapshot capture(const uint8_t* source, size_t size) {
        constexpr size_t maximum_bytes = 24u << 20;
        constexpr size_t maximum_entries = 768;
        if (!size || size > (2u << 20))
            return std::make_shared<std::vector<uint8_t>>(source, source + size);
        auto it = groups.find(size);
        if (it != groups.end()) {
            auto& bucket = it->second;
            // The typical frame has the same sequence of snapshots as the prior frame.
            // Look only a few slots ahead when a GPU-written range still owns an older copy.
            for (size_t i = 0; i < std::min<size_t>(bucket.storage.size(), 8); ++i) {
                auto& candidate = bucket.storage[bucket.next];
                bucket.next = (bucket.next + 1) % bucket.storage.size();
                if (candidate.use_count() != 1) continue;
                copy80(candidate->data(), source, size);
                return candidate;
            }
        }
        auto snapshot = std::make_shared<std::vector<uint8_t>>(source, source + size);
        if (entries < maximum_entries && snapshot->capacity() <= maximum_bytes - bytes) {
            if (it == groups.end()) it = groups.try_emplace(size).first;
            it->second.storage.push_back(snapshot);
            bytes += snapshot->capacity();
            ++entries;
        }
        return snapshot;
    }

    std::unordered_map<size_t, Bucket> groups;
    size_t bytes = 0, entries = 0;
};

BeforeSnapshots g_before_snapshots;

BeforeSnapshots::Snapshot capture_before(const uint8_t* source, uint64_t size) {
    static const bool cache_before = [] {
        const char* setting = std::getenv("NX_SHADOW_SNAPSHOT_CACHE");
        return setting && setting[0] == '1';
    }();
    return cache_before ? g_before_snapshots.capture(source, size)
                        : std::make_shared<std::vector<uint8_t>>(source, source + size);
}

Region make_region(const uint8_t* p, uint64_t size, uint64_t capacity, uint64_t anchor) {
    Region r;
    r.size = size;
    r.cap = capacity;
    r.up = alloc_anchored(capacity, anchor);
    std::memcpy(r.up.ptr, p, size);
    std::memset(r.up.ptr + size, 0, capacity - size);
    r.before = capture_before(r.up.ptr, size);
    return r;
}

// The parts of `regions` inside [p, p + size) copied on the GPU into `to` (whose first byte is p's), then made
// visible to everything after. What the dispatches recorded so far wrote is there when the copy runs: dispatch()
// ends each one with a barrier to all later commands. Outside render passes.
void copy_regions(VkCommandBuffer cb, RegionSpan regions, const uint8_t* p, uint64_t size, const Upload& to) {
    for (It it = regions.first; it != regions.last; ++it) {
        const uint8_t* lo = std::max(it->first, p);
        const uint8_t* hi = std::min(it->first + it->second.size, p + size);
        if (lo >= hi) continue;
        wait_async_skin_region(it->second,SkinJoinReason::Erase);
        it->second.gpu_accessed = true;
        VkBufferCopy c{it->second.up.offset + uint64_t(lo - it->first), to.offset + uint64_t(lo - p), uint64_t(hi - lo)};
        p_vkCmdCopyBuffer(cb, it->second.up.buffer, to.buffer, 1, &c);
    }
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr,
                           0, nullptr);
}

// A dispatch about to write a copy that earlier commands of the submit read (draws binding it as vertices, uniforms,
// storage): they finish first. Earlier writes are already ordered by dispatch()'s barrier.
void write_after_read(VkCommandBuffer cb) {
    if (!g_read) return;
    g_read = false;
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                           nullptr, 0, nullptr);
}

// The device's buffer descriptor offset alignment (kAlign at most, by the Vulkan spec).
uint64_t bind_align() {
    static const uint64_t a = [] {
        VkPhysicalDeviceProperties props{};
        p_vkGetPhysicalDeviceProperties(vk_physical(), &props);
        return std::max<uint64_t>({props.limits.minStorageBufferOffsetAlignment,
                                   props.limits.minUniformBufferOffsetAlignment, 16});
    }();
    return a;
}

Upload at(const It& it, uint64_t off) { return {it->second.up.buffer, it->second.up.offset + off, it->second.up.ptr + off}; }

// [p, p + size) as a sub-range of one region, bindable at `align` (at most the device's descriptor alignment; the
// callers ask 256 for descriptors) with `padded` bytes behind it: else end(). A copy is anchored at its first writer,
// so two pointers aligned in guest memory are aligned in the copy.
It containing(const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align, RegionSpan regions) {
    if (!regions.single()) return g_regions.end();
    It it = regions.first;
    uint64_t off = uint64_t(p - it->first);
    if (p < it->first || off + size > it->second.size || off + padded > it->second.cap) return g_regions.end();
    if ((it->second.up.offset + off) % std::clamp<uint64_t>(align, 1, bind_align())) return g_regions.end();
    return it;
}

#if defined(_WIN32) && defined(__x86_64__)

// CPU computations may read prior CPU results, but they cannot read a copy whose GPU writer has not run yet.
const uint8_t* cpu_view(const uint8_t* p, uint64_t size) {
    const RegionSpan regions = overlapping(p, size);
    if (regions.empty()) return p;
    It it = containing(p, size, size, 1, regions);
    if(it != g_regions.end() && !it->second.gpu_accessed) wait_async_skin_region(it->second,SkinJoinReason::Read);
    if(it != g_regions.end() && it->second.gpu_accessed && it->second.skin_mirror.buffer) ++g_skin_mirror_stats.blocked_cpu_reads;
    return it != g_regions.end() && !it->second.gpu_accessed ? at(it, uint64_t(p - it->first)).ptr : nullptr;
}

// The upload stays CPU-writable only until a recorded GPU command uses it. Changing that copy afterward
// would change what the earlier command sees when Vulkan eventually submits it.
bool find_cpu_destination(const uint8_t* p, uint64_t size, It& result) {
    result = g_regions.end();
    const RegionSpan existing = overlapping(p, size);
    if (existing.empty()) return true;
    for(auto it=existing.first;it!=existing.last;++it) wait_async_skin_region(it->second,SkinJoinReason::Write);
    if(existing.single() && existing.first->second.gpu_accessed && existing.first->second.skin_mirror.buffer) ++g_skin_mirror_stats.blocked_cpu_writers;
    if (!existing.single() || existing.first->first != p || existing.first->second.size < size ||
        existing.first->second.gpu_accessed) return false;
    result = existing.first;
    return true;
}

uint64_t cpu_destination_capacity(uint64_t size) {
    static const bool lazy_tail = [] {
        const char* setting = std::getenv("NX_CPU_LAZY_TAIL");
        return setting && setting[0] == '1' && setting[1] == '\0';
    }();
    return size + (lazy_tail ? 16 : kTail);
}

float* ensure_cpu_destination(const uint8_t* p, uint64_t size, It existing) {
    if (existing == g_regions.end()) {
        Region r = make_region(p, size, cpu_destination_capacity(size), 0);
        r.gpu_accessed = r.gpu_written = false;
        existing = g_regions.emplace(p, std::move(r)).first;
        mark_written_region(p, size);
        dedup_invalidate(p, size);
    }
    wait_async_skin_region(existing->second,SkinJoinReason::Write);
    invalidate_skin_mirror(existing->second);
    return reinterpret_cast<float*>(existing->second.up.ptr);
}

int mode107() {
    static const int m = [] { const char* e = std::getenv("CATHERINE_PARALLEL107"); return e ? std::atoi(e) : 1; }();
    return m;
}
Region prepare_fresh_skin_destination(const uint8_t* p, uint64_t size) {
    Region r;
    r.size = size;
    r.cap = cpu_destination_capacity(size);
    r.up = alloc_anchored(r.cap, 0);
    if (mode107() != 1) r.before = capture_before(p, size);  // cmd107: none in mode 1
    r.gpu_accessed = r.gpu_written = false;
    return r;
}

void commit_fresh_skin_destination(const uint8_t* p, uint64_t written, Region&& r) {
    // The kernel filled the full record prefix, including W from the snapshot.
    if (written < r.size) std::memcpy(r.up.ptr + written, (r.before ? r.before->data() : p) + written, r.size - written);
    std::memset(r.up.ptr + r.size, 0, r.cap - r.size);
    const uint64_t size = r.size;
    mark_skin_vertex(r);
    g_regions.emplace(p, std::move(r));
    mark_written_region(p, size);
    dedup_invalidate(p, size);
}

bool aligned_disjoint(const uint8_t* source, uint64_t source_size, const uint8_t* dest, uint64_t dest_size) {
    const uintptr_t src = reinterpret_cast<uintptr_t>(source), dst = reinterpret_cast<uintptr_t>(dest);
    return !((src | dst) & 3) && source_size <= UINTPTR_MAX - src && dest_size <= UINTPTR_MAX - dst &&
           (src >= dst + dest_size || dst >= src + source_size);
}

bool finite_morph_inputs(const uint8_t* source, const uint8_t* base, uint32_t count, uint32_t base_stride) {
    const __m128i exponent = _mm_set1_epi32(0x7f800000);
    for (uint32_t i = 0; i < count; ++i) {
        const __m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + uint64_t(i) * 16));
        const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(base + uint64_t(i) * base_stride));
        const __m128i exceptional = _mm_or_si128(_mm_cmpeq_epi32(_mm_and_si128(s, exponent), exponent),
                                                _mm_cmpeq_epi32(_mm_and_si128(d, exponent), exponent));
        if (_mm_movemask_ps(_mm_castsi128_ps(exceptional)) & 7) return false;
    }
    return true;
}

// These kernels accumulate xyz from a 16-byte source record. The separate base also has a 16-byte stride.
// Match the tested NVIDIA shader's fused rounding and signed flush-to-zero without changing the caller's FP state.
__attribute__((target("avx,fma"))) void run_cpu_morph(const float* source, const float* base, float* dest,
                                                       uint32_t count, float weight, uint32_t dest_words,
                                                       uint32_t base_words) {
    const unsigned saved = _mm_getcsr();
    _mm_setcsr((saved & ~_MM_ROUND_MASK) | _MM_ROUND_NEAREST | _MM_MASK_MASK | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
    const __m256 weight8 = _mm256_set1_ps(weight);
    const bool in_place = base == dest;
    uint32_t i = 0;
    for (; i + 1 < count; i += 2) {
        const float* src = source + uint64_t(i) * 4;
        float* dst = dest + uint64_t(i) * dest_words;
        const __m256 prior = _mm256_set_m128(_mm_loadu_ps(dst + dest_words), _mm_loadu_ps(dst));
        const __m256 initial = in_place ? prior : _mm256_loadu_ps(base + uint64_t(i) * base_words);
        const __m256 out = _mm256_blend_ps(prior, _mm256_fmadd_ps(_mm256_loadu_ps(src), weight8, initial), 0x77);
        _mm_storeu_ps(dst, _mm256_castps256_ps128(out));
        _mm_storeu_ps(dst + dest_words, _mm256_extractf128_ps(out, 1));
    }
    if (i < count) {
        float* dst = dest + uint64_t(i) * dest_words;
        const __m128 prior = _mm_loadu_ps(dst);
        const __m128 initial = in_place ? prior : _mm_loadu_ps(base + uint64_t(i) * base_words);
        const __m128 inputs = _mm_loadu_ps(source + uint64_t(i) * 4);
        _mm_storeu_ps(dst, _mm_blend_ps(prior, _mm_fmadd_ps(inputs, _mm256_castps256_ps128(weight8), initial), 7));
    }
    _mm_setcsr(saved);
}

bool cpu_compute_opt_in(const char* name) {
    const char* env = std::getenv(name);
    if (!env || env[0] != '1' || (async_gpu() && !async_cpu_compute()) || written_separate()) return false;
    static const bool compatible = [] {
        VkPhysicalDeviceProperties props{};
        p_vkGetPhysicalDeviceProperties(vk_physical(), &props);
        int cpu[4];
        __cpuid(cpu, 1);
        return props.vendorID == 0x10de && props.deviceID == 0x2b85 &&
               IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE) && (cpu[2] & (1 << 12));
    }();
    return compatible;
}

struct CpuShaderSignature {
    const char* hash;
    uint64_t fingerprint;
    const ShaderInfo* checked = nullptr;
    bool matches = false;
};

bool cpu_shader_matches(const ShaderInfo& cs, CpuShaderSignature& signature) {
    if (cs.hash != signature.hash || cs.local_size[0] != 64 || cs.local_size[1] != 1 || cs.local_size[2] != 1)
        return false;
    if (signature.checked != &cs) {
        uint64_t fingerprint = 14695981039346656037ull;
        for (uint32_t word : cs.spirv) fingerprint = (fingerprint ^ word) * 1099511628211ull;
        signature.matches = fingerprint == signature.fingerprint;
        signature.checked = &cs;
    }
    return signature.matches;
}

bool cpu_kernel_bindings(const ShaderInfo& cs, int source_slot, int base_slot, uint32_t dest_binding, int dest_slot) {
    const DescriptorBinding* constants = cs.refl.find(0, 4);
    auto storage_slot = [&](uint32_t binding, int slot) {
        const DescriptorBinding* b = cs.refl.find(1, binding);
        return b && b->nvn.source == NvnSource::StorageBuffer && b->nvn.index == slot;
    };
    return constants && constants->nvn.source == NvnSource::UniformBlock && constants->nvn.index == 0 &&
           storage_slot(0, source_slot) && storage_slot(1, base_slot) && storage_slot(dest_binding, dest_slot) &&
           (written_storage_bindings(&cs) & (1ull << dest_binding));
}

struct MorphKernel {
    CpuShaderSignature signature;
    uint32_t dest_stride;
    bool separate_base;
};

const MorphKernel* morph_kernel(const ShaderInfo& cs) {
    static MorphKernel kernels[] = {
        {{"9ae85fe23552c4e8", 0x5ea09c80344d0893ull}, 32, false},
        {{"4843060eb5a152ba", 0x9a32ed01ed2b13f7ull}, 64, false},
        {{"69a4a203a4c2fb60", 0x87f112eb3ab225b4ull}, 32, true},
        {{"70053e80370597a3", 0xf2fb1ddce4be5062ull}, 64, true},
    };
    for (MorphKernel& kernel : kernels)
        if (cpu_shader_matches(cs, kernel.signature)) return &kernel;
    return nullptr;
}

bool bounded_skin_floats(const uint8_t* p, int lanes = 15) {
    const __m128i bits = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    const __m128i magnitude = _mm_and_si128(bits, _mm_set1_epi32(0x7fffffff));
    // Magnitudes <= 2^30 keep even a sum of four four-term weighted products finite.
    const __m128i too_big = _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(0x4e800000));
    return !(_mm_movemask_ps(_mm_castsi128_ps(too_big)) & lanes);
}

bool canonical_skin_nan_opt_in(const char* name) {
    const char* e = std::getenv(name);
    if (!e || e[0] != '1') return false;
    static const bool compatible = [] {
        VkPhysicalDeviceProperties props{};
        p_vkGetPhysicalDeviceProperties(vk_physical(), &props);
        return props.vendorID == 0x10de && props.deviceID == 0x2b85 && props.driverVersion == 0x9b404000u;
    }();
    return compatible;
}

using SkinJoints = std::array<uint32_t, 4>;

enum class SkinReject {
    GraphicsStorage, DispatchShape, Bindings, Address, ArgsAccess, RangeSize, MatrixCount, GuestAlias,
    SourceAccess, PaletteAccess, DestinationAccess, PaletteValue, JointIndex, VertexValue, Count
};

constexpr const char* kSkinRejectNames[] = {
    "graphics-storage", "dispatch-shape", "bindings", "address", "args-access", "range-size", "matrix-count",
    "guest-alias", "source-access", "palette-access", "destination-access", "palette-value", "joint-index",
    "vertex-value"
};
static_assert(std::size(kSkinRejectNames) == size_t(SkinReject::Count));
std::array<uint64_t, std::size(kSkinRejectNames)> g_skin_rejected{}, g_skin_rejection_details{};

bool trace_skin_rejection_enabled() {
    static const char* from = std::getenv("NX_TRACE_CPU_SKIN_REJECT");
    static const uint64_t frame = from ? std::strtoull(from, nullptr, 10) : 0;
    return from && g_frame.load() >= frame;
}

void trace_skin_rejection(SkinReject reason, const ShaderInfo& cs, uint32_t x, uint32_t y, uint32_t z,
                          uint32_t count, uint32_t matrices, const Range& args, const Range& source,
                          const Range& palette, const Range& dest, const uint8_t* ptr, uint64_t size,
                          uint32_t element, const SkinJoints* joints) {
    const size_t i = size_t(reason);
    ++g_skin_rejected[i];
    if (g_skin_rejection_details[i]++ >= 4) return;
    log("nvn: CPU skin rejected %s shader %s frame %llu groups %ux%ux%u count %u matrices %u "
        "args=%llx+%llx src=%llx+%llx palette=%llx+%llx dst=%llx+%llx element %u",
        kSkinRejectNames[i], cs.hash.c_str(), (unsigned long long)g_frame.load(), x, y, z, count, matrices,
        (unsigned long long)args.address, (unsigned long long)args.size,
        (unsigned long long)source.address, (unsigned long long)source.size,
        (unsigned long long)palette.address, (unsigned long long)palette.size,
        (unsigned long long)dest.address, (unsigned long long)dest.size, element);
    if (joints)
        log("nvn: CPU skin rejected joint indices %#x %#x %#x %#x",
            (*joints)[0], (*joints)[1], (*joints)[2], (*joints)[3]);
    if (!ptr || !size) return;
    const RegionSpan regions = overlapping(ptr, size);
    log("nvn: CPU skin rejected host range %p+%llx has %zu shadow overlaps",
        (const void*)ptr, (unsigned long long)size, size_t(std::distance(regions.first, regions.last)));
    unsigned shown = 0;
    for (It it = regions.first; it != regions.last && shown++ < 4; ++it) {
        const Region& region = it->second;
        log("nvn: CPU skin overlap %p+%llx cap %llx upload %#llx state %s", (const void*)it->first,
            (unsigned long long)region.size, (unsigned long long)region.cap, (unsigned long long)region.up.offset,
            region.gpu_written ? "GPU possible writer" : region.gpu_accessed ? "GPU reader" : "CPU only");
    }
}

void log_skin_rejections() {
    std::string summary;
    for (size_t i = 0; i < g_skin_rejected.size(); ++i) {
        if (g_skin_rejected[i])
            summary += " " + std::string(kSkinRejectNames[i]) + "=" + std::to_string(g_skin_rejected[i]);
    }
    if (!summary.empty()) log("nvn: CPU skin rejection counts:%s", summary.c_str());
    g_skin_rejected = {};
}

bool valid_skin_inputs(const uint8_t* source, const uint8_t* palette, uint32_t count, uint32_t matrices,
                       bool tangent, SkinJoints* joints, SkinReject& reason, uint32_t& element) {
    for (uint32_t i = 0; i < matrices; ++i) {
        const uint8_t* m = palette + uint64_t(i) * 64;
        if (!bounded_skin_floats(m) || !bounded_skin_floats(m + 16) || !bounded_skin_floats(m + 32)) {
            reason = SkinReject::PaletteValue;
            element = i;
            return false;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* s = source + uint64_t(i) * (tangent ? 80 : 64);
        const uint8_t* weights = s + (tangent ? 48 : 32);
        SkinJoints& j = joints[i];
        std::memcpy(j.data(), weights + 16, sizeof(uint32_t) * j.size());
        if (j[0] >= matrices || j[1] >= matrices || j[2] >= matrices || j[3] >= matrices) {
            reason = SkinReject::JointIndex;
            element = i;
            return false;
        }
        if (!bounded_skin_floats(s) || !bounded_skin_floats(s + 16, 7) || !bounded_skin_floats(weights) ||
            (tangent && !bounded_skin_floats(s + 32, 7))) {
            reason = SkinReject::VertexValue;
            element = i;
            return false;
        }
    }
    return true;
}

struct SkinMatrix {
    __m128 columns[4];
};

__attribute__((target("avx,fma"))) __m128 skin_dot(const SkinMatrix& m, __m128 v, bool translate) {
    const __m128 x = _mm_shuffle_ps(v, v, 0);
    const __m128 y = _mm_shuffle_ps(v, v, 0x55);
    const __m128 z = _mm_shuffle_ps(v, v, 0xaa);
    __m128 out = _mm_mul_ps(x, m.columns[0]);
    out = _mm_fmadd_ps(y, m.columns[1], out);
    out = _mm_fmadd_ps(z, m.columns[2], out);
    if (translate) {
        const __m128 w = _mm_shuffle_ps(v, v, 0xff);
        out = _mm_fmadd_ps(w, m.columns[3], out);
    }
    return out;
}

// Match the SPIR-V order: multiply the bone 1 result, then accumulate bones 0, 2 and 3 with fused operations.
__attribute__((target("avx,fma"))) void run_cpu_skin(const float* source, const float* palette, float* dest,
                                                    uint32_t count, uint32_t matrix_count, bool tangent,
                                                    const SkinJoints* validated_joints) {
    static std::vector<SkinMatrix> matrices;  // replay runs under the GPU lock
    matrices.resize(matrix_count);
    const unsigned saved = _mm_getcsr();
    _mm_setcsr((saved & ~_MM_ROUND_MASK) | _MM_ROUND_NEAREST | _MM_MASK_MASK | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
    for (uint32_t i = 0; i < matrix_count; ++i) {
        const float* m = palette + uint64_t(i) * 16;
        __m128 a = _mm_loadu_ps(m), b = _mm_loadu_ps(m + 4), c = _mm_loadu_ps(m + 8), d = _mm_setzero_ps();
        _MM_TRANSPOSE4_PS(a, b, c, d);
        matrices[i] = {{a, b, c, d}};
    }
    for (uint32_t i = 0; i < count; ++i) {
        const float* s = source + uint64_t(i) * (tangent ? 20 : 16);
        float* d = dest + uint64_t(i) * (tangent ? 12 : 8);
        const float* weights = s + (tangent ? 12 : 8);
        const SkinJoints& joints = validated_joints[i];
        const __m128 p = _mm_loadu_ps(s), n = _mm_loadu_ps(s + 4);
        const __m128 t = tangent ? _mm_loadu_ps(s + 8) : _mm_setzero_ps();
        const SkinMatrix& first = matrices[joints[1]];
        const __m128 initial_weight = _mm_set1_ps(weights[1]);
        __m128 pos = _mm_mul_ps(skin_dot(first, p, true), initial_weight);
        __m128 normal = _mm_mul_ps(skin_dot(first, n, false), initial_weight);
        __m128 tang = tangent ? _mm_mul_ps(skin_dot(first, t, false), initial_weight) : _mm_setzero_ps();
        for (uint32_t b : {0u, 2u, 3u}) {
            const SkinMatrix& m = matrices[joints[b]];
            const __m128 w = _mm_set1_ps(weights[b]);
            pos = _mm_fmadd_ps(w, skin_dot(m, p, true), pos);
            normal = _mm_fmadd_ps(w, skin_dot(m, n, false), normal);
            if (tangent) tang = _mm_fmadd_ps(w, skin_dot(m, t, false), tang);
        }
        _mm_storeu_ps(d, _mm_blend_ps(pos, _mm_set1_ps(1.0f), 8));
        _mm_storeu_ps(d + 4, _mm_blend_ps(_mm_loadu_ps(d + 4), normal, 7));
        if (tangent) _mm_storeu_ps(d + 8, _mm_blend_ps(_mm_loadu_ps(d + 8), tang, 7));
    }
    _mm_setcsr(saved);
}

#include "nvn_skin_fused.inc"

// parallel80: the fused kernel over 256-vertex chunks on the helper pool. Each vertex reads only its own source
// record, the read-only palette and its own preserved prior bytes, and writes only its own destination record, so
// the chunks can run in any order and produce the same bytes. The destination is a fresh, unpublished region (it is
// committed only on success), so chunks past a rejected vertex write nothing anyone reads. The failure reported is
// the lowest failing vertex, the one the serial loop stops at.
template<bool Tangent>
bool run_cpu_skin_fused80(const uint8_t* source, const std::vector<SkinMatrix>& matrices, float* dest,
                          const float* preserved, uint32_t count, bool allow_canonical, SkinReject& reason,
                          uint32_t& element, SkinJoints& bad_joints, uint64_t& canonical_count) {
    constexpr uint32_t kChunk = 256;
    parallel80::Pool& pool = parallel80::Pool::get();
    static uint64_t calls = 0;
    if ((++calls & 0x3fff) == 0)
        log("parallel80: helpers %d, skin calls %llu, jobs %llu, chunks %llu, helper chunks %llu, serial jobs %llu",
            int(pool.on()), (unsigned long long)calls, (unsigned long long)parallel80::g_stats.jobs.load(),
            (unsigned long long)parallel80::g_stats.chunks.load(),
            (unsigned long long)parallel80::g_stats.helper_chunks.load(),
            (unsigned long long)parallel80::g_stats.serial_jobs.load());
    if (!pool.on() || count < 2 * kChunk)
        return run_cpu_skin_fused<Tangent>(source, matrices, dest, preserved, count, allow_canonical, reason, element,
                                           bad_joints, canonical_count);
    struct Part {
        bool ok;
        SkinReject reason;
        uint32_t element;
        SkinJoints bad;
        uint64_t canonical;
    };
    static std::vector<Part> parts;  // replay thread only
    const uint32_t chunks = (count + kChunk - 1) / kChunk;
    parts.assign(chunks, Part{});
    struct Ctx {
        const uint8_t* source;
        const std::vector<SkinMatrix>* matrices;
        float* dest;
        const float* preserved;
        uint32_t count;
        bool allow_canonical;
        Part* parts;
    } ctx{source, &matrices, dest, preserved, count, allow_canonical, parts.data()};
    pool.run(chunks, [](void* c, uint32_t k) {
        const Ctx& x = *static_cast<const Ctx*>(c);
        const uint32_t first = k * kChunk, left = x.count - first, n = left < kChunk ? left : kChunk;
        constexpr uint64_t in = Tangent ? 80 : 64, out = Tangent ? 12 : 8;
        Part& part = x.parts[k];
        part.ok = run_cpu_skin_fused<Tangent>(x.source + first * in, *x.matrices, x.dest + first * out,
                                              x.preserved + first * out, n, x.allow_canonical, part.reason,
                                              part.element, part.bad, part.canonical);
    }, &ctx);
    uint64_t canonical = 0;
    for (uint32_t k = 0; k < chunks; ++k) {
        if (!parts[k].ok) {
            reason = parts[k].reason;
            element = k * kChunk + parts[k].element;
            bad_joints = parts[k].bad;
            return false;
        }
        canonical += parts[k].canonical;
    }
    canonical_count += canonical;
    return true;
}

#include "skin_async_validate.inc"
#include "skin_async_compact_kernel.inc"
#include "skin_async_jobs.inc"

#endif

// Read-only inspection differs from cpu_view: an upload already read by the
// GPU is still safe to inspect if no recorded command can write it.
const uint8_t* skin_readable_view(const uint8_t* p, uint64_t size) {
    if (!p || !size) return nullptr;
    const RegionSpan regions = overlapping(p, size);
    if (regions.empty()) return p;
    const It it = containing(p, size, size, 1, regions);
    if(it != g_regions.end() && !it->second.gpu_written) wait_async_skin_region(it->second,SkinJoinReason::Provenance);
    return it != g_regions.end() && !it->second.gpu_written ? at(it, uint64_t(p - it->first)).ptr : nullptr;
}

}  // namespace

// The same audited palette path when CPU skin execution is disabled/rejected.
// Reject GPU-written source/argument aliases rather than reading stale bytes.
const uint8_t *spike_gpu_palette(const uint8_t *palette,uint64_t size,const ShaderInfo &cs) {
    if(cs.hash!="01c7cddd0aab2b5c" || !palette || size!=28*64)return palette;
    const auto &args=g_state.uniforms[5][0];const auto &src=g_state.storage[5][1];
    if(args.size<4)return palette;
    const auto *a=skin_readable_view(guest_bytes(args.address),4);
    if(!a)return palette;uint32_t count;std::memcpy(&count,a,4);
    if(count!=360 && count!=500 && count!=400)return palette;
    uint64_t bytes=uint64_t(count)*80;if(src.size<bytes)return palette;
    const auto *v=skin_readable_view(guest_bytes(src.address),bytes);
    return v?spike_pose_palette(palette,size,v,bytes,count,80):palette;
}

// Exact campaign 3-bone block fallback; readable immutable source only.
const uint8_t*campaign_block_gpu_palette(const uint8_t*palette,uint64_t bytes,const ShaderInfo&cs){
 if(cs.hash!="01c7cddd0aab2b5c"||!palette||bytes!=192)return palette;
 const auto&args=g_state.uniforms[5][0];const auto&src=g_state.storage[5][1];
 const auto*a=args.size>=4?skin_readable_view(guest_bytes(args.address),4):nullptr;if(!a)return palette;
 uint32_t count;std::memcpy(&count,a,4);if(count!=2989||src.size<239120)return palette;
 const auto*s=skin_readable_view(guest_bytes(src.address),239120);
 return s?campaign_block_pose_palette(palette,bytes,s,239120,count,80):palette;
}

// Native breakable-block fallback; the immutable owner packet must match this bind.

// Only an immutable native command packet may identify a type12 root.
static catherine::visual::Root12Translation& root12_history() {
    return catherine::visual::root12_presentation();
}
static const uint8_t* root12_pose_palette(const uint8_t* palette,uint64_t bytes,
    const uint8_t* source,uint64_t used,uint32_t count,uint32_t stride) {
    auto& history=root12_history();
    const uint64_t clock=puzzle_render_clock();
    if(!(clock&0x80000000u)){history.clear();return palette;}
    const auto& range=g_state.storage[5][0];
    const auto* owner=nx::nvn::root15_bound_owner(range.address,range.size);
    if(!owner||catherine::root15::family(*owner)!=12||!owner->lifetime||bytes!=960)return palette;
    if(!nx::nvn::root15_bound_matches(range.address,palette,bytes))return palette;
    catherine::visual::Block20Owner identity{owner->block,owner->generation,owner->model,owner->parent,owner->lifetime};
    const std::array<float,3> root{owner->world[12],owner->world[13],owner->world[14]};
    const auto* result=history.render(true,identity,root,palette,bytes,source,used,count,stride,puzzle_render_frame(),clock);
    return result;
}
static const uint8_t* root12_gpu_palette(const uint8_t* palette,uint64_t bytes,const ShaderInfo& cs) {
    if(!palette||bytes!=960||!vincent_skin_shader(cs))return palette;
    const auto& bound=g_state.storage[5][0];
    const auto* owner=nx::nvn::root15_bound_owner(bound.address,bound.size);
    if(!(puzzle_render_clock()&0x80000000u)){root12_history().clear();return palette;}
    if(bound.size!=960||!owner||catherine::root15::family(*owner)!=12||!owner->lifetime)return palette;
    const bool tangent=cs.hash=="01c7cddd0aab2b5c";
    const auto& args=g_state.uniforms[5][0];const auto& source=g_state.storage[5][1];
    const auto* a=args.size>=4?skin_readable_view(guest_bytes(args.address),4):nullptr;if(!a)return palette;
    uint32_t count;std::memcpy(&count,a,4);uint32_t stride=tangent?80:64;
    if(!((tangent&&(count==298||count==273||count==79))||(!tangent&&count==64)))return palette;
    uint64_t used=uint64_t(count)*stride;if(used>source.size)return palette;
    const auto* s=skin_readable_view(guest_bytes(source.address),used);
    if(!s)return palette;
    const auto* result=root12_pose_palette(palette,bytes,s,used,count,stride);
    if(result!=palette)written_pose_palette_interpolated();
    return result;
}

// The palette and its represented root come from one immutable native copy.
static const uint8_t* root9_pose_palette(const uint8_t* palette,uint64_t bytes,
    const uint8_t* source,uint64_t used,uint32_t count,uint32_t stride){
    auto& history=catherine::visual::root9_presentation();const uint64_t clock=puzzle_render_clock();
    if(!(clock&0x80000000u)){history.clear();return palette;}
    const auto& bound=g_state.storage[5][0];const auto* owner=nx::nvn::root15_bound_owner(bound.address,bound.size);
    if(!owner||catherine::root15::family(*owner)!=9||!owner->lifetime||bytes!=960||!nx::nvn::root15_bound_matches(bound.address,palette,bytes))return palette;
    uint32_t produced=uint32_t(owner->clock>>32),now=uint32_t(clock>>32);
    if(!(owner->clock&0x80000000u)||now<produced||now-produced>2)return palette;
    catherine::visual::Block20Owner identity{owner->block,owner->generation,owner->model,owner->parent,owner->lifetime};
    const std::array<float,3> root{owner->world[12],owner->world[13],owner->world[14]};
    return history.palette(true,identity,root,palette,bytes,source,used,count,stride,puzzle_render_frame(),clock);
}
static const uint8_t* root9_gpu_palette(const uint8_t* palette,uint64_t bytes,const ShaderInfo& cs){
    if(!palette||bytes!=960||cs.hash!="eb49fe48c6f84ebf")return palette;
    if(!(puzzle_render_clock()&0x80000000u)){catherine::visual::root9_presentation().clear();return palette;}
    const auto& bound=g_state.storage[5][0];const auto* owner=nx::nvn::root15_bound_owner(bound.address,bound.size);
    if(!owner||catherine::root15::family(*owner)!=9||!owner->lifetime||bound.size!=960)return palette;
    const uint64_t clock=puzzle_render_clock();uint32_t produced=uint32_t(owner->clock>>32),now=uint32_t(clock>>32);
    if(!(owner->clock&0x80000000u)||now<produced||now-produced>2||!nx::nvn::root15_bound_matches(bound.address,palette,bytes))return palette;
    const auto& args=g_state.uniforms[5][0];const auto& source=g_state.storage[5][1];
    const auto* a=args.size>=4?skin_readable_view(guest_bytes(args.address),4):nullptr;if(!a)return palette;
    uint32_t count;std::memcpy(&count,a,4);if(count!=25||source.size<1600)return palette;
    const auto* s=skin_readable_view(guest_bytes(source.address),1600);if(!s)return palette;
    const auto* out=root9_pose_palette(palette,bytes,s,1600,25,64);
    if(out!=palette)written_pose_palette_interpolated();return out;
}

const uint8_t* block20_gpu_palette(const uint8_t* palette,uint64_t bytes,const ShaderInfo& cs) {
    if(bytes==960){
        const auto& range=g_state.storage[5][0];const auto* owner=nx::nvn::root15_bound_owner(range.address,range.size);
        if(owner&&catherine::root15::family(*owner)==9)return root9_gpu_palette(palette,bytes,cs);
        if(!(puzzle_render_clock()&0x80000000u))catherine::visual::root9_presentation().clear();
        return root12_gpu_palette(palette,bytes,cs);
    }
    const bool tangent = cs.hash == "01c7cddd0aab2b5c";
    if (!vincent_skin_shader(cs) || !palette || bytes != 1280) return palette;
    const auto& args = g_state.uniforms[5][0];
    const auto& source = g_state.storage[5][1];
    const auto* arguments = args.size >= 4 ? skin_readable_view(guest_bytes(args.address),4) : nullptr;
    if (!arguments) return palette;
    uint32_t count; std::memcpy(&count,arguments,4);
    const uint32_t stride = tangent ? 80 : 64;
    const uint64_t used = uint64_t(count)*stride;
    if (!((count==393 && tangent) || (count==1457 && !tangent)) || used > source.size) return palette;
    const auto* readable = skin_readable_view(guest_bytes(source.address),used);
    return readable ? block20_pose_palette(palette,bytes,readable,used,count,stride) : palette;
}

#include "zero_morph_checks.inc"

bool try_cpu_morph(const ShaderInfo& cs, uint32_t x, uint32_t y, uint32_t z) {
#if defined(_WIN32) && defined(__x86_64__)
    static const bool enabled = cpu_compute_opt_in("NX_CPU_MORPH");
    if (!enabled) return false;
    const MorphKernel* kernel = morph_kernel(cs);
    if (!kernel) return false;
    ++g_morph_seen;
    if (g_graphics_storage_seen || y != 1 || z != 1 || x > UINT32_MAX / 64) return false;
    const bool separate = kernel->separate_base;
    const int source_slot = separate ? 1 : 0, base_slot = separate ? 0 : 1, dest_slot = separate ? 2 : 1;
    const uint32_t dest_binding = separate ? 2 : 1;
    if (!cpu_kernel_bindings(cs, source_slot, base_slot, dest_binding, dest_slot)) return false;

    const Range& args = g_state.uniforms[5][0];
    const Range& src = g_state.storage[5][source_slot];
    const Range& dst = g_state.storage[5][dest_slot];
    const uint8_t* arg_bytes = args.size >= 8 ? guest_bytes(args.address) : nullptr;
    const uint8_t* src_bytes = guest_bytes(src.address);
    const uint8_t* dst_bytes = guest_bytes(dst.address);
    if (!arg_bytes || !src_bytes || !dst_bytes) return false;
    arg_bytes = cpu_view(arg_bytes, 8);
    if (!arg_bytes) return false;
    uint32_t count, weight_bits;
    std::memcpy(&count, arg_bytes, sizeof count);
    std::memcpy(&weight_bits, arg_bytes + 4, sizeof weight_bits);
    count = std::min(count, x * 64);
    if (!count || (weight_bits & 0x7f800000) == 0x7f800000) return false;
    float weight;
    std::memcpy(&weight, &weight_bits, sizeof weight);
    constexpr uint64_t storage_cap = 16ull << 20;
    const uint32_t dest_stride = kernel->dest_stride, base_stride = separate ? 16 : dest_stride;
    const uint64_t src_used = uint64_t(count) * 16, dst_used = uint64_t(count) * dest_stride;
    const uint64_t dst_size = std::min(dst.size, storage_cap);
    if (src_used > std::min(src.size, storage_cap) || dst_used > dst_size) return false;
    if (!aligned_disjoint(src_bytes, src_used, dst_bytes, dst_size)) return false;
    src_bytes = cpu_view(src_bytes, src_used);
    if (!src_bytes) return false;
    const uint8_t* separate_base = nullptr;
    if (separate) {
        const Range& base = g_state.storage[5][base_slot];
        separate_base = guest_bytes(base.address);
        if (!separate_base || src_used > std::min(base.size, storage_cap) ||
            !aligned_disjoint(separate_base, src_used, dst_bytes, dst_size)) return false;
        separate_base = cpu_view(separate_base, src_used);
        if (!separate_base) return false;
    }

    It existing;
    if (!find_cpu_destination(dst_bytes, dst_size, existing)) return false;
    const uint8_t* base = existing == g_regions.end() ? dst_bytes : existing->second.up.ptr;
    uint64_t identity_count=0;
    const int zero_mode=zero_morph_mode();
    const bool zero_candidate=zero_mode&&!separate&&(weight_bits&0x7fffffffu)==0;
    if(zero_candidate)++g_zero_morph_stats.considered;
    const bool identity=zero_candidate&&zero_morph_source_finite(src_bytes,count)&&
        zero_morph_base_certificate(existing,base,count,dest_stride,identity_count);
    if (!identity&&!finite_morph_inputs(src_bytes, separate ? separate_base : base, count, base_stride)) return false;
    const bool fresh=existing==g_regions.end();
    float* destination = ensure_cpu_destination(dst_bytes, dst_size, existing);
    const float* initial = separate ? reinterpret_cast<const float*>(separate_base) : destination;
    bool owned_identity=identity;
    if(identity&&fresh){++g_zero_morph_stats.owned_rechecks;owned_identity=zero_morph_base_identity(reinterpret_cast<const uint8_t*>(destination),count,dest_stride);}
    if(owned_identity&&zero_mode==1)++g_zero_morph_stats.skipped;
    else{
        std::vector<uint8_t> before;
        if(owned_identity&&zero_mode==2)before.assign(reinterpret_cast<const uint8_t*>(destination),reinterpret_cast<const uint8_t*>(destination)+dst_size);
        run_cpu_morph(reinterpret_cast<const float*>(src_bytes), initial, destination, count, weight,
                      dest_stride / 4, base_stride / 4);
        if(owned_identity&&zero_mode==2){++g_zero_morph_stats.verified;
            if(std::memcmp(before.data(),destination,dst_size)!=0){++g_zero_morph_stats.mismatches;owned_identity=false;}
        }
    }
    if(owned_identity){auto& region=g_regions.find(dst_bytes)->second;region.morph_identity_count=identity_count;region.morph_identity_stride=dest_stride;}
    ++g_morph_done;
    g_morph_vertices += count;
    return true;
#else
    (void)cs;
    (void)x;
    (void)y;
    (void)z;
    return false;
#endif
}

bool try_cpu_skin(const ShaderInfo& cs, uint32_t x, uint32_t y, uint32_t z) {
#if defined(_WIN32) && defined(__x86_64__)
    static const bool enabled = cpu_compute_opt_in("NX_CPU_SKIN");
    if (!enabled) return false;
    static CpuShaderSignature regular{"eb49fe48c6f84ebf", 0xda83f468f5fb7ec9ull};
    static CpuShaderSignature with_tangent{"01c7cddd0aab2b5c", 0x1b818c23fea4dab9ull};
    const bool tangent = cpu_shader_matches(cs, with_tangent);
    if (!tangent && !cpu_shader_matches(cs, regular)) return false;
    ++g_skin_seen;

    const Range& args = g_state.uniforms[5][0];
    const Range& src = g_state.storage[5][1];
    const Range& palette = g_state.storage[5][0];
    const Range& dst = g_state.storage[5][2];
    uint32_t count = 0, matrix_count = 0;
    auto reject = [&](SkinReject reason, const uint8_t* ptr = nullptr, uint64_t size = 0,
                      uint32_t element = 0, const SkinJoints* joints = nullptr) {
        if (trace_skin_rejection_enabled())
            trace_skin_rejection(reason, cs, x, y, z, count, matrix_count, args, src, palette, dst, ptr, size, element,
                                 joints);
        return false;
    };
    if (g_graphics_storage_seen) return reject(SkinReject::GraphicsStorage);
    if (y != 1 || z != 1 || x > UINT32_MAX / 64) return reject(SkinReject::DispatchShape);
    if (!cpu_kernel_bindings(cs, 1, 0, 2, 2)) return reject(SkinReject::Bindings);
    const uint8_t* arg_bytes = args.size >= 4 ? guest_bytes(args.address) : nullptr;
    const uint8_t* src_bytes = guest_bytes(src.address);
    const uint8_t* palette_bytes = guest_bytes(palette.address);
    const uint8_t* dst_bytes = guest_bytes(dst.address);
    if (!arg_bytes || !src_bytes || !palette_bytes || !dst_bytes) return reject(SkinReject::Address);
    const uint8_t* arg_view = cpu_view(arg_bytes, 4);
    if (!arg_view) return reject(SkinReject::ArgsAccess, arg_bytes, 4);
    std::memcpy(&count, arg_view, sizeof count);
    count = std::min(count, x * 64);
    constexpr uint64_t storage_cap = 16ull << 20;
    const uint64_t src_used = uint64_t(count) * (tangent ? 80 : 64);
    const uint64_t dst_used = uint64_t(count) * (tangent ? 48 : 32);
    const uint64_t dst_size = std::min(dst.size, storage_cap);
    matrix_count = uint32_t(std::min(palette.size, storage_cap) / 64);
    const uint64_t palette_used = uint64_t(matrix_count) * 64;
    if (!count || src_used > std::min(src.size, storage_cap) || dst_used > dst_size) return reject(SkinReject::RangeSize);
    if (!matrix_count || matrix_count > 4096) return reject(SkinReject::MatrixCount);
    if (!aligned_disjoint(src_bytes, src_used, dst_bytes, dst_size) ||
        !aligned_disjoint(palette_bytes, palette_used, dst_bytes, dst_size))
        return reject(SkinReject::GuestAlias);
    const uint8_t* src_view = cpu_view(src_bytes, src_used);
    if (!src_view) return reject(SkinReject::SourceAccess, src_bytes, src_used);
    const uint8_t* palette_view = cpu_view(palette_bytes, palette_used);
    if (!palette_view) return reject(SkinReject::PaletteAccess, palette_bytes, palette_used);
    palette_view=vincent_pose_palette(palette_view,palette_used,src.address,palette.address);
    palette_view=spike_pose_palette(palette_view,palette_used,src_view,src_used,count,tangent?80:64);
    palette_view=campaign_block_pose_palette(palette_view,palette_used,src_view,src_used,count,tangent?80:64);
    palette_view=block20_pose_palette(palette_view,palette_used,src_view,src_used,count,tangent?80:64);
    const uint8_t* before_root12=palette_view;
    if(palette_used==960)palette_view=root12_pose_palette(palette_view,palette_used,src_view,src_used,count,tangent?80:64);
    const bool root12_changed=palette_view!=before_root12;
    const uint8_t* before_root9=palette_view;
    if(palette_used==960)palette_view=root9_pose_palette(palette_view,palette_used,src_view,src_used,count,tangent?80:64);
    const bool root9_changed=palette_view!=before_root9;
    It existing;
    if (!find_cpu_destination(dst_bytes, dst_size, existing)) return reject(SkinReject::DestinationAccess, dst_bytes, dst_size);
    static const bool fused = [] {
        const char* setting = std::getenv("NX_CPU_SKIN_FUSE_VALIDATION");
        return setting && setting[0] == '1' && setting[1] == 0;
    }();
    if (fused && !root12_changed && !root9_changed && room_for_speculative_skin(cpu_destination_capacity(dst_size)) &&
        try_async_skin(dst_bytes,dst_size,src_view,src_used,src_bytes,palette_view,palette_used,count,matrix_count,tangent,existing)) {
        ++g_skin_done;g_skin_vertices+=count;return true;
    }
    if (fused && existing == g_regions.end() && room_for_speculative_skin(cpu_destination_capacity(dst_size))) {
        static std::vector<SkinMatrix> matrices;
        SkinReject reason{};
        uint32_t element = 0;
        if (!transpose_skin_palette(palette_view, matrix_count, matrices, element))
            return reject(SkinReject::PaletteValue, nullptr, 0, element);
        Region fresh = prepare_fresh_skin_destination(dst_bytes, dst_size);
        auto* destination = reinterpret_cast<float*>(fresh.up.ptr);
        const auto* preserved = reinterpret_cast<const float*>(fresh.before ? fresh.before->data() : dst_bytes);
        SkinJoints bad_joints;
        uint64_t canonical_count = 0;
        static const bool tangent_nan = canonical_skin_nan_opt_in("NX_CPU_SKIN_NAN_TANGENTS");
        static const bool position_nan = canonical_skin_nan_opt_in("NX_CPU_SKIN_NAN_POSITIONS");
        const bool allow_canonical = tangent ? tangent_nan : position_nan;
        const bool ok = tangent
            ? run_cpu_skin_fused80<true>(src_view, matrices, destination, preserved, count, allow_canonical, reason, element,
                                         bad_joints, canonical_count)
            : run_cpu_skin_fused80<false>(src_view, matrices, destination, preserved, count, allow_canonical, reason, element,
                                          bad_joints, canonical_count);
        if (!ok) return reject(reason, nullptr, 0, element, reason == SkinReject::JointIndex ? &bad_joints : nullptr);
        if(root12_changed||root9_changed)written_pose_palette_interpolated();
        commit_fresh_skin_destination(dst_bytes, dst_used, std::move(fresh));
        (tangent ? g_skin_nan_vertices : g_skin_nan_positions) += canonical_count;
        ++g_skin_done;
        g_skin_vertices += count;
        return true;
    }

    // Replay can overlap the guest recording thread. Index the palette with the values the bounds check saw.
    static std::vector<SkinJoints> joints;
    joints.resize(count);
    SkinReject reason{};
    uint32_t element = 0;
    if (!valid_skin_inputs(src_view, palette_view, count, matrix_count, tangent, joints.data(), reason, element))
        return reject(reason, nullptr, 0, element, reason == SkinReject::JointIndex ? &joints[element] : nullptr);
    float* destination = ensure_cpu_destination(dst_bytes, dst_size, existing);
    run_cpu_skin(reinterpret_cast<const float*>(src_view), reinterpret_cast<const float*>(palette_view), destination,
                 count, matrix_count, tangent, joints.data());
    if(root12_changed||root9_changed)written_pose_palette_interpolated();
    mark_skin_vertex(g_regions.find(dst_bytes)->second);
    ++g_skin_done;
    g_skin_vertices += count;
    return true;
#else
    (void)cs;
    (void)x;
    (void)y;
    (void)z;
    return false;
#endif
}

bool written_separate() {
    static const bool on = std::getenv("NX_COMPUTE_SEPARATE") != nullptr;
    return on;
}

void written_pose_dispatch_begin() {
    g_dispatch_palette_interpolated = g_dispatch_world_morph_base = false;
    const ShaderInfo* cs = g_state.compute_shader;
    if (!cs) return;
    const bool separate = cs->hash == "69a4a203a4c2fb60" || cs->hash == "70053e80370597a3";
    const bool in_place32 = cs->hash == "9ae85fe23552c4e8";
    const bool in_place64 = cs->hash == "4843060eb5a152ba";
    if (!separate && !in_place32 && !in_place64) return;
    const Range& args = g_state.uniforms[5][0];
    const Range& base = g_state.storage[5][separate ? 0 : 1];
    const uint8_t* arg = args.address && args.size >= 4 ? guest_bytes(args.address) : nullptr;
    const uint8_t* p = base.address ? guest_bytes(base.address) : nullptr;
    arg = arg ? skin_readable_view(arg, 4) : nullptr;
    if (!arg || !p) return;
    uint32_t count;
    std::memcpy(&count, arg, 4);
    const uint64_t stride = separate ? 16 : in_place64 ? 64 : 32;
    if (uint64_t(count) * stride > std::min<uint64_t>(base.size, 16ull << 20)) return;
    // Capture the base's coordinate space BEFORE either CPU or GPU overwrites
    // it. Additive morph deltas do not turn an existing world-space base local.
    g_dispatch_world_morph_base = g_verified_skin_outputs.positions(p, count, stride, skin_readable_view);
}

void written_pose_palette_interpolated() {
    g_dispatch_palette_interpolated = true;
}

void written_pose_dispatch_complete(const ShaderInfo& cs, uint32_t groups_x, bool gpu_indirect) {
    const bool skin = vincent_skin_shader(cs);
    const bool morph32 = cs.hash == "9ae85fe23552c4e8" || cs.hash == "69a4a203a4c2fb60";
    const bool morph64 = cs.hash == "4843060eb5a152ba" || cs.hash == "70053e80370597a3";
    const bool separate_morph = cs.hash == "69a4a203a4c2fb60" || cs.hash == "70053e80370597a3";
    const int output_slot = skin || separate_morph ? 2 : 1;
    const Range& destination = g_state.storage[5][output_slot];
    const uint8_t* output = destination.address ? guest_bytes(destination.address) : nullptr;
    uint64_t produced = 0;
    bool exact_prefix = false;
    if ((skin || morph32 || morph64) && !gpu_indirect && output) {
        const Range& args = g_state.uniforms[5][0];
        const uint8_t* arg_bytes = args.address && args.size >= 4 ? guest_bytes(args.address) : nullptr;
        // GPU-dependent counts cannot establish an exact produced prefix.
#if defined(_WIN32) && defined(__x86_64__)
        if (arg_bytes) arg_bytes = cpu_view(arg_bytes, 4);
#else
        if (arg_bytes && written_overlaps(arg_bytes, 4)) arg_bytes = nullptr;
#endif
        if (arg_bytes) {
            uint32_t count;
            std::memcpy(&count, arg_bytes, 4);
            const uint64_t vertices = std::min<uint64_t>(count, uint64_t(groups_x) * 64);
            const uint64_t stride = skin ? (cs.hash == "01c7cddd0aab2b5c" ? 48 : 32) : (morph64 ? 64 : 32);
            produced = vertices * stride;
            exact_prefix = produced <= std::min<uint64_t>(destination.size, 16ull << 20);
        }
    }
    // Clear provenance only after a dispatch was successfully recorded/executed.
    // Known kernels replace an exact prefix, not the unused remainder of their
    // bound allocation. Unknown writers invalidate their entire writable range.
    for (const DescriptorBinding& b : cs.refl.bindings) {
        if (b.set >= 2 || b.nvn.source != NvnSource::StorageBuffer || b.binding >= 64 ||
            !(written_storage_bindings(&cs) & (1ull << b.binding)) || b.nvn.index < 0 || b.nvn.index >= 16)
            continue;
        const Range& r = g_state.storage[5][b.nvn.index];
        if (const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr) {
            const uint64_t size = exact_prefix && b.nvn.index == output_slot
                                      ? produced : std::min<uint64_t>(r.size, 16ull << 20);
            g_verified_skin_outputs.overwrite(p, size);
        }
    }
    // A native or local-space palette never sets the substitution flag. Leave
    // those outputs eligible for c3 anchoring, even when a skin shader made them.
    const bool world_output = skin ? g_dispatch_palette_interpolated : g_dispatch_world_morph_base;
    if (exact_prefix && world_output) {
        const uint64_t stride = skin ? (cs.hash == "01c7cddd0aab2b5c" ? 48 : 32) : (morph64 ? 64 : 32);
        g_verified_skin_outputs.produced(output, produced / stride, stride, skin_readable_view(output, produced));
    }
}

bool written_skin_output(const uint8_t* p, uint64_t size) {
    {
        static uint64_t calls105 = 0;
        if ((++calls105 & 0xfffff) == 0)
            log("cmd105: %llu skin-output checks; memo hits %llu; verify: %llu checked, %llu would have been wrong",
                (unsigned long long)calls105, (unsigned long long)g_verified_skin_outputs.memo_hits105,
                (unsigned long long)g_verified_skin_outputs.memo_checked105, (unsigned long long)g_verified_skin_outputs.memo_wrong105);
    }
    return g_verified_skin_outputs.contains(p, size, skin_readable_view);
}

void written_pose_commit(const WriteBack& w) {
    g_verified_skin_outputs.write_back(w.from, w.to, w.size);
}

uint64_t written_bind_count() { return g_binds; }
void written_graphics_storage(const uint8_t* p, uint64_t size) {
    if (!p || !size) return;
    g_graphics_storage_seen = true;
    if (g_regions.empty()) return;
    const RegionSpan regions = overlapping(p, size);
    if (regions.empty()) return;
    ++g_binds;
    // A graphics storage descriptor can write a CPU-produced shadow. Treat unknown accesses as writes too.
    for (It it = regions.first; it != regions.last; ++it) {
        wait_async_skin_region(it->second,SkinJoinReason::Write);
        invalidate_skin_mirror(it->second);
        it->second.gpu_accessed = it->second.gpu_written = true;
    }
}

Upload written_bind(VkCommandBuffer cb, const uint8_t* p, uint64_t size, uint64_t padded) {
    g_binds++;
    const RegionSpan old = overlapping(p, size);
    for(auto it=old.first;it!=old.last;++it) wait_async_skin_region(it->second,SkinJoinReason::Write);
    if (It it = containing(p, size, padded, bind_align(), old); it != g_regions.end()) {
        write_after_read(cb);
        invalidate_skin_mirror(it->second);
        it->second.gpu_accessed = it->second.gpu_written = true;
        g_shared++;
        return at(it, uint64_t(p - it->first));
    }
    // A new range, or one meeting others (or misaligned in its copy): one copy covering all of them, anchored at p.
    const uint8_t* lo = p;
    const uint8_t* hi = p + size;
    for (It it = old.first; it != old.last; ++it) {
        lo = std::min(lo, it->first);
        hi = std::max(hi, it->first + it->second.size);
    }
    uint64_t n = uint64_t(hi - lo), off = uint64_t(p - lo);
    Region r = make_region(lo, n, std::max(n + kTail, off + padded), off);
    for (It it = old.first; it != old.last; ++it)  // their parts: as they were before their own GPU writes
        if (it->second.before)
            std::memcpy(r.before->data() + (it->first - lo), it->second.before->data(), it->second.before->size());
    if (!old.empty()) {
        copy_regions(cb, old, lo, n, r.up);  // dispatch(): no render pass is open
        g_regions.erase(old.first, old.last);
        g_merged++;
    }
    dedup_invalidate(lo, n);  // copies of these bytes made earlier in the submit are not what later work reads
    It it = g_regions.emplace(lo, r).first;
    mark_written_region(lo, n);
    return at(it, off);
}

bool written_read_impl(VkCommandBuffer cb, const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align, Upload& out,bool allow_pending) {
    if (!p || !size || g_regions.empty() || written_separate()) return false;
    RegionSpan regions = overlapping(p, size);
    if (regions.empty()) return false;
    if (It it = containing(p, size, padded, align, regions); it != g_regions.end()) {
        if(!allow_pending) wait_async_skin_region(it->second,SkinJoinReason::Read);
        g_shared++, g_read = true;
        it->second.gpu_accessed = true;
        out = at(it, uint64_t(p - it->first));
        return true;
    }
    // Partly written, or not bindable in place: a copy of its own, the written parts copied in on the GPU. This is
    // what the range holds at this point of the submit (later dispatches do not change it).
    draw_end_rendering(cb, 2);
    out = upload_alloc(std::max(padded, size), std::max<uint64_t>(align, 16));
    std::memcpy(out.ptr, p, size);
    if (padded > size) std::memset(out.ptr + size, 0, padded - size);
    copy_regions(cb, regions, p, size, out);
    g_composed++;
    return true;
}

bool written_read(VkCommandBuffer cb,const uint8_t* p,uint64_t size,uint64_t padded,uint64_t align,Upload& out) {
    return written_read_impl(cb,p,size,padded,align,out,false);
}

Upload read_vertex_upload(VkCommandBuffer cb,const uint8_t* p,uint64_t size,uint64_t padded,uint64_t align) {
    Upload out;if(written_read_impl(cb,p,size,padded,align,out,true))return out;
    return dedup_upload(p,size,padded,align);
}

Upload read_upload(VkCommandBuffer cb, const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align) {
    Upload u;
    if (written_read(cb, p, size, padded, align, u)) return u;
    return dedup_upload(p, size, padded, align);
}

bool written_overlaps(const uint8_t* p, uint64_t size) {
    return p && size && !g_regions.empty() && !overlapping(p, size).empty();
}

bool written_indirect(VkCommandBuffer cb, const uint8_t* p) {
    Upload u;
    if (!written_read(cb, p, 12, 16, 4, u)) return false;
    p_vkCmdDispatchIndirect(cb, u.buffer, u.offset);  // {x, y, z} as the earlier dispatches left them
    g_indirect++;
    return true;
}

#include "skin_device_mirror_batch.inc"
#include "native_vertex_range.inc"

bool draw_inputs(VkCommandBuffer cb, const ShaderInfo* vs, bool indexed, const Cmd& c, DrawInputs& in) {
    prepare_skin_mirrors(cb);
    // Vertex buffers: the bound ranges of the streams the vertex shader reads (the pipeline declares no others).
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    const bool watched = watched_geometry_enabled(false);
    uint32_t unknown_streams = 0;
#endif
    for (const ShaderIo& io : vs->refl.inputs) {
        if (io.location >= g_state.attribs.size()) continue;
        const auto& attr = g_state.attribs[io.location];
        if (attr.stream >= 15) continue;
        in.used |= 1u << attr.stream;
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
        if (watched && (attr.stream >= g_state.streams.size() || vk_vertex_format(attr.format) == VK_FORMAT_UNDEFINED))
            unknown_streams |= 1u << attr.stream;
#endif
    }
    for (uint32_t s = 0; s < 15; s++) {
        if (!(in.used >> s & 1)) continue;
        const Range& r = g_state.vertex_buffers[s];
        const uint8_t* p = r.address ? guest_bytes(r.address) : nullptr;
        uint64_t size = p ? std::min<uint64_t>(r.size, 64ull << 20) : 0;
        const uint64_t padded = std::max<uint64_t>((size + 15) & ~15ull, 16);
        bool watch_candidate=false;
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
        watch_candidate=watched&&!(unknown_streams>>s&1)&&p&&size>=kWatchedGeometryMinimum;
#endif
        // The screen-gradient special case still forces the original CPU join.
        in.vertex[s]=native_vertex_range::read(cb,p,size,padded,*vs,watch_candidate);
    }
    if (!indexed) return true;
    uint32_t count = uint32_t(c.i[0]);
    const uint8_t* p = guest_bytes(c.a[0]);
    if (!p) return false;
    in.index_type = vk_index_type(c.mask);
    if (in.index_type == VK_INDEX_TYPE_UINT8_EXT) {
        // 8-bit indices: Metal has none, and MoltenVK converts them on the GPU at every draw (that was most of the
        // frame's GPU time), so they are widened here, from guest memory (a dispatch writing them is only logged).
        static int logs = 0;
        if (written_overlaps(p, count) && logs++ < 16) log("nvn: 8-bit indices written by a dispatch: stale");
        in.index = upload_alloc(uint64_t(count) * 2, 16);
        auto* d = reinterpret_cast<uint16_t*>(in.index.ptr);
        for (uint32_t k = 0; k < count; k++) d[k] = p[k];
        in.index_type = VK_INDEX_TYPE_UINT16;
        return true;
    }
    uint64_t bytes = uint64_t(count) * index_size(c.mask);
    if (!written_read(cb, p, bytes, bytes, index_size(c.mask), in.index)) in.index = upload(p, bytes, 16);
    return true;
}

void check_vertex_ranges(const ShaderInfo* vs, const ShaderInfo* fs, bool indexed, const Cmd& c, const DrawInputs& in) {
    static const bool on = std::getenv("NX_CHECK_VERTS") != nullptr;
    static uint64_t found = 0;
    if (!on || !vs) return;
    // The vertices and instances the draw fetches.
    uint64_t lo = 0, hi = 0;  // vertex indices [lo, hi]
    uint32_t count = uint32_t(indexed ? c.i[0] : c.i[1]);
    if (!count) return;
    if (indexed) {
        const uint8_t* p = guest_bytes(c.a[0]);
        if (!p) return;
        uint32_t isz = index_size(c.mask), restart = isz == 1 ? 0xFF : isz == 2 ? 0xFFFF : 0xFFFFFFFFu;
        uint32_t mn = UINT32_MAX, mx = 0;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t v = isz == 1 ? p[k] : isz == 2 ? reinterpret_cast<const uint16_t*>(p)[k] : reinterpret_cast<const uint32_t*>(p)[k];
            if (v == restart) continue;
            mn = std::min(mn, v), mx = std::max(mx, v);
        }
        if (mn == UINT32_MAX) return;
        lo = uint64_t(int64_t(mn) + c.i[1]), hi = uint64_t(int64_t(mx) + c.i[1]);
    } else {
        lo = uint32_t(c.i[0]), hi = uint64_t(uint32_t(c.i[0])) + count - 1;
    }
    uint64_t inst_hi = uint64_t(uint32_t(c.i[2])) + uint32_t(std::max(1, c.i[3])) - 1;
    for (const ShaderIo& io : vs->refl.inputs) {
        if (io.location >= g_state.attribs.size()) continue;
        const VertexAttrib& va = g_state.attribs[io.location];
        if (va.stream >= 15 || va.stream >= g_state.streams.size() || !(in.used >> va.stream & 1)) continue;
        const VertexStream& st = g_state.streams[va.stream];
        const Range& r = g_state.vertex_buffers[va.stream];
        uint64_t last = st.divisor ? inst_hi / st.divisor : hi;
        uint64_t end = last * st.stride + va.offset + vk_block_bytes(vk_vertex_format(va.format));
        uint64_t copied = r.address && guest_bytes(r.address) ? std::min<uint64_t>(r.size, 64ull << 20) : 0;
        if (end <= copied) continue;
        found++;
        static std::map<std::string, uint64_t> seen;
        std::string key = vs->hash + (fs ? fs->hash : std::string("-")) + char('a' + va.stream);
        uint64_t n = ++seen[key];
        if (n <= 3 || (n & (n - 1)) == 0)
            log("nvn: vertex fetch past the bound range (%llu so far; #%llu of vs %s fs %s): stream %u loc %u fmt %#x off %u "
                "stride %u div %u, vertices %llu..%llu instances ..%llu, needs %llu bytes, bound %llu at %llx (copied %llu)",
                (unsigned long long)found, (unsigned long long)n, vs->hash.c_str(), fs ? fs->hash.c_str() : "-", va.stream,
                io.location, va.format, va.offset, st.stride, st.divisor, (unsigned long long)lo, (unsigned long long)hi,
                (unsigned long long)inst_hi, (unsigned long long)end, (unsigned long long)r.size,
                (unsigned long long)r.address, (unsigned long long)copied);
        break;
    }
}

void written_cpu_write(const uint8_t* p, uint64_t size) {
    if ((skin_mirror_mode() || async_skin_enabled() || zero_morph_mode()) && p && size) {
        const auto regions=overlapping(p,size);
        for(auto it=regions.first;it!=regions.last;++it) {wait_async_skin_region(it->second,SkinJoinReason::Write);invalidate_skin_mirror(it->second);}
    }
    g_verified_skin_outputs.overwrite(p, size);
    // UpdateUniformBuffer / ReportCounter into a range a dispatch of this submit writes: the copy misses the bytes
    // (they reach guest memory at replay time) and the write-back replaces them. Not handled: logged.
    static int logs = 0;
    if (written_overlaps(p, size) && logs++ < 16)
        log("nvn: guest write into a compute-written range (%llu bytes): lost at write-back", (unsigned long long)size);
}

void written_wait_async_skin_jobs() {wait_async_skin_jobs(SkinJoinReason::Submit);}

std::vector<WriteBack> written_take() {
    wait_async_skin_jobs(SkinJoinReason::Seal);
    std::vector<WriteBack> v;
    v.reserve(g_regions.size());
    for (auto& [p, r] : g_regions) v.push_back({r.up.ptr, const_cast<uint8_t*>(p), r.size, r.before, !r.gpu_written});
    if (mode107() == 2) {  // cmd107 verify: a guest write into a region since its snapshot
        static uint64_t regions107 = 0, changed107 = 0;
        for (const WriteBack& x : v)
            if (x.before && x.host_ready && x.before->size() >= x.size) {
                ++regions107;
                if (std::memcmp(x.to, x.before->data(), x.size) != 0) ++changed107;
            }
        static uint64_t takes107 = 0;
        if ((++takes107 % 600) == 0)
            log("cmd107 verify: %llu host-ready regions checked, %llu had guest bytes changed since their snapshot",
                (unsigned long long)regions107, (unsigned long long)changed107);
    }
    for (const WriteBack& w : v) g_verified_skin_outputs.seal_write_back(w.from, w.to, w.size);
    clear_regions();
    return v;
}

void written_reset() {
    ::cmdthread86::install_on_replay_thread();
    block20_clear_replay_packet();
    clear_regions();
    g_verified_skin_outputs.begin_submit();
    g_read = g_graphics_storage_seen = false;
}

void written_log() {
    skin_repeat_joints::report();
    zero_morph_log();
    async_skin_log();
    skin_mirror_log();
    if (g_page_queries) {
        log("nvn: written page census: %llu queries, %llu possible skips, %llu empty tree results, %llu mismatches",
            (unsigned long long)g_page_queries, (unsigned long long)g_page_rejects,
            (unsigned long long)g_page_tree_empty, (unsigned long long)g_page_mismatches);
        g_page_queries = g_page_rejects = g_page_tree_empty = g_page_mismatches = 0;
    }
    if (g_shared + g_merged + g_composed + g_indirect)
        log("nvn: compute ranges: %llu bindings of written copies, %llu merged, %llu reads composed on the GPU, %llu indirect",
            (unsigned long long)g_shared, (unsigned long long)g_merged, (unsigned long long)g_composed,
            (unsigned long long)g_indirect);
    g_shared = g_merged = g_composed = g_indirect = 0;
    if (g_morph_seen) {
        log("nvn: CPU morph %llu of %llu eligible shader calls, %llu vertices",
            (unsigned long long)g_morph_done, (unsigned long long)g_morph_seen, (unsigned long long)g_morph_vertices);
        g_morph_seen = g_morph_done = g_morph_vertices = 0;
    }
    if (g_skin_seen) {
        log("nvn: CPU skin %llu of %llu eligible shader calls, %llu vertices",
            (unsigned long long)g_skin_done, (unsigned long long)g_skin_seen, (unsigned long long)g_skin_vertices);
        if (g_skin_nan_vertices)
            log("nvn: CPU skin canonical NaN tangent vertices %llu", (unsigned long long)g_skin_nan_vertices);
        if (g_skin_nan_positions)
            log("nvn: CPU skin canonical NaN position vertices %llu", (unsigned long long)g_skin_nan_positions);
        g_skin_seen = g_skin_done = g_skin_vertices = g_skin_nan_vertices = g_skin_nan_positions = 0;
    }
#if defined(_WIN32) && defined(__x86_64__)
    if (trace_skin_rejection_enabled()) log_skin_rejections();
#endif
}

}  // namespace nx::nvn::replay

namespace nx::nvn {

#if defined(_WIN32) && defined(__x86_64__)
namespace {

// A masked dword store never writes unchanged lanes: the guest may have updated them after our snapshot.
// The scalar path handles the last 0..31 bytes with its original tail rules.
__attribute__((target("avx2"))) uint64_t apply_host_diff(const WriteBack& w, const uint8_t* was) {
    uint64_t k = 0;
    for (; k < (w.size & ~31ull); k += 32) {
        const __m256i from = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w.from + k));
        const __m256i before = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(was + k));
        const __m256i same = _mm256_cmpeq_epi32(from, before);
        if (uint32_t(_mm256_movemask_ps(_mm256_castsi256_ps(same))) != 0xffu) {
            const __m256i changed = _mm256_xor_si256(same, _mm256_set1_epi32(-1));
            _mm256_maskstore_epi32(reinterpret_cast<int*>(w.to + k), changed, from);
        }
    }
    return k;
}

// parallel80: apply_host_diff over 16 KB spans on the helper pool. Each 32-byte block is decided only by its own
// bytes of from/before and writes only its own bytes of the destination, and the spans start on 32-byte
// boundaries, so the blocks written are exactly the serial loop's. Returns the same k (size & ~31).
uint64_t apply_host_diff80(const WriteBack& w, const uint8_t* was) {
    constexpr uint64_t kSpan = 16 << 10;
    const uint64_t blocks = w.size & ~31ull;
    if (blocks < 2 * kSpan || !parallel80::Pool::get().on()) return apply_host_diff(w, was);
    struct Ctx {
        const WriteBack* w;
        const uint8_t* was;
        uint64_t blocks;
    } ctx{&w, was, blocks};
    parallel80::Pool::get().run(uint32_t((blocks + kSpan - 1) / kSpan), [](void* c, uint32_t k) {
        const Ctx& x = *static_cast<const Ctx*>(c);
        const uint64_t at = uint64_t(k) * kSpan, left = x.blocks - at, n = left < kSpan ? left : kSpan;
        WriteBack span{x.w->from + at, x.w->to + at, n};
        apply_host_diff(span, x.was + at);
    }, &ctx);
    return blocks;
}

}  // namespace
#endif

// A bound storage range is often larger than what the shader writes, and the game's recording thread writes the
// rest of those buffers while the submit is replayed. Copying the whole range back put stale bytes over them: meshes
// exploded into beams and a band of stretched triangles at the horizon, for the rest of the run (DEBUG.md #56). Only
// the words the GPU changed go back. NX_WRITE_BACK_WHOLE=1: the old whole-range copy.
void apply_write_back(const WriteBack& w) {
    static const bool whole = std::getenv("NX_WRITE_BACK_WHOLE") != nullptr;
    if (whole || !w.before || w.before->size() < w.size) {
        std::memcpy(w.to, w.from, w.size);
        replay::written_pose_commit(w);
        return;
    }
    const uint8_t* was = w.before->data();
    uint64_t k = 0;
#if defined(_WIN32) && defined(__x86_64__)
    static const bool simd = [] {
        const char* e = std::getenv("NX_CPU_SIMD_WRITEBACK");
        return e && e[0] == '1' && IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE);
    }();
    if (simd && w.host_ready) k = apply_host_diff80(w, was);
#endif
    for (; k + 8 <= w.size; k += 8) {
        if (std::memcmp(w.from + k, was + k, 8) == 0) continue;
        for (uint64_t b = k; b < k + 8; b += 4)
            if (std::memcmp(w.from + b, was + b, 4) != 0) std::memcpy(w.to + b, w.from + b, 4);
    }
    for (; k < w.size; k++)
        if (w.from[k] != was[k]) w.to[k] = w.from[k];
    replay::written_pose_commit(w);
}


// parallel80b: a submit's host-ready write-backs diffed in parallel, then committed in order. Their guest ranges are
// disjoint and each commit reads only its own range, so all diffs then the commits in order equals apply_write_back
// one by one. Each diff runs whole on one helper (the pool is never re-entered).
static void write_back_diff_serial(const WriteBack& w) {
    static const bool whole = std::getenv("NX_WRITE_BACK_WHOLE") != nullptr;
    if (whole || !w.before || w.before->size() < w.size) {
        std::memcpy(w.to, w.from, w.size);
        return;
    }
    const uint8_t* was = w.before->data();
    uint64_t k = 0;
#if defined(_WIN32) && defined(__x86_64__)
    static const bool simd = [] {
        const char* e = std::getenv("NX_CPU_SIMD_WRITEBACK");
        return e && e[0] == '1' && IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE);
    }();
    if (simd && w.host_ready) k = apply_host_diff(w, was);
#endif
    for (; k + 8 <= w.size; k += 8) {
        if (std::memcmp(w.from + k, was + k, 8) == 0) continue;
        for (uint64_t b = k; b < k + 8; b += 4)
            if (std::memcmp(w.from + b, was + b, 4) != 0) std::memcpy(w.to + b, w.from + b, 4);
    }
    for (; k < w.size; k++)
        if (w.from[k] != was[k]) w.to[k] = w.from[k];
}

size_t apply_host_write_backs(std::vector<WriteBack>& v) {
    thread_local std::vector<uint32_t> idx;
    idx.clear();
    for (uint32_t k = 0; k < v.size(); k++)
        if (v[k].host_ready) idx.push_back(k);
    if (idx.empty()) return 0;
    for (uint32_t k : idx) replay::trace_nan_write_back(v[k].from, v[k].to, v[k].size);
    struct Ctx { std::vector<WriteBack>* v; const uint32_t* idx; } ctx{&v, idx.data()};
    parallel80::Pool::get().run(uint32_t(idx.size()), [](void* c, uint32_t k) {
        const Ctx& x = *static_cast<const Ctx*>(c);
        write_back_diff_serial((*x.v)[x.idx[k]]);
    }, &ctx);
    for (uint32_t k : idx) replay::written_pose_commit(v[k]);
    const size_t n = idx.size();
    std::erase_if(v, [](const WriteBack& w) { return w.host_ready; });
    return n;
}

}  // namespace nx::nvn
