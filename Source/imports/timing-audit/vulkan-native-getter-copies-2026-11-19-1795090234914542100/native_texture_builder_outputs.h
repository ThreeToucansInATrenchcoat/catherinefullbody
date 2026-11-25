#pragma once
#include <cstdint>
#include <cstring>
namespace catherine::native_builder_outputs {
inline constexpr uint64_t addresses[]={0x70054277e0ull,0x7005427830ull};
template<class T> T load(uint64_t p){T v;std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof v);return v;}
inline void store(uint64_t p,uint32_t v){std::memcpy(reinterpret_cast<void*>(p),&v,4);}
inline uint32_t rotate(uint32_t v){return(v>>4)|(v<<28);}
template<class Guest> void execute(Guest& c,unsigned kind){
 const uint64_t builder=c.x[0];
 if(!kind){
   for(unsigned i=0;i<4;++i){const auto v=load<uint32_t>(builder+0x40+4*i);store(c.x[1+i],v);c.x[8]=v;}
   return;
 }
 const uint64_t output=c.x[1];store(output,0);
 uint64_t x8=load<uint16_t>(builder+0x60),x9=c.x[9];
 if(x8&15){
   x9=rotate(uint32_t(x8))&15;store(output,uint32_t(x9));
   x9=rotate(load<uint16_t>(builder+0x60))&0x0fffffff;
   x9=uint32_t(x9)&0xf0;
   x9=(uint32_t(x9)&0xfffffff0)|(((uint32_t(x9)&0x0ffffff0)|(rotate(uint32_t(x8))&0xf000000f))&15);store(output,uint32_t(x9));
   x8=rotate(load<uint16_t>(builder+0x60))&0x0fffffff;x8=uint32_t(x8)&0xf00;x8=uint32_t(x8)|uint32_t(x9);store(output,uint32_t(x8));
   x8=load<uint8_t>(builder+0x60);
 }
 c.x[0]=uint32_t(x8)&15;c.x[8]=x8;c.x[9]=x9;
}
}
