// sets87: a resolved set 0/1 binding reused when its inputs are the same as last time (nvn_sets.cpp build_sets only).
// Per DescriptorBinding (each shader's binding has its own entry), the last descriptor info computed and what it was
// computed from. Reused only while the upload ring's contents are kept (epoch: every dedup_reset, i.e. each submit and
// each ring wrap, starts a new one) and when the inputs match exactly:
//   uniform block (not the vertex bank-3 block, which goes through the model-matrix interpolation): same size (<= 256
//     bytes) and byte-identical guest bytes, and no GPU-written range overlaps them now (else the written copy binds);
//   support buffer: same viewport size; shader constants: always; driver constants: same storage ranges of the stage;
//   a binding with no source: always. Storage buffers are never reused.
// The reused info names an upload of the same bytes made earlier in the epoch: what the shader reads is identical.
// CATHERINE_PARALLEL87=0: off.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace nx::nvn::replay::memo87 {

extern std::atomic<uint64_t> g_epoch;  // defined and bumped by dedup_reset (nvn_dedup.cpp)
inline uint64_t g_hits = 0, g_misses = 0;

inline bool on() {
    static const bool v = [] {
        const char* e = std::getenv("CATHERINE_PARALLEL87");
        return !(e && e[0] == '0');
    }();
    return v;
}

struct Entry {
    const DescriptorBinding* b = nullptr;
    uint64_t epoch = 0;
    uint64_t size = 0;
    float vw = 0, vh = 0;
    Range storage[16];
    uint8_t bytes[256];
    VkDescriptorBufferInfo info{};
};
inline Entry g_table[4096];

inline Entry& slot(const DescriptorBinding* b) {
    const uint64_t h = reinterpret_cast<uintptr_t>(b) * 0x9e3779b97f4a7c15ull;
    return g_table[(h >> 40) & 4095];
}

// The guest bytes of a uniform binding as build_sets reads them (before any interpolation).
inline bool uniform_source(const ShaderInfo* s, const DescriptorBinding& b, int st, const uint8_t*& p, uint64_t& size) {
    if (b.nvn.bank == 3 && s->stage == ShaderStage::Vertex) return false;  // the model matrix: always computed
    const Range& r = b.nvn.index >= 0 && b.nvn.index < 18 ? g_state.uniforms[st][b.nvn.index] : Range{};
    p = r.address ? guest_bytes(r.address) : nullptr;
    size = p ? std::min<uint64_t>(r.size, 0x10000) : 0;
    return size <= sizeof(Entry::bytes) && (!size || !written_overlaps(p, size));
}

inline bool reusable_kind(NvnSource source) {
    return source != NvnSource::StorageBuffer;
}

inline bool lookup(const ShaderInfo* s, const DescriptorBinding& b, int st, float vw, float vh, VkDescriptorBufferInfo& info) {
    if (!on() || !reusable_kind(b.nvn.source)) return false;
    Entry& e = slot(&b);
    if (e.b != &b || e.epoch != g_epoch.load(std::memory_order_relaxed)) return false;
    switch (b.nvn.source) {
        case NvnSource::UniformBlock:
        case NvnSource::UniformAsStorage: {
            const uint8_t* p;
            uint64_t size;
            if (!uniform_source(s, b, st, p, size) || size != e.size) return false;
            if (size && std::memcmp(p, e.bytes, size) != 0) return false;
            break;
        }
        case NvnSource::SupportBuffer:
            if (std::memcmp(&vw, &e.vw, 4) != 0 || std::memcmp(&vh, &e.vh, 4) != 0) return false;
            break;
        case NvnSource::DriverConstants:
            if (std::memcmp(g_state.storage[st], e.storage, sizeof e.storage) != 0) return false;
            break;
        default:
            break;
    }
    info = e.info;
    ++g_hits;
    return true;
}

inline void store(const ShaderInfo* s, const DescriptorBinding& b, int st, float vw, float vh, const VkDescriptorBufferInfo& info) {
    if (!on() || !reusable_kind(b.nvn.source)) return;
    ++g_misses;
    Entry& e = slot(&b);
    e.b = nullptr;  // valid only once filled
    switch (b.nvn.source) {
        case NvnSource::UniformBlock:
        case NvnSource::UniformAsStorage: {
            const uint8_t* p;
            uint64_t size;
            if (!uniform_source(s, b, st, p, size)) return;
            e.size = size;
            if (size) std::memcpy(e.bytes, p, size);
            break;
        }
        case NvnSource::SupportBuffer:
            e.vw = vw, e.vh = vh;
            break;
        case NvnSource::DriverConstants:
            std::memcpy(e.storage, g_state.storage[st], sizeof e.storage);
            break;
        default:
            break;
    }
    e.info = info;
    e.epoch = g_epoch.load(std::memory_order_relaxed);
    e.b = &b;
}

}  // namespace nx::nvn::replay::memo87
