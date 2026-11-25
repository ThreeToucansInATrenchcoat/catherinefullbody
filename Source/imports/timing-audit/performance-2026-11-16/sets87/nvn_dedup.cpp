#include "shared_texture_watch.h"
// Per-submit upload reuse (docs/nvn/DEBUG.md #30). A draw's buffers are copied from guest memory into the upload
// ring when it is replayed; the same camera block, scene constants and vertex buffers were copied again for every
// draw that bound them (~130 KB per draw, 50-70 MB per frame). Within one submit guest memory only changes through
// inline uniform updates (UpdateUniformBuffer, which invalidates what it overlaps) and compute results (ranges a
// dispatch writes are read from their written copy, nvn_written.cpp, and invalidated here): a copy made once can
// serve every later draw of the submit that binds the same bytes.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>

#include "nvn_draw_state.h"
#include "latest-flat.h"
#include "watched-geometry.h"
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
#include <algorithm>
#include <immintrin.h>
#include <memory>
#include <new>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif

#include "catherine_heap_watch.h"
#include "platform.h"
#endif

namespace nx::nvn::replay {

#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
#include "watched-geometry-arena.inc"
#endif

namespace {

struct Entry {
    uint64_t size = 0, padded = 0;
    Upload upload;
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    bool geometry_resident = false;
#endif
};
// By host address; several sizes of one address are rare, so the latest one is kept.
std::map<const uint8_t*, Entry> g_copies;
uint64_t g_saved = 0, g_copied = 0;

// In front of the map (a tree walk per bound buffer of every draw, and most are hits): a slot per address hash with a
// copy of that address's entry in g_copies. A slot of the current generation always equals the map's entry: both are
// written together, dedup_invalidate clears the slot of each entry it erases, dedup_reset starts a new generation.
struct Front {
    const uint8_t* p = nullptr;
    uint64_t generation = 0;
    Entry entry;
};
Front g_front[1024];
uint64_t g_generation = 1;
Front& front_of(const uint8_t* p) { return g_front[(uintptr_t(p) >> 4 ^ uintptr_t(p) >> 14) & 1023]; }

latest_flat::Index<Upload> g_flat;

bool dedup_off() {
    static const bool off = std::getenv("NX_NO_DEDUP") != nullptr;
    return off;
}

bool flat_requested() {
    static const bool requested = [] {
        const char* value = std::getenv("NX_DEDUP_FLAT");
        const bool enabled = value && value[0] == '1' && !value[1] && !dedup_off();
        if (enabled) log("nvn: latest-only flat upload cache enabled");
        return enabled;
    }();
    return requested;
}

}  // namespace

#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
bool texture_write_watch(const uint8_t* source,uint64_t size,uint64_t& since,uint64_t& allocation,bool& changed) {
    if (!watched_geometry_mode() || g_geometry.disabled) return false;
    const auto span=watched_span(source,size);
    if (!span.bytes) return false;
    HeapPin pin(span);
    if (!pin.held) return false;
    uint64_t stamp=0;
    for(uint64_t at=0;at<span.bytes;) {
        const uint64_t bytes=std::min<uint64_t>(span.bytes-at,kMaxBytes);
        if(!g_geometry.query({span.first+at,bytes},stamp)) return false;
        at+=bytes;
    }
    changed=!since || allocation!=pin.allocation || !g_geometry.clean(span,since);
    since=stamp;allocation=pin.allocation;
    return true;
}

enum class UploadKind { General, RawIndex, RawVertex };

static Upload dedup_upload_impl(const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align, UploadKind kind) {
#else
bool texture_write_watch(const uint8_t*,uint64_t,uint64_t&,uint64_t&,bool&) { return false; }

Upload dedup_upload(const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align) {
#endif
    if (!p || !size) {
        Upload u = upload_alloc(padded, align);
        std::memset(u.ptr, 0, padded);
        return u;
    }
    const bool off = dedup_off();
    const bool flat = flat_requested();
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    const bool allow_resident = kind != UploadKind::General && !g_geometry.disabled;
#else
    const bool allow_resident = false;
#endif
    latest_flat::Index<Upload>::Probe probe;
    if (flat && g_flat.available()) {
        probe = g_flat.find(p);
        if (const auto* upload = g_flat.hit(probe, size, padded, align, allow_resident)) {
            g_saved += padded;
            return *upload;
        }
    }
    if (!flat || !g_flat.available()) {
        Front& f = front_of(p);
        const Entry* e = nullptr;
        if (!off && f.generation == g_generation && f.p == p) {
            e = &f.entry;
        } else if (auto it = off ? g_copies.end() : g_copies.find(p); it != g_copies.end()) {
            f = {p, g_generation, it->second};
            e = &f.entry;
        }
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
        if (e && e->geometry_resident && !allow_resident) e = nullptr;
#endif
        if (e && e->size == size && e->padded == padded && e->upload.offset % align == 0) {
            g_saved += padded;
            return e->upload;
        }
    }
    auto store = [&](const Upload& upload, bool resident) {
        if (flat && g_flat.store(probe, p, size, padded, upload, resident)) return;
        Entry entry{size, padded, upload};
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
        entry.geometry_resident = resident;
#endif
        // A saturated flat cache drops its old entries. The map starts empty for this generation.
        g_copies[p] = entry;
        front_of(p) = {p, g_generation, entry};
    };
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    if (!off && kind != UploadKind::General) {
        const Upload resident = g_geometry.lookup({p, size, padded}, g_generation, kind == UploadKind::RawIndex);
        if (resident.buffer) {
            store(resident, true);
            return resident;
        }
    }
#endif
    Upload u = upload_alloc(padded, align);
    std::memcpy(u.ptr, p, size);
    if (padded > size) std::memset(u.ptr + size, 0, padded - size);
    store(u, false);
    g_copied += padded;
    return u;
}

#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
Upload dedup_upload(const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align) {
    return dedup_upload_impl(p, size, padded, align, UploadKind::General);
}

bool watched_geometry_enabled(bool index) {
    return !g_geometry.disabled && watched_geometry_mode() >= (index ? 1u : 2u) && !written_separate();
}

Upload watched_geometry_upload(const uint8_t* p, uint64_t size, uint64_t padded, bool index) {
    return dedup_upload_impl(p, size, padded, 16, index ? UploadKind::RawIndex : UploadKind::RawVertex);
}
#endif

namespace memo87 { std::atomic<uint64_t> g_epoch{1}; }
void dedup_reset() {
    memo87::g_epoch.fetch_add(1, std::memory_order_relaxed);
    g_copies.clear();
    ++g_generation;
    if (flat_requested()) g_flat.reset();
}

void dedup_invalidate(const uint8_t* p, uint64_t size) {
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    if (g_geometry.page_epochs) g_geometry.invalidate(p, size);
#endif
    if (flat_requested() && g_flat.invalidate(p, size)) return;
    // Entries starting before p may still reach into it: look back over the few that can.
    auto it = g_copies.lower_bound(p + size);
    while (it != g_copies.begin()) {
        --it;
        if (it->first + it->second.size <= p) {
            if (p - it->first > (64ll << 20)) break;  // nothing larger than 64 MB is copied
            continue;
        }
        if (Front& f = front_of(it->first); f.p == it->first) f.generation = 0;
        it = g_copies.erase(it);
    }
}

// c[0], the NVN driver's constants, as far as shaders read them (docs/nvn/TRANSLATOR.md): buffer descriptors
// {u64 GPU address; u32 size} of what is bound, which compute shaders search to find the buffer an address falls in
// (they wrote garbage with a zero table), and the maximum point size.
std::array<uint8_t, 0x1000> driver_constants(ShaderStage stage, int st) {
    std::array<uint8_t, 0x1000> c0{};
    auto put = [&](uint32_t at, const Range& r) {
        if (at + 12 > c0.size()) return;
        std::memcpy(c0.data() + at, &r.address, 8);
        uint32_t size = uint32_t(std::min<uint64_t>(r.size, 0xFFFFFFFFu));
        std::memcpy(c0.data() + at + 8, &size, 4);
    };
    if (stage == ShaderStage::Compute) {
        for (int n = 0; n < 16; n++) put(0x210 + 0x10 * n, g_state.uniforms[st][n]);
        for (int n = 0; n < 16; n++) put(0x310 + 0x10 * n, g_state.storage[st][n]);
    } else {
        int hw = stage == ShaderStage::Fragment ? 4 : 0;  // Maxwell stage numbers: vertex 0, fragment 4
        for (int n = 0; n < 16; n++) put(0x110 + 0x100 * hw + 0x10 * n, g_state.storage[st][n]);
        float max_point = 64.0f;
        std::memcpy(c0.data() + 0x610, &max_point, 4);
    }
    return c0;
}

void dedup_log() {
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    if (watched_geometry_mode()) g_geometry.print();
#endif
    if (g_copied + g_saved)
        log("nvn: uploads copied %.1f MB, reused %.1f MB", double(g_copied) / 1e6, double(g_saved) / 1e6);
    g_copied = g_saved = 0;
}

}  // namespace nx::nvn::replay
