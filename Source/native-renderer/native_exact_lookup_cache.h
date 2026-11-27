#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace experimental_root9 {
// Direct-mapped exact-key cache. Collisions only evict a lookup. The owner must
// invalidate an erased key BEFORE destroying its iterator and clear on clear.
// Copy/move deliberately discard iterators: the enclosing map may be copied.
template<class Iterator,size_t Slots=2048>class ExactLookupCache {
    static_assert(Slots && !(Slots&(Slots-1)));
    struct Entry{uint64_t key=0,stamp=0;Iterator value{};};
    std::array<Entry,Slots> entries{};
    uint64_t stamp=1;
    static size_t slot(uint64_t key){const uint64_t bucket=key>>6;return size_t((bucket^(bucket>>17)^(bucket>>33))&(Slots-1));}
public:
    ExactLookupCache()=default;
    ExactLookupCache(const ExactLookupCache&){}
    ExactLookupCache(ExactLookupCache&& other)noexcept{other.clear();}
    ExactLookupCache& operator=(const ExactLookupCache&){clear();return *this;}
    ExactLookupCache& operator=(ExactLookupCache&& other)noexcept{clear();other.clear();return *this;}
    bool find(uint64_t key,Iterator& value)const{const auto&e=entries[slot(key)];if(e.stamp!=stamp||e.key!=key)return false;value=e.value;return true;}
    void remember(uint64_t key,Iterator value){entries[slot(key)]={key,stamp,value};}
    void erase(uint64_t key){auto&e=entries[slot(key)];if(e.stamp==stamp&&e.key==key)e.stamp=0;}
    void clear(){if(!++stamp){for(auto&e:entries)e.stamp=0;stamp=1;}}
};
}
