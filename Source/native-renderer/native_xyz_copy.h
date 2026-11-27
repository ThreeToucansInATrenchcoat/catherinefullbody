#pragma once
#include <cstdint>
#include <cstring>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
namespace experimental_xyz {
inline void scalar(uint8_t*out,const uint8_t*in,uint64_t count,uint64_t stride){
 if(stride==12){std::memcpy(out,in,count*12);return;}
 for(uint64_t n=0;n<count;++n){std::memcpy(out+n*12,in+n*stride,8);std::memcpy(out+n*12+8,in+n*stride+8,4);}
}
inline void native(uint8_t*out,const uint8_t*in,uint64_t count,uint64_t stride){
 if(stride==12){std::memcpy(out,in,count*12);return;}
 uint64_t n=0;
#if defined(__AVX2__)
 // Every caller supplies complete stride-sized records. Read four full rows
 // only when each stride contains at least a 16-byte row; short rows retain
 // exact 8+4-byte loads. All shuffles are integer bit operations.
 if(stride>=16)for(;count-n>=4;n+=4){
  const auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+(n+0)*stride));
  const auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+(n+1)*stride));
  const auto c=_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+(n+2)*stride));
  const auto d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(in+(n+3)*stride));
  const auto x=_mm_blend_epi16(a,_mm_shuffle_epi32(b,0),0xc0);
  const auto y=_mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(b),_mm_castsi128_ps(c),_MM_SHUFFLE(1,0,2,1)));
  const auto z=_mm_or_si128(_mm_and_si128(_mm_srli_si128(c,8),_mm_set_epi32(0,0,0,-1)),_mm_slli_si128(d,4));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(out+n*12),x);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(out+n*12+16),y);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(out+n*12+32),z);
 }
#endif
 for(;n<count;++n){std::memcpy(out+n*12,in+n*stride,8);std::memcpy(out+n*12+8,in+n*stride+8,4);}
}
}
