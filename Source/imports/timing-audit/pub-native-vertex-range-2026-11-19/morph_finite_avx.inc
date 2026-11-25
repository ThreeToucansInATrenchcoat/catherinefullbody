namespace morph_finite_scan {
int mode() {
    static const int value=[] {const auto* p=std::getenv("CATHERINE_MORPH_FINITE_SCAN");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();
    return value;
}
// Each loaded pair is exactly two16-byte source records. Bit3/7 are W,
// deliberately ignored just as in the original one-record predicate.
__attribute__((target("avx2"))) bool avx(const uint8_t* source,uint32_t count) {
    const __m256i exponent=_mm256_set1_epi32(0x7f800000);
    uint32_t i=0;
    for(;count-i>=8;i+=8) {
        const auto* p=source+uint64_t(i)*16;
        const auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        const auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+32));
        const auto c=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+64));
        const auto d=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+96));
        const auto ab=_mm256_or_si256(_mm256_cmpeq_epi32(_mm256_and_si256(a,exponent),exponent),_mm256_cmpeq_epi32(_mm256_and_si256(b,exponent),exponent));
        const auto cd=_mm256_or_si256(_mm256_cmpeq_epi32(_mm256_and_si256(c,exponent),exponent),_mm256_cmpeq_epi32(_mm256_and_si256(d,exponent),exponent));
        if(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_or_si256(ab,cd)))&0x77)return false;
    }
    for(;count-i>=2;i+=2) {
        const auto bits=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(source+uint64_t(i)*16));
        if(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(bits,exponent),exponent)))&0x77)return false;
    }
    if(i<count) {
        const __m128i mask=_mm_set1_epi32(0x7f800000);
        const auto bits=_mm_loadu_si128(reinterpret_cast<const __m128i*>(source+uint64_t(i)*16));
        if(_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(bits,mask),mask)))&7)return false;
    }
    return true;
}
bool compared(const uint8_t* source,uint32_t count) {
    const unsigned saved=_mm_getcsr();
    try {
        std::vector<uint8_t> owned;
        if(count)owned.assign(source,source+uint64_t(count)*16);
        _mm_setcsr(saved);
        const bool result=avx(owned.data(),count);const unsigned actual=_mm_getcsr();
        _mm_setcsr(saved);
        const bool reference=finite_zero_morph_source_original(owned.data(),count);const unsigned expected=_mm_getcsr();
        if(result!=reference||actual!=expected) {log("Morph finite AVX mismatch: count %u result %d/%d MXCSR %x/%x",count,int(result),int(reference),actual,expected);std::abort();}
        static std::atomic<uint64_t> comparisons{};const auto n=comparisons.fetch_add(1,std::memory_order_relaxed)+1;
        if(n==1||n%100000==0)log("Morph finite AVX: compared %llu scans, no mismatches",(unsigned long long)n);
        return result;
    } catch(const std::bad_alloc&) {_mm_setcsr(saved);return finite_zero_morph_source_original(source,count);}
}
}
bool finite_zero_morph_source(const uint8_t* source,uint32_t count) {
    const int m=morph_finite_scan::mode();
    if(!m)return finite_zero_morph_source_original(source,count);
    if(m==1)return morph_finite_scan::avx(source,count);
    return morph_finite_scan::compared(source,count);
}
