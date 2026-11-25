// bridge84: the state-object binds of nvn_state.cpp with their decodes cached by the objects' bytes (state_cache84.h).
// Each decode lambda is the original decode, unchanged; what is recorded is unchanged. Object sizes (NVN SDK): vertex
// attrib 4 (the array stride used here), vertex stream 8 (likewise), blend 8, color 4, channel mask 4, depth-stencil 8,
// polygon 4, multisample 24. Each decode zero-fills its struct first, so padding bytes are zero (they were
// indeterminate; the recorded blob carries them, and nothing reads them).

struct Attribs84 { VertexAttrib v[32]; };
struct Streams84 { VertexStream v[32]; };

void BindVertexAttribState(CPU& c) {
    uint32_t n = std::min<uint32_t>(uint32_t(c.x[1]), 32);
    thread_local bridge84::Cache<4 * 32, Attribs84> cache;
    const Attribs84 a = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(c.x[2]), 4 * n, [&] {
        Attribs84 v;
        std::memset(&v, 0, sizeof v);
        uint64_t tmp = scratch(c, 16);
        for (uint32_t k = 0; k < n; k++) {
            uint64_t state = c.x[2] + 4 * k;
            native_state_get(c,0, {state, tmp, tmp + 8});
            v.v[k].format = ld<uint32_t>(tmp);
            v.v[k].offset = uint32_t(ld<uint64_t>(tmp + 8));
            v.v[k].stream = uint32_t(native_state_get(c,1, {state}));
        }
        return v;
    }, 0);
    for (uint32_t k = 0; k < n; k++) log_state("attrib", a.v[k]);
    {
        static std::atomic<uint64_t> calls{0};
        if ((++calls & 0x3ffff) == 0)
            log("bridge84 mode %d: %llu hits %llu misses; %llu verified, %llu mismatches; by type (attrib stream blend color "
                "mask depth polygon multisample) %llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu",
                bridge84::mode(),
                (unsigned long long)bridge84::g_stats.hits.load(), (unsigned long long)bridge84::g_stats.misses.load(),
                (unsigned long long)bridge84::g_stats.verified.load(), (unsigned long long)bridge84::g_stats.mismatches.load(),
                (unsigned long long)bridge84::g_type_mismatch[0].load(), (unsigned long long)bridge84::g_type_verified[0].load(),
                (unsigned long long)bridge84::g_type_mismatch[1].load(), (unsigned long long)bridge84::g_type_verified[1].load(),
                (unsigned long long)bridge84::g_type_mismatch[2].load(), (unsigned long long)bridge84::g_type_verified[2].load(),
                (unsigned long long)bridge84::g_type_mismatch[3].load(), (unsigned long long)bridge84::g_type_verified[3].load(),
                (unsigned long long)bridge84::g_type_mismatch[4].load(), (unsigned long long)bridge84::g_type_verified[4].load(),
                (unsigned long long)bridge84::g_type_mismatch[5].load(), (unsigned long long)bridge84::g_type_verified[5].load(),
                (unsigned long long)bridge84::g_type_mismatch[6].load(), (unsigned long long)bridge84::g_type_verified[6].load(),
                (unsigned long long)bridge84::g_type_mismatch[7].load(), (unsigned long long)bridge84::g_type_verified[7].load());
    }
    Cmd cmd{Cmd::BindVertexAttribs};
    cmd.n = n;
    record(c.x[0], cmd, a.v, n * sizeof(VertexAttrib));
}

void BindVertexStreamState(CPU& c) {
    uint32_t n = std::min<uint32_t>(uint32_t(c.x[1]), 32);
    thread_local bridge84::Cache<8 * 32, Streams84> cache;
    const Streams84 s = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(c.x[2]), 8 * n, [&] {
        Streams84 v;
        std::memset(&v, 0, sizeof v);
        for (uint32_t k = 0; k < n; k++) {
            uint64_t state = c.x[2] + 8 * k;
            v.v[k].stride = uint32_t(native_state_get(c,2, {state}));
            v.v[k].divisor = uint32_t(native_state_get(c,3, {state}));
        }
        return v;
    }, 1);
    Cmd cmd{Cmd::BindVertexStreams};
    cmd.n = n;
    record(c.x[0], cmd, s.v, n * sizeof(VertexStream));
}

void BindBlendState(CPU& c) {
    uint64_t state = c.x[1];
    thread_local bridge84::Cache<8, BlendTarget> cache;
    const BlendTarget b = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(state), 8, [&] {
        uint64_t tmp = scratch(c, 16);
        BlendTarget b;
        std::memset(&b, 0, sizeof b);
        b.target = uint32_t(native_state_get(c,4, {state}));
        native_state_get(c,5, {state, tmp, tmp + 4, tmp + 8, tmp + 12});
        b.src_rgb = ld<uint32_t>(tmp), b.dst_rgb = ld<uint32_t>(tmp + 4);
        b.src_a = ld<uint32_t>(tmp + 8), b.dst_a = ld<uint32_t>(tmp + 12);
        native_state_get(c,6, {state, tmp, tmp + 4});
        b.eq_rgb = ld<uint32_t>(tmp), b.eq_a = ld<uint32_t>(tmp + 4);
        return b;
    }, 2);
    Cmd cmd{Cmd::BindBlendState};
    log_state("blend", b);
    record(c.x[0], cmd, &b, sizeof b);
}

