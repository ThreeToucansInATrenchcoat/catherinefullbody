// Windows x64 skinning for a fresh, unpublished CPU destination.
struct PairedSkinDot {
    __m256 position_normal;
    __m128 tangent;
};

template<bool Tangent>
__attribute__((always_inline, target("avx,fma"))) inline PairedSkinDot paired_skin_dot(
    const SkinMatrix& m, __m256 position_normal, __m128 tangent) {
    const __m256 x = _mm256_broadcast_ps(&m.columns[0]);
    const __m256 y = _mm256_broadcast_ps(&m.columns[1]);
    const __m256 z = _mm256_broadcast_ps(&m.columns[2]);
    const __m128 translation = m.columns[3];
    __m256 out = _mm256_mul_ps(_mm256_permute_ps(position_normal, 0x00), x);
    out = _mm256_fmadd_ps(_mm256_permute_ps(position_normal, 0x55), y, out);
    out = _mm256_fmadd_ps(_mm256_permute_ps(position_normal, 0xaa), z, out);
    const __m128 p = _mm256_castps256_ps128(position_normal);
    const __m128 translated = _mm_fmadd_ps(_mm_shuffle_ps(p, p, 0xff), translation, _mm256_castps256_ps128(out));
    out = _mm256_insertf128_ps(out, translated, 0);
    __m128 out_tangent = _mm_setzero_ps();
    if constexpr (Tangent) {
        out_tangent = _mm_mul_ps(_mm_shuffle_ps(tangent, tangent, 0x00), _mm256_castps256_ps128(x));
        out_tangent = _mm_fmadd_ps(_mm_shuffle_ps(tangent, tangent, 0x55), _mm256_castps256_ps128(y), out_tangent);
        out_tangent = _mm_fmadd_ps(_mm_shuffle_ps(tangent, tangent, 0xaa), _mm256_castps256_ps128(z), out_tangent);
    }
    return {out, out_tangent};
}

__attribute__((always_inline, target("avx,fma"))) inline __m128i skin_large_lanes(__m128 value) {
    const __m128i magnitude = _mm_and_si128(_mm_castps_si128(value), _mm_set1_epi32(0x7fffffff));
    return _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(0x4e800000));
}

struct SkinMxcsr {
    unsigned saved = _mm_getcsr();

    SkinMxcsr() {
        _mm_setcsr((saved & ~_MM_ROUND_MASK) | _MM_ROUND_NEAREST | _MM_MASK_MASK | _MM_FLUSH_ZERO_ON |
                   _MM_DENORMALS_ZERO_ON);
    }
    ~SkinMxcsr() { _mm_setcsr(saved); }
};

__attribute__((target("avx,fma"))) bool transpose_skin_palette(const uint8_t* palette, uint32_t count,
                                                               std::vector<SkinMatrix>& matrices, uint32_t& element) {
    matrices.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const float* m = reinterpret_cast<const float*>(palette + uint64_t(i) * 64);
        __m128 a = _mm_loadu_ps(m), b = _mm_loadu_ps(m + 4), c = _mm_loadu_ps(m + 8), d = _mm_setzero_ps();
        const __m128i bad = _mm_or_si128(skin_large_lanes(a), _mm_or_si128(skin_large_lanes(b), skin_large_lanes(c)));
        if (_mm_movemask_ps(_mm_castsi128_ps(bad))) {
            element = i;
            return false;
        }
        _MM_TRANSPOSE4_PS(a, b, c, d);
        matrices[i] = {{a, b, c, d}};
    }
    return true;
}

template<bool Tangent, unsigned Bone>
__attribute__((always_inline, target("avx,fma"))) inline void accumulate_fused_skin(
    const SkinMatrix& matrix, __m256 input, __m128 tangent, __m256 weights, __m256& out, __m128& out_tangent) {
    const auto joint = paired_skin_dot<Tangent>(matrix, input, tangent);
    const __m256 w = _mm256_permute_ps(weights, Bone * 0x55);
    out = _mm256_fmadd_ps(w, joint.position_normal, out);
    if constexpr (Tangent) out_tangent = _mm_fmadd_ps(_mm256_castps256_ps128(w), joint.tangent, out_tangent);
}

template<bool Tangent>
__attribute__((target("avx,fma"))) bool run_cpu_skin_fused(const uint8_t* source, const std::vector<SkinMatrix>& matrices,
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
        const float* prior = preserved + offset;
        const __m256 untouched = _mm256_set_m128(_mm_loadu_ps(prior + 4), _mm_set1_ps(1.0f));
        if (canonical) {
            ++canonical_count;
            if constexpr (Tangent) out_tangent = canonical_xyz;
            else out = _mm256_insertf128_ps(out, canonical_xyz, 0);
        }
        _mm256_storeu_ps(d, _mm256_blend_ps(untouched, out, 0x77));
        if constexpr (Tangent) _mm_storeu_ps(d + 8, _mm_blend_ps(_mm_loadu_ps(prior + 8), out_tangent, 7));
    }
    return true;
}

bool room_for_speculative_skin(uint64_t capacity) {
    // upload_alloc may align the head and skip a short physical tail. Do not let an unpublished
    // candidate wrap over earlier data in this submit, including any src/palette shadow views.
    const uint64_t ring_size = frame_ring_size();
    const uint64_t margin = 2 * (capacity + kAlign) + (kAlign - 1);
    return ring_size >= margin && frame_upload_used() <= ring_size - margin;
}
