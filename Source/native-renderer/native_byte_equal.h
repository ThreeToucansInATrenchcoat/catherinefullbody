#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <immintrin.h>
namespace experimental_bytes {
__attribute__((target("avx2"))) inline bool native(const void*left,const void*right,size_t count){
 const auto*a=static_cast<const uint8_t*>(left);const auto*b=static_cast<const uint8_t*>(right);
 while(count>=32){const auto x=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));const auto y=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));if(_mm256_movemask_epi8(_mm256_cmpeq_epi8(x,y))!=-1)return false;a+=32;b+=32;count-=32;}
 while(count>=8){uint64_t x,y;std::memcpy(&x,a,8);std::memcpy(&y,b,8);if(x!=y)return false;a+=8;b+=8;count-=8;}
 if(count>=4){uint32_t x,y;std::memcpy(&x,a,4);std::memcpy(&y,b,4);if(x!=y)return false;a+=4;b+=4;count-=4;}
 if(count>=2){uint16_t x,y;std::memcpy(&x,a,2);std::memcpy(&y,b,2);if(x!=y)return false;a+=2;b+=2;count-=2;}
 return !count||*a==*b;
}
inline bool equal(const void*a,const void*b,size_t n){
 static const int mode=[] {const auto*e=std::getenv("CATHERINE_NATIVE_BYTE_EQUAL");return e?std::atoi(e):1;}();
 if(!mode)return std::memcmp(a,b,n)==0;
 const bool answer=native(a,b,n);
 if(mode==2&&answer!=(std::memcmp(a,b,n)==0))std::abort();
 return answer;
}
}
