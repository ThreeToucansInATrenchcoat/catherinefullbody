#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <immintrin.h>
namespace experimental_uniform_shapes {
template<class Range> inline bool scalar(const Range* a0,const Range* b0,const Range* a1,const Range* b1) {
    for(unsigned stage=0;stage<2;++stage) {
        const auto* a=stage?a1:a0;const auto* b=stage?b1:b0;
        for(unsigned k=0;k<18;++k)
            if(a[k].size!=b[k].size || !a[k].address!=!b[k].address) return false;
    }
    return true;
}
template<class Range> __attribute__((target("avx2"))) bool native(const Range* a0,const Range* b0,const Range* a1,const Range* b1) {
    static_assert(sizeof(Range)==16&&offsetof(Range,address)==0&&offsetof(Range,size)==8);
    static_assert(sizeof(a0[0].address)==8&&sizeof(a0[0].size)==8);
    const __m256i zero=_mm256_setzero_si256();
    for(unsigned stage=0;stage<2;++stage) {
      const auto* a=stage?a1:a0;const auto* b=stage?b1:b0;
      for(unsigned k=0;k<18;k+=2) {
        const auto left=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a+k));
        const auto right=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(b+k));
        const auto nulls=_mm256_xor_si256(_mm256_cmpeq_epi64(left,zero),_mm256_cmpeq_epi64(right,zero));
        const auto sizes=_mm256_xor_si256(left,right);
        // Each address contributes only its nullness; each size contributes all
        // 64 bits. Low/high 32-bit halves of address lanes are selected together.
        const auto mismatch=_mm256_blend_epi32(sizes,nulls,0x33);
        if(!_mm256_testz_si256(mismatch,mismatch))return false;
      }
    }
    return true;
}
template<class Range> inline bool equal(const Range* a0,const Range* b0,const Range* a1,const Range* b1) {
    static const int mode=[] {const char* e=std::getenv("CATHERINE_NATIVE_UNIFORM_SHAPES");return e?std::atoi(e):1;}();
    if(!mode)return scalar(a0,b0,a1,b1);
    const bool answer=native(a0,b0,a1,b1);
    if(mode==2&&answer!=scalar(a0,b0,a1,b1))std::abort();
    return answer;
}
}