void BindColorState(CPU& c) {
    uint64_t state = c.x[1];
    thread_local bridge84::Cache<4, ColorStateInfo> cache;
    const ColorStateInfo s = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(state), 4, [&] {
        ColorStateInfo s;
        std::memset(&s, 0, sizeof s);
        for (uint32_t k = 0; k < 8; k++) s.blend_enable[k] = uint8_t(native_state_get(c,7, {state, k}) & 1);
        s.alpha_test = uint32_t(native_state_get(c,9, {state}));
        s.logic_op = uint32_t(native_state_get(c,8, {state}));
        return s;
    }, 3);
    Cmd cmd{Cmd::BindColorState};
    log_state("color", s);
    record(c.x[0], cmd, &s, sizeof s);
}

void BindChannelMaskState(CPU& c) {
    uint64_t state = c.x[1];
    thread_local bridge84::Cache<4, ChannelMaskInfo> cache;
    const ChannelMaskInfo s = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(state), 4, [&] {
        uint64_t tmp = scratch(c, 16);
        ChannelMaskInfo s;
        std::memset(&s, 0, sizeof s);
        for (uint32_t k = 0; k < 8; k++) {
            // Four NVNboolean* outputs, one slot of 4 bytes each: slots 1 byte apart overlapped when the driver stores
            // words, and draws lost channels (the title art came out green only).
            for (int ch = 0; ch < 4; ch++) st<uint32_t>(tmp + 4 * ch, 0);
            native_state_get(c,10, {state, k, tmp, tmp + 4, tmp + 8, tmp + 12});
            s.mask[k] = uint8_t((ld<uint8_t>(tmp) & 1) | (ld<uint8_t>(tmp + 4) & 1) << 1 | (ld<uint8_t>(tmp + 8) & 1) << 2 |
                                (ld<uint8_t>(tmp + 12) & 1) << 3);
        }
        return s;
    }, 4);
    Cmd cmd{Cmd::BindChannelMaskState};
    record(c.x[0], cmd, &s, sizeof s);
}

void BindDepthStencilState(CPU& c) {
    uint64_t state = c.x[1];
    thread_local bridge84::Cache<8, DepthStencilInfo> cache;
    const DepthStencilInfo s = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(state), 8, [&] {
        uint64_t tmp = scratch(c, 16);
        DepthStencilInfo s;
        std::memset(&s, 0, sizeof s);
        s.depth_test = uint8_t(native_state_get(c,11, {state}) & 1);
        s.depth_write = uint8_t(native_state_get(c,12, {state}) & 1);
        s.depth_func = uint32_t(native_state_get(c,13, {state}));
        s.stencil_test = uint8_t(native_state_get(c,14, {state}) & 1);
        for (uint32_t face = 0; face < 2; face++) {  // NVNface: 1 front, 2 back
            s.stencil_func[face] = uint32_t(native_state_get(c,15, {state, face + 1}));
            native_state_get(c,16, {state, face + 1, tmp, tmp + 4, tmp + 8});
            s.stencil_fail[face] = ld<uint32_t>(tmp), s.depth_fail[face] = ld<uint32_t>(tmp + 4);
            s.depth_pass[face] = ld<uint32_t>(tmp + 8);
        }
        return s;
    }, 5);
    Cmd cmd{Cmd::BindDepthStencilState};
    log_state("depth", s);
    record(c.x[0], cmd, &s, sizeof s);
}

void BindPolygonState(CPU& c) {
    uint64_t state = c.x[1];
    thread_local bridge84::Cache<4, PolygonInfo> cache;
    const PolygonInfo s = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(state), 4, [&] {
        PolygonInfo s;
        std::memset(&s, 0, sizeof s);
        s.cull = uint32_t(native_state_get(c,17, {state}));
        s.front = uint32_t(native_state_get(c,18, {state}));
        s.mode = uint32_t(native_state_get(c,19, {state}));
        s.offset_enables = uint32_t(native_state_get(c,20, {state}));
        return s;
    }, 6);
    Cmd cmd{Cmd::BindPolygonState};
    log_state("polygon", s);
    record(c.x[0], cmd, &s, sizeof s);
}

// BindMultisampleState(cmd, const NVNmultisampleState*) (G7; it was a trace stub). Render targets here are single
// sampled, so only alpha to coverage is decoded (through the driver's getter, like the other state objects); sample
// counts, coverage modulation and the alpha-to-coverage dither are not.
void BindMultisampleState(CPU& c) {
    uint64_t state = c.x[1];
    Cmd cmd{Cmd::BindMultisampleState};
    if (state) {
        thread_local bridge84::Cache<24, uint32_t> cache;
        cmd.n = bridge84::cached(cache, reinterpret_cast<const uint8_t*>(state), 24, [&]() -> uint32_t {
            uint64_t fn = real_proc(c, "nvnMultisampleStateGetAlphaToCoverageEnable");
            return fn ? uint32_t(native_state_get_at(c,21,fn,{state}) & 1) : 0u;
        }, 7);
    }
    log_state("multisample", cmd.n);
    record(c.x[0], cmd);
}

