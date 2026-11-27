#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace experimental_root9 {
// Monotonic conservative set of exact addresses ever installed in copies.
// Bits are never cleared, including transport.clear/retire; saturation only
// produces more fallbacks. A completed publication reaches a later load through
// the recording job's existing same-thread/release-acquire handoff. A first
// concurrent publication may linearize after a negative probe.
class alignas(64) SourcePresence {
    static constexpr uint64_t words=8192,mask=words*64-1;
    std::array<std::atomic<uint64_t>,words> bits{};
    static uint64_t hash(uint64_t a){a^=a>>30;a*=0xbf58476d1ce4e5b9ull;a^=a>>27;a*=0x94d049bb133111ebull;return a^(a>>31);}
    void mark_bit(uint64_t bit){auto&word=bits[bit>>6];const uint64_t flag=uint64_t(1)<<(bit&63);if(!(word.load(std::memory_order_relaxed)&flag))word.fetch_or(flag,std::memory_order_seq_cst);}
public:
    void publish(uint64_t address){const auto h=hash(address);mark_bit(h&mask);mark_bit((h>>23)&mask);}
    bool may_have(uint64_t address)const{const auto h=hash(address);for(uint64_t bit:{h&mask,(h>>23)&mask})if(!(bits[bit>>6].load(std::memory_order_seq_cst)&(uint64_t(1)<<(bit&63))))return false;return true;}
};
}
