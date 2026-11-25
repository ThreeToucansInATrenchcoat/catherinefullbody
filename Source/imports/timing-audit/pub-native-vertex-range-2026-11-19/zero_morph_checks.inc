struct ZeroMorphStats{uint64_t considered=0,skipped=0,verified=0,mismatches=0,source_hits=0,source_misses=0,source_bad=0,source_compared=0,source_copied=0,source_evictions=0,budget_fallback=0,proof_hits=0,proof_scans=0,identity_rejects=0,owned_rechecks=0,source_scans=0,source_vertices=0;}g_zero_morph_stats;

bool finite_zero_morph_source_original(const uint8_t* source,uint32_t count){
    const __m128i exponent=_mm_set1_epi32(0x7f800000);
    for(uint32_t i=0;i<count;++i){const __m128i bits=_mm_loadu_si128(reinterpret_cast<const __m128i*>(source+uint64_t(i)*16));
        if(_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(bits,exponent),exponent)))&7)return false;
    }return true;
}
#include "morph_finite_avx.inc"
bool zero_morph_source_finite(const uint8_t* source,uint32_t count){
    ++g_zero_morph_stats.source_scans;g_zero_morph_stats.source_vertices+=count;
    const uint64_t ps_ms=perf22::start();
    const bool finite=source&&count&&finite_zero_morph_source(source,count);
    perf22::add(perf22::MorphScan,ps_ms);
    if(!finite)++g_zero_morph_stats.source_bad;
    return finite;
}
bool zero_morph_base_identity(const uint8_t* base,uint32_t count,uint32_t stride){
    if(!base||!count||(stride!=32&&stride!=64))return false;
    const __m128i zero=_mm_setzero_si128(),exponent=_mm_set1_epi32(0x7f800000);
    for(uint32_t i=0;i<count;++i){
        const __m128i bits=_mm_loadu_si128(reinterpret_cast<const __m128i*>(base+uint64_t(i)*stride));
        const __m128i e=_mm_and_si128(bits,exponent);
        const __m128i bad_zero=_mm_andnot_si128(_mm_cmpeq_epi32(bits,zero),_mm_cmpeq_epi32(e,zero));
        const __m128i bad=_mm_or_si128(bad_zero,_mm_cmpeq_epi32(e,exponent));
        if(_mm_movemask_ps(_mm_castsi128_ps(bad))&7)return false;
    }return true;
}
bool zero_morph_base_certificate(It existing,const uint8_t* base,uint32_t count,uint32_t stride,uint64_t& proof_count){
    if(existing!=g_regions.end()&&existing->second.morph_identity_stride==stride&&existing->second.morph_identity_count>=count){
        ++g_zero_morph_stats.proof_hits;proof_count=existing->second.morph_identity_count;return true;
    }
    ++g_zero_morph_stats.proof_scans;
    if(!zero_morph_base_identity(base,count,stride)){++g_zero_morph_stats.identity_rejects;return false;}
    proof_count=count;return true;
}
void zero_morph_log(){
    if(!zero_morph_mode())return;const auto& s=g_zero_morph_stats;
    log("Zero in-place morph: mode %d considered %llu skipped %llu verified %llu mismatches %llu source-hits %llu source-misses %llu source-bad %llu compared %llu copied %llu retained %llu evicted %llu budget-fallback %llu proof-hits %llu proof-scans %llu identity-rejects %llu owned-rechecks %llu source-scans %llu source-vertices %llu",
        zero_morph_mode(),(unsigned long long)s.considered,(unsigned long long)s.skipped,(unsigned long long)s.verified,(unsigned long long)s.mismatches,
        (unsigned long long)s.source_hits,(unsigned long long)s.source_misses,(unsigned long long)s.source_bad,(unsigned long long)s.source_compared,(unsigned long long)s.source_copied,
        0ull,(unsigned long long)s.source_evictions,(unsigned long long)s.budget_fallback,
        (unsigned long long)s.proof_hits,(unsigned long long)s.proof_scans,(unsigned long long)s.identity_rejects,(unsigned long long)s.owned_rechecks,(unsigned long long)s.source_scans,(unsigned long long)s.source_vertices);
}
