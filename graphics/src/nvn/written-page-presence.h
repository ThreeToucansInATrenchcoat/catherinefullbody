#pragma once

#include <array>
#include <cstdint>

namespace nx::nvn::replay {

// A zero bit proves no live written region touches that page. Set bits may
// collide or describe erased regions, so possible hits still use the tree.
class WrittenPagePresence {
    static constexpr unsigned kPageShift = 14;
    static constexpr unsigned kSlots = 1u << 14;
    static constexpr uint64_t kMarkLimit = 1024;
    static constexpr uint64_t kQueryLimit = 8;

    std::array<uint64_t, kSlots / 64> pages{};
    bool saturated = false;

    static unsigned slot(uint64_t page) {
        return unsigned((page ^ (page >> 13) ^ (page >> 26)) & (kSlots - 1));
    }

public:
    void clear() {
        pages.fill(0);
        saturated = false;
    }

    void mark(uint64_t address, uint64_t size) {
        if (saturated) return;
        if (!size || size > UINT64_MAX - address) {
            saturated = true;
            return;
        }
        const uint64_t first = address >> kPageShift;
        const uint64_t last = (address + size - 1) >> kPageShift;
        if (last - first >= kMarkLimit) {
            saturated = true;
            return;
        }
        for (uint64_t page = first; page <= last; ++page) {
            const unsigned index = slot(page);
            pages[index >> 6] |= uint64_t{1} << (index & 63);
        }
    }

    bool may_overlap(uint64_t address, uint64_t size) const {
        if (saturated || !size || size > UINT64_MAX - address) return true;
        const uint64_t first = address >> kPageShift;
        const uint64_t last = (address + size - 1) >> kPageShift;
        if (last - first >= kQueryLimit) return true;
        for (uint64_t page = first; page <= last; ++page) {
            const unsigned index = slot(page);
            if ((pages[index >> 6] >> (index & 63)) & 1) return true;
        }
        return false;
    }
};

}  // namespace nx::nvn::replay
