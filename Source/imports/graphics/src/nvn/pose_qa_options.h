#pragma once
#include <cstdint>
#if defined(CATHERINE_POSE_DIAGNOSTICS)
#include <cstdio>
#include <cstdlib>
#endif

namespace catherine::visual {
inline constexpr uint32_t PoseQaNoHead = 1u;
inline constexpr uint32_t PoseQaNoGenericC3 = 2u;
inline constexpr uint32_t PoseQaNoSkeleton = 4u;
inline constexpr uint32_t PoseQaNoCpuSkin = 8u;
inline constexpr uint32_t PoseQaNoCpuMorph = 16u;
inline constexpr uint32_t PoseQaLinearPalette = 32u;
inline constexpr uint32_t PoseQaLegacyPalette = 128u;

// Diagnostic builds can isolate one pose path without restarting the game.
// One read per rendered frame keeps every draw/dispatch on the same mask.
// Production builds perform no file access and always retain normal behavior.
inline uint32_t pose_qa_options(uint64_t frame) {
#if defined(CATHERINE_POSE_DIAGNOSTICS)
    static uint64_t sampled_frame = ~0ull;
    static uint32_t options = 0;
    if (sampled_frame != frame) {
        sampled_frame = frame;
        options = 0;
        std::FILE* file = std::fopen("pose-options.txt", "rb");
        if (!file) file = std::fopen("pose-options", "rb");
        if (file) {
            char text[64] = {};
            if (std::fgets(text, sizeof text, file)) {
                char* end = nullptr;
                const unsigned long long mask = std::strtoull(text, &end, 0);
                if (end != text && mask <= UINT32_MAX) {
                    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
                    if (!*end) options = static_cast<uint32_t>(mask);
                }
            }
            std::fclose(file);
        }
    }
    return options;
#else
    (void)frame;
    return 0;
#endif
}
} // namespace catherine::visual
