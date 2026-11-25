#pragma once
#include <cstdint>
#include <cstring>
namespace catherine::native_window {
inline constexpr uint64_t addresses[]={0x7005429940ull,0x7005429950ull,0x7005429970ull,0x7005429980ull};
template<class T>T load(uint64_t p){T v;std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof v);return v;}
template<class Guest>void execute(Guest& c,unsigned kind){
 const auto builder=c.x[0];
 if(kind==0)c.x[0]=load<uint32_t>(builder+0x10);
 else if(kind==3)c.x[0]=load<uint32_t>(builder+0x14);
 else if(kind==2){c.x[8]=load<uint32_t>(builder+0x28);c.x[0]=uint32_t(c.x[8]+1);}
 else{const auto count=load<uint32_t>(builder+0x10);c.x[8]=count;if(int32_t(count)<=int32_t(c.x[1]))c.x[0]=0;else{c.x[8]=load<uint64_t>(builder+0x18);c.x[0]=load<uint64_t>(c.x[8]+(uint64_t(int64_t(int32_t(c.x[1])))<<3));}}
}
}
