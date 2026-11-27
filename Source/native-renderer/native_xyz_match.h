#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <immintrin.h>
namespace experimental_xyz_match {
inline bool scalar(const uint8_t* want,const uint8_t* bytes,uint64_t count,uint64_t stride) {
    if(stride==12)return std::memcmp(want,bytes,count*12)==0;
    for(uint64_t i=0;i<count;++i) {
        uint64_t a,x;uint32_t b,y;
        std::memcpy(&a,bytes+i*stride,8);std::memcpy(&b,bytes+i*stride+8,4);
        std::memcpy(&x,want+i*12,8);std::memcpy(&y,want+i*12+8,4);
        if((a^x)|uint64_t(b^y))return false;
    }
    return true;
}
__attribute__((target("avx2"))) inline bool native(const uint8_t* want,const uint8_t* bytes,uint64_t count,uint64_t stride) {
    if(stride==12)return std::memcmp(want,bytes,count*12)==0;
    uint64_t i=0;
    if(stride>=16)for(;count-i>=4;i+=4) {
        const auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes+(i+0)*stride));
        const auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes+(i+1)*stride));
        const auto c=_mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes+(i+2)*stride));
        const auto d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes+(i+3)*stride));
        // Same four-row packing as the verified XYZ capture helper. W and
        // every other record field remain outside the comparison.
        const auto x=_mm_blend_epi16(a,_mm_shuffle_epi32(b,0),0xc0);
        const auto y=_mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(b),_mm_castsi128_ps(c),_MM_SHUFFLE(1,0,2,1)));
        const auto z=_mm_or_si128(_mm_and_si128(_mm_srli_si128(c,8),_mm_set_epi32(0,0,0,-1)),_mm_slli_si128(d,4));
        const auto dx=_mm_xor_si128(x,_mm_loadu_si128(reinterpret_cast<const __m128i*>(want+i*12)));
        const auto dy=_mm_xor_si128(y,_mm_loadu_si128(reinterpret_cast<const __m128i*>(want+i*12+16)));
        const auto dz=_mm_xor_si128(z,_mm_loadu_si128(reinterpret_cast<const __m128i*>(want+i*12+32)));
        const auto mismatch=_mm_or_si128(_mm_or_si128(dx,dy),dz);
        if(!_mm_testz_si128(mismatch,mismatch))return false;
    }
    for(;i<count;++i) {
        uint64_t a,x;uint32_t b,y;
        std::memcpy(&a,bytes+i*stride,8);std::memcpy(&b,bytes+i*stride+8,4);
        std::memcpy(&x,want+i*12,8);std::memcpy(&y,want+i*12+8,4);
        if((a^x)|uint64_t(b^y))return false;
    }
    return true;
}
inline bool equal(const uint8_t* want,const uint8_t* bytes,uint64_t count,uint64_t stride) {
    static const int mode=[] {const char*e=std::getenv("CATHERINE_NATIVE_XYZ_MATCH");return e?std::atoi(e):0;}();
    if(!mode)return scalar(want,bytes,count,stride);
    const bool answer=native(want,bytes,count,stride);
    if(mode==2&&answer!=scalar(want,bytes,count,stride))std::abort();
    return answer;
}
}
