#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace experimental_root9 {
// Exact reference counts over folded 64-byte address buckets. Collisions can
// only request an unnecessary exact lookup; a zero proves no installed tag.
class TagOverlapFilter {
    std::array<uint16_t,65536> counts{};
    bool reliable=true;
    static size_t slot(uint64_t bucket){return size_t((bucket^(bucket>>16)^(bucket>>32)^(bucket>>48))&65535);}
    template<class F>static void buckets(uint64_t address,F f){
        const uint64_t first=address>>6;
        const uint64_t last=(address>UINT64_MAX-63?UINT64_MAX:address+63)>>6;
        f(slot(first));if(last!=first)f(slot(last));
    }
public:
    void clear(){counts.fill(0);reliable=true;}
    void insert(uint64_t address){if(!reliable)return;buckets(address,[&](size_t n){if(counts[n]==UINT16_MAX)reliable=false;else ++counts[n];});}
    void erase(uint64_t address){if(!reliable)return;buckets(address,[&](size_t n){if(!counts[n])reliable=false;else --counts[n];});}
    bool absent(uint64_t address)const {if(!reliable)return false;bool empty=true;buckets(address,[&](size_t n){empty&=counts[n]==0;});return empty;}
};
}
