#pragma once
#include <cstdint>
#include <cstring>
namespace catherine::native_sampler {
inline constexpr uint64_t addresses[]={0x70054257f0ull,0x7005425810ull,0x7005425830ull,0x7005425850ull,0x7005425860ull,0x7005425880ull,0x7005425910ull,0x7005425920ull};
inline constexpr unsigned outputs[]={2,3,2,0,2,1,0,0};
inline constexpr unsigned offsets[]={8,16,28,36,40,48,64,68};
inline uint32_t load(uint64_t p){uint32_t v;std::memcpy(&v,reinterpret_cast<const void*>(p),4);return v;}
inline void store(uint64_t p,uint32_t v){std::memcpy(reinterpret_cast<void*>(p),&v,4);}
template<class Guest> void execute(Guest& c,unsigned kind){
 const uint64_t builder=c.x[0];
 if(kind==3||kind==6){const auto value=load(builder+offsets[kind]);std::memset(c.v[0],0,16);std::memcpy(c.v[0],&value,4);return;}
 if(kind==7){c.x[0]=load(builder+offsets[kind]);return;}
 for(unsigned i=0;i<(kind==5?4:outputs[kind]);++i){const auto value=load(builder+offsets[kind]+4*i);store(kind==5?c.x[1]+4*i:c.x[1+i],value);c.x[8]=value;}
}
}
