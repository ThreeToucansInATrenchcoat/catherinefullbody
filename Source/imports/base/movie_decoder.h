#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CatherineMovieDecoder CatherineMovieDecoder;

enum CatherineMovieResult {
    CATHERINE_MOVIE_ERROR = -1,
    CATHERINE_MOVIE_OK = 0,
    CATHERINE_MOVIE_AGAIN = 1,
    CATHERINE_MOVIE_END = 2
};

typedef struct CatherineMovieFrame {
    const uint8_t *data; /* Packed top-down NV12, Y followed by interleaved UV. */
    size_t size;
    uint32_t width, height;
    uint32_t display_x, display_y, display_width, display_height;
    int64_t pts_100ns;
    int has_pts;
} CatherineMovieFrame;

/* Call on one host worker thread for the full lifetime of a decoder. Width and
   height, and frame rate numerator and denominator, are optional pairs. */
CatherineMovieDecoder *catherine_movie_create(uint32_t width, uint32_t height,
    uint32_t rate_num, uint32_t rate_den, int32_t *error);
void catherine_movie_destroy(CatherineMovieDecoder *decoder);

/* Submit one Annex-B access unit. Include SPS/PPS before the first picture.
   If AGAIN is returned, receive output and retry the same unit. */
int catherine_movie_submit(CatherineMovieDecoder *decoder, const void *data,
    uint32_t size, int64_t pts_100ns);
/* A returned frame is valid until the next receive, flush, or destroy.
   AGAIN means more input is needed. END means a drain has completed. */
int catherine_movie_receive(CatherineMovieDecoder *decoder, CatherineMovieFrame *frame);
int catherine_movie_drain(CatherineMovieDecoder *decoder);
int catherine_movie_flush(CatherineMovieDecoder *decoder);
int32_t catherine_movie_last_error(const CatherineMovieDecoder *decoder);

/* Optional interpolation worker. Inputs are packed top-down NV12 and outputs
   are packed BGRA8. A generation separates movies or seeks, and right_frame
   identifies one adjacent pair. expires_qpc uses QueryPerformanceCounter. */
void catherine_interpolation_worker_warmup(void);
int catherine_interpolation_worker_available(void);
void catherine_interpolation_worker_reset(uint64_t generation);
int catherine_interpolation_worker_submit(uint64_t generation, uint64_t right_frame,
    uint32_t width, uint32_t height, const uint8_t *left, const uint8_t *right,
    unsigned intervals, uint64_t expires_qpc);
int catherine_interpolation_worker_take(uint64_t generation, uint64_t right_frame,
    unsigned intervals, unsigned phase, uint8_t *output, size_t size);
int catherine_interpolation_worker_has(uint64_t generation, uint64_t right_frame,
    unsigned intervals, unsigned phase);

#ifdef __cplusplus
}
#endif
