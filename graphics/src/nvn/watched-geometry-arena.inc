namespace {

using namespace nx::vk;

constexpr uint64_t kArenaSize = 64ull << 20;
constexpr uint64_t kNoOffset = UINT64_MAX;
constexpr uint64_t kMinBytes = 16ull << 10;
constexpr uint64_t kMaxBytes = 1ull << 20;
constexpr size_t kMaxEntries = 32768;
constexpr size_t kPruneAt = kMaxEntries - 512;
constexpr uint64_t kPruneInterval = 120;
constexpr uint64_t kInactiveGenerations = 240;
constexpr uint64_t kAlignment = 256;
constexpr uint64_t kHeapBase = CATHERINE_HEAP_WATCH_BASE;
constexpr uint64_t kHeapSize = CATHERINE_HEAP_WATCH_SIZE;
constexpr uint64_t kPage = 4096;

struct GeometryKey {
    const uint8_t* p;
    uint64_t size, padded;
    bool operator==(const GeometryKey&) const = default;
};

struct GeometryHash {
    size_t operator()(const GeometryKey& key) const {
        uint64_t v = uint64_t(reinterpret_cast<uintptr_t>(key.p)) ^ (key.size << 19) ^ (key.padded << 37);
        v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ull;
        return size_t(v ^ (v >> 27));
    }
};

struct PageSpan {
    uint64_t first = 0, bytes = 0;
};

PageSpan watched_span(const uint8_t* p, uint64_t size) {
    const uint64_t a = uintptr_t(p);
    if (!size || a < kHeapBase || a - kHeapBase >= kHeapSize || size > kHeapSize - (a - kHeapBase)) return {};
    const uint64_t first = a & ~(kPage - 1);
    const uint64_t end = (a + size + kPage - 1) & ~(kPage - 1);
    return {first, end - first};
}

struct HeapPin {
    uint64_t allocation = 0;
    bool held;
    explicit HeapPin(PageSpan s)
        : held(catherine_heap_watch_begin(s.first, s.bytes, &allocation) != 0) {}
    ~HeapPin() { if (held) catherine_heap_watch_end(); }
    HeapPin(const HeapPin&) = delete;
    HeapPin& operator=(const HeapPin&) = delete;
};

struct GeometryEntry {
    uint64_t arena_epoch = 0;
    uint64_t generation = 0, page_epoch = 0, allocation = 0, offset = kNoOffset;
    uint64_t last_use = 0;
    unsigned stable = 0;

    void clear_candidate() {
        generation = page_epoch = allocation = 0;
        offset = kNoOffset;
        stable = 0;
    }
};

struct GeometryStats {
    uint64_t attempted[2]{}, bytes[2]{}, hit[2]{}, hit_bytes[2]{};
    uint64_t queries = 0, query_ns = 0, dirty_pages = 0, software_pages = 0;
    uint64_t untracked = 0, failed = 0, invalidated = 0, new_bytes = 0, captured_dirty = 0, verified = 0;
    uint64_t prune_passes = 0, pruned_entries = 0;
};

unsigned watched_geometry_mode() {
    static const unsigned mode = [] {
        if (!catherine_heap_watch_active()) return 0u;
        const char* value = std::getenv("NX_WATCHED_GEOMETRY");
        if (!value || (value[0] != '1' && value[0] != '2') || value[1] != 0) return 0u;
        return unsigned(value[0] - '0');
    }();
    return mode;
}

bool watched_geometry_verify() {
    static const bool verify = [] {
        const char* value = std::getenv("NX_WATCHED_GEOMETRY_VERIFY");
        return value && value[0] == '1' && value[1] == 0;
    }();
    return verify;
}

bool watched_geometry_timing() {
    static const bool timing = [] {
        const char* value = std::getenv("NX_WATCHED_GEOMETRY_TIMING");
        return value && value[0] == '1' && value[1] == 0;
    }();
    return timing;
}

bool watched_geometry_recycle() {
    static const bool requested = [] {
        const char* value = std::getenv("NX_WATCHED_GEOMETRY_RECYCLE");
        const char* chunks = std::getenv("NX_SUBMIT_CHUNK_DRAWS");
        return value && value[0] == '1' && value[1] == 0 &&
               (!chunks || (chunks[0] == '0' && chunks[1] == 0));
    }();
    return requested && async_gpu() && !pipelined();
}

// The Vulkan device owns the arena. A reset waits for every published offset to retire.
struct GeometryArena {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint8_t* mapped = nullptr;
    uint8_t* shadow = nullptr;
    uint64_t head = 0, epoch = 0, unreliable_epoch = 0, last_prune = 0;
    uint64_t arena_epoch = 1, drain_serial = 0, rotations = 0, drain_fallbacks = 0;
    bool attempted = false, disabled = false;
    std::unique_ptr<uint64_t[]> page_epochs;
    std::array<void*, kMaxBytes / kPage + 1> returned{};
    std::unordered_map<GeometryKey, GeometryEntry, GeometryHash> entries;
    GeometryStats stats;

#include "watched-geometry-allocation.inc"

    void capacity_miss() {
        if (drain_serial || !watched_geometry_recycle()) return;
        // lookup runs under the renderer's GPU lock while a submit is recorded.
        // Include that pending submit: same-submit dedup entries may still hand
        // out old resident uploads after this call. No new lookup may do so.
        const uint64_t serial = gpu_serial();
        if (serial != UINT64_MAX) drain_serial = serial + 1;
    }

    bool retirement_pending() {
        if (!drain_serial) return false;
        if (frame_completed() < drain_serial) {
            ++drain_fallbacks;
            return true;
        }
        // All submissions that could reference this arena have retired. Later
        // submits used ordinary ring uploads. Keep the allocation and OS watch
        // history; entry epochs invalidate old offsets without a mass erase.
        if (arena_epoch == UINT64_MAX) { disabled = true; return true; }
        ++arena_epoch;
        ++rotations;
        drain_serial = 0;
        head = 0;
        return false;
    }

    bool query(PageSpan span, uint64_t& stamp) {
        if (!page_epochs) {
            page_epochs.reset(new (std::nothrow) uint64_t[kHeapSize / kPage]());
            if (!page_epochs) {
                disabled = true;
                return false;
            }
        }
        const uint64_t pages = span.bytes / kPage;
        if (!pages || pages > returned.size()) return false;
        ULONG_PTR written = static_cast<ULONG_PTR>(pages);
        DWORD granularity = 0;
        const bool timing = watched_geometry_timing();
        const auto start = timing ? plat::monotonic_ns() : 0;
        const UINT status = GetWriteWatch(WRITE_WATCH_FLAG_RESET, reinterpret_cast<void*>(span.first), span.bytes,
                                          returned.data(), &written, &granularity);
        if (timing) stats.query_ns += plat::monotonic_ns() - start;
        ++stats.queries;
        stamp = ++epoch;
        // A failed/partial reset may have consumed notifications for overlapping ranges.
        if (status || granularity != kPage || written > pages) {
            unreliable_epoch = stamp;
            ++stats.failed;
            return false;
        }
        for (ULONG_PTR i = 0; i < written; ++i) {
            const uint64_t a = uintptr_t(returned[i]);
            if ((a & (kPage - 1)) || a < span.first || a - span.first >= span.bytes) {
                unreliable_epoch = stamp;
                ++stats.failed;
                return false;
            }
            page_epochs[(a - kHeapBase) / kPage] = stamp;
        }
        stats.dirty_pages += written;
        return true;
    }

    bool clean(PageSpan span, uint64_t since) const {
        if (!since || since < unreliable_epoch) return false;
        const auto* first = page_epochs.get() + (span.first - kHeapBase) / kPage;
        return std::none_of(first, first + span.bytes / kPage, [since](uint64_t p) { return p > since; });
    }

    void invalidate(const uint8_t* p, uint64_t size) {
        if (!page_epochs || !size || disabled) return;
        const uint64_t a = uintptr_t(p);
        if (a >= kHeapBase + kHeapSize || (a < kHeapBase && size <= kHeapBase - a)) return;
        const uint64_t lo = std::max(a, kHeapBase);
        const uint64_t hi = size > kHeapBase + kHeapSize - a ? kHeapBase + kHeapSize : a + size;
        const auto span = watched_span(reinterpret_cast<const uint8_t*>(lo), hi - lo);
        if (!span.bytes) return;
        const auto stamp = ++epoch;
        const auto pages = span.bytes / kPage;
        stats.software_pages += pages;
        if (span.bytes > kMaxBytes) {
            unreliable_epoch = stamp;
        } else {
            std::fill_n(page_epochs.get() + (span.first - kHeapBase) / kPage, pages, stamp);
        }
    }

    void prune_candidates(uint64_t generation) {
        if (generation < last_prune || generation - last_prune < kPruneInterval) return;
        last_prune = generation;
        ++stats.prune_passes;
        for (auto it = entries.begin(); it != entries.end();) {
            const auto& entry = it->second;
            // Erase lookup metadata only. A published offset in the current arena epoch
            // stays immutable until the retirement boundary.
            if ((entry.offset == kNoOffset || entry.arena_epoch != arena_epoch) && generation >= entry.last_use &&
                generation - entry.last_use >= kInactiveGenerations) {
                it = entries.erase(it);
                ++stats.pruned_entries;
            } else {
                ++it;
            }
        }
    }

    Upload lookup(const GeometryKey& key, uint64_t generation, bool index) {
        if (disabled || key.size < kMinBytes || key.size > key.padded || key.padded > kMaxBytes) return {};
        const unsigned group = index ? 0 : 1;
        ++stats.attempted[group];
        stats.bytes[group] += key.size;
        if (retirement_pending()) return {};
        const auto span = watched_span(key.p, key.size);
        if (!span.bytes) {
            ++stats.untracked;
            return {};
        }
        auto it = entries.find(key);
        if (it == entries.end()) {
            if (head + key.padded > kArenaSize) { capacity_miss(); return {}; }
            if (entries.size() >= kPruneAt) prune_candidates(generation);
            if (entries.size() >= kMaxEntries) return {};
            it = entries.try_emplace(key).first;
        }
        auto& entry = it->second;
        if (entry.arena_epoch != arena_epoch) {
            entry.clear_candidate();
            entry.arena_epoch = arena_epoch;
        }
        entry.last_use = generation;
        // Per-submit invalidation can revisit a range. Software stamps preserve the
        // invalidation for its next generation; this use keeps the normal copy.
        if (entry.generation == generation) return {};
        HeapPin pin(span);
        if (!pin.held) {
            if (entry.offset != kNoOffset) ++stats.invalidated;
            entry.clear_candidate();
            ++stats.untracked;
            return {};
        }
        uint64_t before = 0;
        if (!query(span, before)) {
            if (entry.offset != kNoOffset) ++stats.invalidated;
            entry.clear_candidate();
            return {};
        }
        const bool unchanged = pin.allocation == entry.allocation && clean(span, entry.page_epoch);
        if (entry.offset != kNoOffset) {
            if (unchanged) {
                if (watched_geometry_verify()) {
                    ++stats.verified;
                    if (std::memcmp(key.p, shadow + entry.offset, key.size) != 0) {
                        log("nvn: watched geometry exact verification mismatch, disabling residency (%llu bytes)",
                            (unsigned long long)key.size);
                        entry.clear_candidate();
                        disabled = true;
                        return {};
                    }
                }
                entry.generation = generation;
                entry.page_epoch = before;
                ++stats.hit[group];
                stats.hit_bytes[group] += key.size;
                return {buffer, entry.offset, mapped + entry.offset};
            }
            ++stats.invalidated;
            entry.clear_candidate();
        }
        entry.stable = unchanged && entry.generation + 1 == generation ? entry.stable + 1 : 1;
        entry.generation = generation;
        entry.page_epoch = before;
        entry.allocation = pin.allocation;
        if (entry.stable < 3) return {};

        const uint64_t offset = (head + kAlignment - 1) & ~(kAlignment - 1);
        if (offset + key.padded > kArenaSize) { capacity_miss(); return {}; }
        if (!create()) {
            disabled = true;
            return {};
        }
        std::memcpy(shadow + offset, key.p, key.size);
        uint64_t after = 0;
        if (!query(span, after) || !clean(span, before)) {
            ++stats.captured_dirty;
            entry.clear_candidate();
            return {};
        }

        // Pin stays held through capture. The slot is unpublished until the
        // full copy and the post-capture watch query succeed. It is never reused.
        std::memcpy(mapped + offset, shadow + offset, key.size);
        if (key.padded > key.size) std::memset(mapped + offset + key.size, 0, key.padded - key.size);
        _mm_sfence();
        entry.offset = offset;
        entry.page_epoch = before;
        head = offset + key.padded;
        stats.new_bytes += key.padded;
        return {buffer, offset, mapped + offset};
    }

    void print() {
        if (stats.attempted[0] || stats.attempted[1]) {
            log("nvn watched geometry: index=%llu/%llu bytes=%llu/%llu vertex=%llu/%llu bytes=%llu/%llu "
                "queries=%llu query_us=%.2f dirty=%llu software=%llu untracked=%llu failed=%llu "
                "invalidated=%llu admitted_B=%llu capture_dirty=%llu verified=%llu retained_B=%llu entries=%zu "
                "prune_passes=%llu pruned_entries=%llu arena_epoch=%llu rotations=%llu drain_serial=%llu drain_fallbacks=%llu",
                (unsigned long long)stats.hit[0], (unsigned long long)stats.attempted[0],
                (unsigned long long)stats.hit_bytes[0], (unsigned long long)stats.bytes[0],
                (unsigned long long)stats.hit[1], (unsigned long long)stats.attempted[1],
                (unsigned long long)stats.hit_bytes[1], (unsigned long long)stats.bytes[1],
                (unsigned long long)stats.queries, double(stats.query_ns) / 1000.0,
                (unsigned long long)stats.dirty_pages, (unsigned long long)stats.software_pages,
                (unsigned long long)stats.untracked, (unsigned long long)stats.failed,
                (unsigned long long)stats.invalidated, (unsigned long long)stats.new_bytes,
                (unsigned long long)stats.captured_dirty, (unsigned long long)stats.verified,
                (unsigned long long)head, entries.size(), (unsigned long long)stats.prune_passes,
                (unsigned long long)stats.pruned_entries, (unsigned long long)arena_epoch,
                (unsigned long long)rotations, (unsigned long long)drain_serial, (unsigned long long)drain_fallbacks);
        }
        stats = {};
    }
};

GeometryArena g_geometry;

}
