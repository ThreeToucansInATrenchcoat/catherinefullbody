template<bool Tangent>
__attribute__((target("avx,fma"))) bool run_cpu_skin_compact_preserved(const uint8_t* source, const std::vector<SkinMatrix>& matrices,
                                                            float* dest, const float* preserved, uint32_t count,
                                                            bool allow_canonical, SkinReject& reason, uint32_t& element,
                                                            SkinJoints& bad_joints, uint64_t& canonical_count) {
    const SkinMxcsr mxcsr;
    const __m128i max_joint = _mm_set1_epi32(int(matrices.size()) - 1);
    const __m128i negative_qnan = _mm_set1_epi32(-0x400000);
    const __m128i negative_qnan_position = _mm_set_epi32(0x3f800000, -0x400000, -0x400000, -0x400000);
    const __m128 canonical_xyz = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* s = source + uint64_t(i) * (Tangent ? 80 : 64);
        const float* src = reinterpret_cast<const float*>(s);
        const uint8_t* weights_bytes = s + (Tangent ? 48 : 32);
        const __m128i packed_joints = _mm_loadu_si128(reinterpret_cast<const __m128i*>(weights_bytes + 16));
        const __m128i large_joint = _mm_cmpgt_epi32(packed_joints, max_joint);
        const __m128i bad_joint = _mm_or_si128(packed_joints, large_joint);  // also reject unsigned values >= 2^31
        if (_mm_movemask_ps(_mm_castsi128_ps(bad_joint))) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(bad_joints.data()), packed_joints);
            reason = SkinReject::JointIndex;
            element = i;
            return false;
        }

        const __m256 input = _mm256_loadu_ps(src);
        const __m128 p = _mm256_castps256_ps128(input);
        const __m128 n = _mm256_extractf128_ps(input, 1);
        const __m128 tangent = Tangent ? _mm_loadu_ps(src + 8) : _mm_setzero_ps();
        const __m128 weights = _mm_loadu_ps(reinterpret_cast<const float*>(weights_bytes));
        const __m128i bad_normal = _mm_and_si128(skin_large_lanes(n), _mm_set_epi32(0, -1, -1, -1));
        const bool bad_base = _mm_movemask_ps(_mm_castsi128_ps(_mm_or_si128(bad_normal, skin_large_lanes(weights)))) != 0;
        const bool bad_pos = _mm_movemask_ps(_mm_castsi128_ps(skin_large_lanes(p))) != 0;
        const bool bad_tangent = Tangent && (_mm_movemask_ps(_mm_castsi128_ps(skin_large_lanes(tangent))) & 7) != 0;
        bool canonical = false;
        if (bad_base || bad_pos || bad_tangent) {
            if constexpr (Tangent) {
                canonical = allow_canonical && !bad_base && !bad_pos && bad_tangent &&
                    (_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(tangent), negative_qnan))) & 7) == 7;
            } else {
                canonical = allow_canonical && !bad_base && bad_pos &&
                    _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(p), negative_qnan_position))) == 15;
            }
            if (!canonical) {
                reason = SkinReject::VertexValue;
                element = i;
                return false;
            }
        }

        const uint32_t j0 = uint32_t(_mm_cvtsi128_si32(packed_joints));
        const uint32_t j1 = uint32_t(_mm_extract_epi32(packed_joints, 1));
        const uint32_t j2 = uint32_t(_mm_extract_epi32(packed_joints, 2));
        const uint32_t j3 = uint32_t(_mm_extract_epi32(packed_joints, 3));
        const auto first = paired_skin_dot<Tangent>(matrices[j1], input, tangent);
        const __m256 weights8 = _mm256_set_m128(weights, weights);
        const __m256 weight1 = _mm256_permute_ps(weights8, 0x55);
        __m256 out = _mm256_mul_ps(first.position_normal, weight1);
        __m128 out_tangent = _mm_setzero_ps();
        if constexpr (Tangent) out_tangent = _mm_mul_ps(first.tangent, _mm256_castps256_ps128(weight1));
        accumulate_fused_skin<Tangent, 0>(matrices[j0], input, tangent, weights8, out, out_tangent);
        accumulate_fused_skin<Tangent, 2>(matrices[j2], input, tangent, weights8, out, out_tangent);
        accumulate_fused_skin<Tangent, 3>(matrices[j3], input, tangent, weights8, out, out_tangent);

        const uint64_t offset = uint64_t(i) * (Tangent ? 12 : 8);
        float* d = dest + offset;
        const auto* words=reinterpret_cast<const uint32_t*>(preserved)+uint64_t(i)*2;
        const __m128i prior_bits=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(words));
        const __m128 normal_w=_mm_castsi128_ps(_mm_shuffle_epi32(prior_bits,0));
        const __m128 tangent_w=_mm_castsi128_ps(_mm_shuffle_epi32(prior_bits,0x55));
        const __m256 untouched = _mm256_set_m128(normal_w, _mm_set1_ps(1.0f));
        if (canonical) {
            ++canonical_count;
            if constexpr (Tangent) out_tangent = canonical_xyz;
            else out = _mm256_insertf128_ps(out, canonical_xyz, 0);
        }
        _mm256_storeu_ps(d, _mm256_blend_ps(untouched, out, 0x77));
        if constexpr (Tangent) _mm_storeu_ps(d + 8, _mm_blend_ps(tangent_w, out_tangent, 7));
    }
    return true;
}
