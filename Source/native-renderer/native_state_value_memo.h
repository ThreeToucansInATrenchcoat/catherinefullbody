#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include "C:/catherine/native-gpu-experiment/Source/native_byte_equal.h"
namespace experimental_record {
inline constexpr uint64_t fixed_value_oracle_marker=0x4e41544649585632ull;
class FixedValueMemo {
    struct Entry {std::array<uint8_t,48> bytes{};uint64_t stamp=1;uint32_t mxcsr=0;bool valid=false,frozen=false;};
    std::array<Entry,18> entries;
public:
    static int key(uint32_t kind,uint32_t first){
        if(kind>=9)return -1;
        if(kind==0)return first<8?(first?int(10+first):0):-1;
        if(kind==7){const auto mask=first&3;return mask==1?7:mask==2?9:mask==3?10:-1;}
        return int(kind);
    }
    bool matches(int key,const void* bytes,uint32_t now=_mm_getcsr())const {
        if(key<0||key>=int(entries.size())||!bytes)return false;const auto& e=entries[key];
        // Repeated lowering may touch FP status. Reuse only under identical
        // controls, masked exceptions, and with every learned sticky flag present.
        return e.valid&&!e.frozen&&(now&0x1f80u)==0x1f80u&&((now^e.mxcsr)&0xffc0u)==0&&!(e.mxcsr&0x3fu&~now)&&experimental_bytes::native(e.bytes.data(),bytes,48);
    }
    uint64_t stamp(int key)const{return key>=0&&key<int(entries.size())?entries[key].stamp:0;}
    bool valid_at(int key,uint64_t stamp)const{return key>=0&&key<int(entries.size())&&entries[key].valid&&!entries[key].frozen&&entries[key].stamp==stamp;}
    void invalidate(int key){if(key<0||key>=int(entries.size()))return;auto& e=entries[key];e.valid=false;if(e.stamp==UINT64_MAX)e.frozen=true;else ++e.stamp;}
    void reset(){for(int key=0;key<int(entries.size());++key)invalidate(key);}
    void blend_changed(int target){if(target>=0&&target<8)invalidate(target?10+target:0);else {invalidate(0);for(int n=11;n<18;++n)invalidate(n);}}
    void ref_changed(uint32_t faces){if(faces&1){invalidate(7);invalidate(10);}if(faces&2){invalidate(9);invalidate(10);}}
    bool learn(int key,uint64_t expected,const void* bytes,uint32_t after=_mm_getcsr()){if(key<0||key>=int(entries.size())||!bytes)return false;auto& e=entries[key];if(e.frozen||e.stamp!=expected)return false;if(e.stamp==UINT64_MAX){e.frozen=true;e.valid=false;return false;}std::memcpy(e.bytes.data(),bytes,48);++e.stamp;e.mxcsr=after;e.valid=true;return true;}
};
}
