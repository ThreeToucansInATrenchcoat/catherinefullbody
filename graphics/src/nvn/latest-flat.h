#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace nx::nvn::replay::latest_flat {

// A latest-only address index for one submit. A stale key stays in its probe chain
// until the next reset. A generation miss never examines the old Upload value.
template<class Upload>
class Index {
    static constexpr uint32_t kCapacity = 8192;
    static constexpr uint32_t kMaxEntries = kCapacity * 3 / 4;
    static constexpr uint32_t kMaxProbe = 64;
    static constexpr uint32_t kMaxMutations = 8192;

    struct Range {
        uint64_t lo, hi;
    };

    struct alignas(64) Slot {
        const uint8_t* p = nullptr;
        uint64_t size = 0, padded = 0;
        Upload upload{};
        uint64_t generation = 0;
        uint32_t checked = 0;
        bool live = false, geometry_resident = false;
    };
    static_assert(sizeof(Slot) == 64, "Expected one cache line per upload record");

    std::array<Slot, kCapacity> slots{};
    std::array<Range, kMaxMutations> mutations{};
    uint64_t generation = 1;
    uint32_t entries = 0, mutation_count = 0;
    bool enabled = true;

    static Range range_of(const uint8_t* p, uint64_t size) {
        const uint64_t lo = reinterpret_cast<uintptr_t>(p);
        return {lo, lo + std::min(size, UINT64_MAX - lo)};
    }

public:
    struct Probe {
        Slot* slot = nullptr;
        uint64_t generation = 0;
    };

    bool available() const { return enabled; }

    void reset() {
        if (++generation == 0) {
            slots.fill({});
            generation = 1;
        }
        entries = mutation_count = 0;
        enabled = true;
    }

    Probe find(const uint8_t* p) {
        if (!enabled) return {};
        const uint64_t address = reinterpret_cast<uintptr_t>(p);
        uint32_t position = ((address >> 4) ^ (address >> 17) ^ (address >> 31)) & (kCapacity - 1);
        for (uint32_t step = 0; step < kMaxProbe; ++step) {
            auto& slot = slots[position];
            if (slot.generation != generation || slot.p == p) return {&slot, generation};
            position = (position + 1) & (kCapacity - 1);
        }
        enabled = false;
        return {};
    }

    const Upload* hit(Probe probe, uint64_t size, uint64_t padded, uint64_t align,
                      bool allow_geometry_resident = false) {
        if (!enabled || !probe.slot || probe.generation != generation) return nullptr;
        auto& slot = *probe.slot;
        if (slot.generation != generation || !slot.live || slot.size != size || slot.padded != padded ||
            (slot.geometry_resident && !allow_geometry_resident) || !align || slot.upload.offset % align != 0)
            return nullptr;
        if (slot.checked != mutation_count) {
            const Range copied = range_of(slot.p, size);
            for (uint32_t i = slot.checked; i < mutation_count; ++i) {
                const auto& mutation = mutations[i];
                if (copied.lo < mutation.hi && mutation.lo < copied.hi) {
                    slot.live = false;
                    return nullptr;
                }
            }
            slot.checked = mutation_count;
        }
        return &slot.upload;
    }

    bool store(Probe probe, const uint8_t* p, uint64_t size, uint64_t padded, const Upload& upload,
               bool geometry_resident = false) {
        if (!enabled) return false;
        // upload_alloc may have reset this submit's ring cache while finding space.
        if (!probe.slot || probe.generation != generation ||
            (probe.slot->generation == generation && probe.slot->p != p)) probe = find(p);
        if (!probe.slot) return false;
        auto& slot = *probe.slot;
        if (slot.generation != generation) {
            if (entries >= kMaxEntries) {
                enabled = false;
                return false;
            }
            ++entries;
        }
        slot = {p, size, padded, upload, generation, mutation_count, true, geometry_resident};
        return true;
    }

    bool invalidate(const uint8_t* p, uint64_t size) {
        if (!enabled) return false;
        if (!entries) return true;
        if (mutation_count == kMaxMutations) {
            enabled = false;
            return false;
        }
        // Retain zero-length mutations: stock invalidates a point strictly inside a copy.
        mutations[mutation_count++] = range_of(p, size);
        return true;
    }
};

}  // namespace nx::nvn::replay::latest_flat

