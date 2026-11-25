template<bool Tangent>
__attribute__((target("avx,fma"))) bool validate_async_skin_source(const uint8_t* source, const std::vector<SkinMatrix>& matrices,
                                                            float* dest, const float* preserved, uint32_t count,
                                                            bool allow_canonical, SkinReject& reason, uint32_t& element,
                                                            SkinJoints& bad_joints, uint64_t& canonical_count, SkinJoints* captured, uint8_t* canonical_mask) {
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

        _mm_storeu_si128(reinterpret_cast<__m128i*>(captured[i].data()),packed_joints);
        canonical_mask[i]=canonical?1:0;canonical_count+=canonical?1:0;
    }
    return true;
}
