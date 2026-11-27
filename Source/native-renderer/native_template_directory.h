#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace experimental_templates {
// A compact mirror of the two fields used by the original four-way search.
// Keys, slot order and validity have exactly their original meanings. This
// directory neither authenticates a template nor changes its lifetime checks.
class Directory {
    struct Entry {uint64_t key=0;bool valid=false;};
    std::array<Entry,8192> entries{};
public:
    void set(size_t slot,uint64_t key,bool valid){entries[slot]={key,valid};}
    size_t find(uint64_t key,bool require_valid)const {
        const size_t base=size_t(key>>51);
        for(size_t k=0;k<4;++k){const auto& e=entries[base^k];if((!require_valid||e.valid)&&e.key==key)return base^k;}
        return SIZE_MAX;
    }
};
}
