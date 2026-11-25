#pragma once
#include <cstdint>
#include <cstring>

namespace catherine::native_view {
enum Kind : uint32_t { Levels, Layers, Swizzle, Format, Count };
inline constexpr uint64_t addresses[Count]={0x7005427ca0ull,0x7005427cc0ull,0x7005427d00ull,0x7005427ce0ull};
template<class T> inline T load(uint64_t address){T value;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof value);return value;}
inline void store(uint64_t address,uint32_t value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof value);}
// Exact ordered SDK getter operations. Outputs may overlap the view itself, so
// flags and subsequent components are read only after preceding writes.
template<class Guest> inline void execute(Guest& c,uint32_t kind){
    const uint64_t view=c.x[0];
    if(kind==Levels){store(c.x[1],load<uint8_t>(view+1));store(c.x[2],load<uint8_t>(view+2));}
    else if(kind==Layers){store(c.x[1],load<uint16_t>(view+4));store(c.x[2],load<uint16_t>(view+6));}
    else if(kind==Swizzle){
        store(c.x[1],load<uint32_t>(view+12));store(c.x[2],load<uint32_t>(view+16));
        store(c.x[3],load<uint32_t>(view+20));store(c.x[4],load<uint32_t>(view+24));
    }else store(c.x[1],load<uint32_t>(view+8));
    const uint32_t flags=load<uint8_t>(view);
    c.x[8]=flags;c.x[0]=(flags>>(kind==Swizzle?3:kind==Format?2:kind))&1u;
}
inline bool overlaps(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<=b?b-a<n:a-b<m;}
}
