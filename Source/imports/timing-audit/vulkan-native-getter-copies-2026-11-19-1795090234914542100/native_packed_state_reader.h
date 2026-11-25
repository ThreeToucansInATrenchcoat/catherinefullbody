#pragma once
#include <cstdint>
#include <cstring>
namespace catherine::native_packed {
inline constexpr unsigned count=22;
inline constexpr uint64_t addresses[]={0x7005426370ull,0x7005426390ull,0x70054263d0ull,0x70054263e0ull,0x7005426110ull,0x7005426120ull,0x7005426160ull,0x70054262f0ull,0x7005426300ull,0x7005426310ull,0x7005426220ull,0x7005426520ull,0x7005426530ull,0x7005426540ull,0x7005426550ull,0x7005426560ull,0x7005426580ull,0x7005426660ull,0x7005426670ull,0x7005426680ull,0x7005426690ull,0x7005426760ull};
struct Meta{unsigned bytes,argc,start,outputs,stride;};
inline constexpr Meta meta[]={{4,3,1,2,8},{4,1,0,0,0},{8,1,0,0,0},{8,1,0,0,0},{8,1,0,0,0},{8,5,1,4,4},{8,3,1,2,4},{4,2,0,0,0},{4,1,0,0,0},{4,1,0,0,0},{4,6,2,4,4},{8,1,0,0,0},{8,1,0,0,0},{8,1,0,0,0},{8,1,0,0,0},{8,2,0,0,0},{8,5,2,3,4},{4,1,0,0,0},{4,1,0,0,0},{4,1,0,0,0},{4,1,0,0,0},{24,1,0,0,0}};
template<class T>T load(uint64_t p){T v;std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof v);return v;}
template<class T>void store(uint64_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
template<class Guest>void execute(Guest& c,unsigned kind){
 const uint64_t s=c.x[0];uint32_t word;
 switch(kind){
 case 0:c.x[8]=(load<uint32_t>(s)>>21)&0x3ff;store<uint32_t>(c.x[1],uint32_t(c.x[8]));c.x[8]=(load<uint32_t>(s)>>7)&0x3fff;store<uint64_t>(c.x[2],c.x[8]);break;
 case 1:c.x[8]=load<uint32_t>(s);c.x[0]=uint32_t(c.x[8])&31;break;
 case 2:c.x[0]=load<uint32_t>(s);break;
 case 3:c.x[0]=load<uint32_t>(s+4);break;
 case 4:c.x[8]=load<uint32_t>(s);c.x[0]=uint32_t(c.x[8])&7;break;
 case 5:c.x[8]=load<uint32_t>(s+4)&127;store<uint32_t>(c.x[1],uint32_t(c.x[8]));c.x[8]=(load<uint32_t>(s+4)>>8)&127;store<uint32_t>(c.x[2],uint32_t(c.x[8]));c.x[8]=load<uint16_t>(s+6)&127;store<uint32_t>(c.x[3],uint32_t(c.x[8]));c.x[8]=load<uint8_t>(s+7)&127;store<uint32_t>(c.x[4],uint32_t(c.x[8]));break;
 case 6:c.x[8]=load<uint8_t>(s+3)&7;store<uint32_t>(c.x[1],uint32_t(c.x[8]));c.x[8]=(load<uint32_t>(s)>>28)&7;store<uint32_t>(c.x[2],uint32_t(c.x[8]));break;
 case 7:c.x[8]=uint32_t(load<uint8_t>(s+1))>>(uint32_t(c.x[1])&31);c.x[0]=uint32_t(c.x[8])&1;break;
 case 8:c.x[0]=load<uint8_t>(s+2);break;
 case 9:c.x[8]=load<uint8_t>(s+3);c.x[0]=uint32_t(c.x[8])&15;break;
 case 10:word=load<uint32_t>(s)>>((uint32_t(c.x[1])<<2)&31);c.x[9]=word&255;c.x[8]=word&1;store<uint8_t>(c.x[2],uint8_t(c.x[8]));c.x[8]=(uint32_t(c.x[9])>>1)&1;c.x[10]=(uint32_t(c.x[9])>>2)&1;store<uint8_t>(c.x[3],uint8_t(c.x[8]));c.x[8]=(uint32_t(c.x[9])>>3)&1;store<uint8_t>(c.x[4],uint8_t(c.x[10]));store<uint8_t>(c.x[5],uint8_t(c.x[8]));break;
 case 11:c.x[8]=load<uint8_t>(s);c.x[0]=uint32_t(c.x[8])&1;break;
 case 12:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>1)&1;break;
 case 13:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>4)&15;break;
 case 14:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>2)&1;break;
 case 15:word=load<uint32_t>(s+4);c.x[9]=word>>16;c.x[8]=uint32_t(c.x[1])==2?uint32_t(c.x[9]):word;c.x[0]=uint32_t(c.x[8])&15;break;
 case 16:{const bool back=uint32_t(c.x[1])==2;c.x[8]=(load<uint32_t>(s+4)>>(back?20:4))&15;store<uint32_t>(c.x[2],uint32_t(c.x[8]));c.x[8]=(load<uint32_t>(s+4)>>(back?28:12))&15;c.x[9]=back?24:8;store<uint32_t>(c.x[3],uint32_t(c.x[8]));c.x[8]=(load<uint32_t>(s+4)>>uint32_t(c.x[9]))&15;store<uint32_t>(c.x[4],uint32_t(c.x[8]));break;}
 case 17:c.x[8]=load<uint32_t>(s);c.x[0]=uint32_t(c.x[8])&3;break;
 case 18:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>2)&1;break;
 case 19:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>3)&3;break;
 case 20:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>15)&15;break;
 case 21:c.x[8]=load<uint32_t>(s);c.x[0]=(uint32_t(c.x[8])>>1)&1;break;
 }
}
}
