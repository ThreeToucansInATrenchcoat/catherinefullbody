#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CatherineMovieFrame;

typedef struct CatherineInterpolationPlan {
    uint64_t generation, right_frame;
    unsigned intervals, phase;
} CatherineInterpolationPlan;

uint64_t catherine_movie_interpolation_generation(void);
void catherine_movie_interpolation_decoded(uint64_t generation, uint64_t ordinal,
    const struct CatherineMovieFrame *frame, int target);
void catherine_movie_interpolation_end(uint64_t generation);
void catherine_movie_interpolation_display_upload(uint64_t movie, uint64_t info, uint64_t descriptor);
void catherine_movie_interpolation_display(uint64_t memory, uint32_t width, uint32_t height,
    uint32_t stride, uint64_t ordinal, int target);
int catherine_movie_interpolation_present_rate(void);
uint64_t catherine_movie_replay_present(void);
int catherine_movie_interpolation_texture(uint64_t memory, uint32_t width, uint32_t height,
    uint32_t stride, uint64_t present, CatherineInterpolationPlan *plan);
int catherine_movie_interpolation_ready(const CatherineInterpolationPlan *plan);
int catherine_movie_interpolation_copy(const CatherineInterpolationPlan *plan, uint8_t *pixels, size_t size,
    uint64_t present);

#ifdef __cplusplus
}
#endif
