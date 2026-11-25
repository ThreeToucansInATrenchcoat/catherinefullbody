#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK MovieMutex;
typedef CONDITION_VARIABLE MovieCondition;
#define MOVIE_MUTEX_INITIALIZER SRWLOCK_INIT
static void movie_lock(MovieMutex *lock) { AcquireSRWLockExclusive(lock); }
static void movie_unlock(MovieMutex *lock) { ReleaseSRWLockExclusive(lock); }
static void movie_wait(MovieCondition *event, MovieMutex *lock) {
    SleepConditionVariableSRW(event, lock, INFINITE, 0);
}
static void movie_signal(MovieCondition *event) { WakeConditionVariable(event); }
#else
#include <errno.h>
#include <pthread.h>
typedef pthread_mutex_t MovieMutex;
typedef pthread_cond_t MovieCondition;
#define MOVIE_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define E_INVALIDARG (-EINVAL)
#define E_OUTOFMEMORY (-ENOMEM)
#define ARRAYSIZE(array) (sizeof(array) / sizeof((array)[0]))
static void movie_lock(MovieMutex *lock) { pthread_mutex_lock(lock); }
static void movie_unlock(MovieMutex *lock) { pthread_mutex_unlock(lock); }
static void movie_wait(MovieCondition *event, MovieMutex *lock) { pthread_cond_wait(event, lock); }
static void movie_signal(MovieCondition *event) { pthread_cond_signal(event); }
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "movie_decoder.h"
#include "movie_guest.h"
#ifdef CATHERINE_HIGH_REFRESH_BUILD
#include "catherine_refresh.h"
#include "movie_interpolation.h"
#endif

extern void guest_call_original(void *context, uint64_t address);

enum { INPUT_COUNT = 4, OUTPUT_COUNT = 8, INPUT_DEFAULT = 512 * 1024, INPUT_LIMIT = 8 * 1024 * 1024 };
enum { ST_OK = 0, ST_ERROR = 0x1000, ST_NULL = 0x100b, ST_NO_OUTPUT = 0x100e, ST_NO_INPUT = 0x1011,
       ST_UNSUPPORTED = 0x2001, ST_NOT_CONFIGURED = 0x2002 };
enum { MOVIE_EOS = 4, EV_INPUT = 1, EV_OUTPUT = 2, EV_FORMAT = 4, EV_ERROR = 8 };
enum SlotState { SLOT_FREE, SLOT_HELD, SLOT_QUEUED };
enum Command { COMMAND_IDLE, COMMAND_OPEN, COMMAND_PUMP, COMMAND_FLUSH, COMMAND_CLOSE, COMMAND_QUIT };

typedef struct {
    enum SlotState state;
    uint32_t offset, length, flags;
    int64_t pts_us;
} Input;

typedef struct {
    enum SlotState state;
    CatherineMovieFrame frame;
    uint8_t *bytes;
    size_t capacity;
    uint32_t flags;
} Output;

typedef struct Movie {
    struct Movie *next;
    uint64_t object, events[4];
#ifdef _WIN32
    HANDLE worker;
#else
    pthread_t worker;
    bool worker_started;
#endif
    MovieMutex lock;
    MovieCondition work, complete;
    enum Command command;
    CatherineMovieDecoder *decoder;
    Input input[INPUT_COUNT];
    Output output[OUTPUT_COUNT];
    unsigned queued[INPUT_COUNT], ready[OUTPUT_COUNT];
    unsigned queued_count, ready_count, owed;
    uint32_t input_capacity, width, height, last_error;
    uint8_t *input_bytes, *scratch;
    size_t scratch_capacity, headers_size;
    uint8_t headers[4096];
    unsigned nal_length;
    CatherineMovieFrame format;
    unsigned trace_calls, trace_packets, trace_frames;
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    uint64_t interpolation_generation, native_ordinal;
#endif
    bool trace;
    bool configured, started, receiving, eos_queued, finished, need_headers, announced_format;
} Movie;

static Movie *movies;
static MovieMutex movies_lock = MOVIE_MUTEX_INITIALIZER;

#define GUEST_VPTR UINT64_C(0x7004dc8308)
#define GUEST_CTOR UINT64_C(0x7004820db0)
#define GUEST_D2 UINT64_C(0x7004820e30)
#define GUEST_D0 UINT64_C(0x7004820e80)
#define GUEST_FAILURE UINT64_MAX
#define GUEST_PTR(p) ((uint64_t)(uintptr_t)(p))

static uint64_t call_guest(Cpu *cpu, uint64_t address, const uint64_t args[6]) {
    Cpu call = *cpu;
    memcpy(call.x, args, sizeof(uint64_t) * 6);
    guest_call_original(&call, address);
    return call.x[0];
}

#define CALL(cpu, address, ...) call_guest(cpu, address, (const uint64_t[6]){__VA_ARGS__})

static void movie_error(Movie *movie, const char *operation, int32_t detail) {
    if (!movie->last_error)
        fprintf(stderr, "Catherine H.264 %s failed (%#x)\n", operation, (unsigned)detail);
    movie->last_error = ST_ERROR;
    movie->owed |= EV_ERROR;
}

static bool append_nal(uint8_t *out, size_t capacity, size_t *used,
                       const uint8_t *nal, size_t length) {
    static const uint8_t start[4] = {0, 0, 0, 1};
    if (!length || capacity - *used < sizeof start || capacity - *used - sizeof start < length)
        return false;
    memcpy(out + *used, start, sizeof start);
    *used += sizeof start;
    memcpy(out + *used, nal, length);
    *used += length;
    return true;
}

static bool read_avcc(Movie *movie, const uint8_t *data, size_t length) {
    movie->headers_size = 0;
    movie->nal_length = 4;
    if (!data || !length) return true;
    if (length < 7 || data[0] != 1) return false;
    movie->nal_length = (data[4] & 3) + 1;
    unsigned count = data[5] & 31;
    size_t offset = 6;
    for (unsigned group = 0; group < 2; ++group) {
        if (!count) return false;
        for (unsigned i = 0; i < count; ++i) {
            if (length - offset < 2) return false;
            size_t size = ((unsigned)data[offset] << 8) | data[offset + 1];
            offset += 2;
            if (size > length - offset || !append_nal(movie->headers, sizeof movie->headers,
                                                      &movie->headers_size, data + offset, size)) return false;
            offset += size;
        }
        if (group == 0) {
            if (offset == length) return false;
            count = data[offset++];
        }
    }
    return true;
}

static bool annex_b(const uint8_t *data, size_t length) {
    size_t offset = 0;
    while (offset < length && data[offset] == 0) ++offset;
    return offset >= 2 && offset + 1 < length && data[offset] == 1;
}

static bool prepare_packet(Movie *movie, const uint8_t **data, uint32_t *length) {
    size_t prefix = movie->need_headers ? movie->headers_size : 0;
    bool is_annex_b = annex_b(*data, *length);
    if (is_annex_b && !prefix) return true;
    size_t capacity = prefix + (is_annex_b ? *length : (size_t)*length * 3);
    if (movie->scratch_capacity < capacity) {
        uint8_t *scratch = realloc(movie->scratch, capacity);
        if (!scratch) return false;
        movie->scratch = scratch;
        movie->scratch_capacity = capacity;
    }
    memcpy(movie->scratch, movie->headers, prefix);
    size_t used = prefix;
    if (is_annex_b) {
        memcpy(movie->scratch + used, *data, *length);
        used += *length;
    } else {
        size_t offset = 0;
        while (offset < *length) {
            if (*length - offset < movie->nal_length) return false;
            uint32_t size = 0;
            for (unsigned i = 0; i < movie->nal_length; ++i) size = (size << 8) | (*data)[offset++];
            if (size > *length - offset || !append_nal(movie->scratch, capacity, &used, *data + offset, size))
                return false;
            offset += size;
        }
    }
    *data = movie->scratch;
    *length = (uint32_t)used;
    return true;
}

static void clear_queues(Movie *movie) {
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    catherine_movie_interpolation_end(movie->interpolation_generation);
    movie->interpolation_generation = movie->native_ordinal = 0;
#endif
    for (unsigned i = 0; i < INPUT_COUNT; ++i) movie->input[i].state = SLOT_FREE;
    for (unsigned i = 0; i < OUTPUT_COUNT; ++i) movie->output[i].state = SLOT_FREE;
    movie->queued_count = movie->ready_count = movie->owed = 0;
    movie->receiving = movie->eos_queued = movie->finished = movie->announced_format = false;
    movie->need_headers = true;
}

static void close_decoder(Movie *movie) {
    catherine_movie_destroy(movie->decoder);
    movie->decoder = NULL;
    movie->configured = movie->started = false;
    clear_queues(movie);
}

static bool publish_frame(Movie *movie, unsigned index, const CatherineMovieFrame *frame) {
    Output *output = &movie->output[index];
    if (frame) {
        if (output->capacity < frame->size) {
            uint8_t *bytes = realloc(output->bytes, frame->size);
            if (!bytes) return false;
            output->bytes = bytes;
            output->capacity = frame->size;
        }
        memcpy(output->bytes, frame->data, frame->size);
        output->frame = *frame;
        output->frame.data = output->bytes;
        output->flags = 0;
        if (!movie->announced_format || movie->format.width != frame->width || movie->format.height != frame->height) {
            movie->announced_format = true;
            movie->owed |= EV_FORMAT;
        }
        movie->format = output->frame;
#ifdef CATHERINE_HIGH_REFRESH_BUILD
        const uint64_t ordinal = movie->native_ordinal++;
        const int target = catherine_requested_movie_interpolation();
        if (target && catherine_fullscreen_movie_active()) {
            if (!movie->interpolation_generation)
                movie->interpolation_generation = catherine_movie_interpolation_generation();
            catherine_movie_interpolation_decoded(movie->interpolation_generation, ordinal, frame, target);
        }
#endif
    } else {
        output->frame = movie->format;
        output->frame.size = 0;
        output->frame.has_pts = 0;
        output->flags = MOVIE_EOS;
    }
    output->state = SLOT_QUEUED;
    movie->ready[movie->ready_count++] = index;
    movie->owed |= EV_OUTPUT;
    if (movie->trace && (++movie->trace_frames <= 12 || movie->trace_frames % 120 == 0 || !frame)) {
        fprintf(stderr, "Movie %#llx output %u slot=%u %ux%u bytes=%zu pts=%lld flags=%#x ready=%u\n",
                (unsigned long long)movie->object, movie->trace_frames, index,
                output->frame.width, output->frame.height, output->frame.size,
                (long long)output->frame.pts_100ns, output->flags, movie->ready_count);
    }
    return true;
}

static int submit_packet(Movie *movie, unsigned index) {
    Input *input = &movie->input[index];
    const uint8_t *data = movie->input_bytes + (size_t)index * movie->input_capacity + input->offset;
    uint32_t length = input->length;
    if (movie->trace && (++movie->trace_packets <= 12 || movie->trace_packets % 120 == 0 || input->flags)) {
        fprintf(stderr, "Movie %#llx packet %u slot=%u bytes=%u pts=%lld flags=%#x annexb=%d\n",
                (unsigned long long)movie->object, movie->trace_packets, index, length,
                (long long)input->pts_us, input->flags, annex_b(data, length));
    }
    if (length) {
        if (input->pts_us > INT64_MAX / 10 || input->pts_us < INT64_MIN / 10 ||
            !prepare_packet(movie, &data, &length)) {
            movie_error(movie, "packet assembly", E_INVALIDARG);
            return CATHERINE_MOVIE_ERROR;
        }
        int status = catherine_movie_submit(movie->decoder, data, length, input->pts_us * 10);
        if (status != CATHERINE_MOVIE_OK) return status;
        movie->need_headers = false;
        // If drain must wait for output, retry only the drain, not this packet.
        input->length = 0;
    }
    return input->flags & MOVIE_EOS ? catherine_movie_drain(movie->decoder) : CATHERINE_MOVIE_OK;
}

static void pump_decoder(Movie *movie) {
    while (movie->started && !movie->finished && !movie->last_error) {
        unsigned index = 0;
        while (index < OUTPUT_COUNT && movie->output[index].state != SLOT_FREE) ++index;
        if (index == OUTPUT_COUNT) return;
        if (movie->receiving) {
            CatherineMovieFrame frame;
            int result = catherine_movie_receive(movie->decoder, &frame);
            if (result == CATHERINE_MOVIE_OK || result == CATHERINE_MOVIE_END) {
                movie->finished = result == CATHERINE_MOVIE_END;
                if (!publish_frame(movie, index, movie->finished ? NULL : &frame))
                    movie_error(movie, "frame allocation", E_OUTOFMEMORY);
                continue;
            }
            if (result != CATHERINE_MOVIE_AGAIN) {
                movie_error(movie, "receive", catherine_movie_last_error(movie->decoder));
                return;
            }
            movie->receiving = false;
        }
        if (!movie->queued_count) return;
        index = movie->queued[0];
        int result = submit_packet(movie, index);
        if (result == CATHERINE_MOVIE_ERROR) {
            movie_error(movie, "submit", catherine_movie_last_error(movie->decoder));
            return;
        }
        if (result == CATHERINE_MOVIE_OK) {
            memmove(movie->queued, movie->queued + 1, --movie->queued_count * sizeof movie->queued[0]);
            movie->input[index].state = SLOT_FREE;
            movie->owed |= EV_INPUT;
        }
        movie->receiving = true;
    }
}

#ifdef _WIN32
static DWORD WINAPI
#else
static void *
#endif
decoder_worker(void *argument) {
    Movie *movie = argument;
    movie_lock(&movie->lock);
    for (;;) {
        while (movie->command == COMMAND_IDLE)
            movie_wait(&movie->work, &movie->lock);
        enum Command command = movie->command;
        if (command == COMMAND_OPEN) {
            close_decoder(movie);
            int32_t error;
            movie->decoder = catherine_movie_create(movie->width, movie->height, 0, 0, &error);
            movie->configured = movie->decoder != NULL;
            if (!movie->configured) movie_error(movie, "initialization", error);
        } else if (command == COMMAND_PUMP) {
            pump_decoder(movie);
        } else if (command == COMMAND_FLUSH) {
            clear_queues(movie);
            movie->last_error = 0;
            if (movie->decoder && catherine_movie_flush(movie->decoder) != CATHERINE_MOVIE_OK)
                movie_error(movie, "flush", catherine_movie_last_error(movie->decoder));
            if (movie->started) movie->owed |= EV_INPUT;
        } else {
            close_decoder(movie);
        }
        movie->command = COMMAND_IDLE;
        movie_signal(&movie->complete);
        if (command == COMMAND_QUIT) break;
    }
    movie_unlock(&movie->lock);
    return 0;
}

static void worker_call(Movie *movie, enum Command command) {
    movie_lock(&movie->lock);
    movie->command = command;
    movie_signal(&movie->work);
    while (movie->command != COMMAND_IDLE)
        movie_wait(&movie->complete, &movie->lock);
    movie_unlock(&movie->lock);
}

static int configure(Cpu *cpu, Movie *movie, uint64_t md) {
    if (!md) return ST_NULL;
    const char *mime = NULL;
    int width = 0, height = 0, capacity = 0;
    uint64_t avcc = 0;
    CALL(cpu, 0x7004823f90ull, md, GUEST_PTR("mime"), GUEST_PTR(&mime));
    CALL(cpu, 0x7004823ea0ull, md, GUEST_PTR("width"), GUEST_PTR(&width));
    CALL(cpu, 0x7004823ea0ull, md, GUEST_PTR("height"), GUEST_PTR(&height));
    CALL(cpu, 0x7004823ea0ull, md, GUEST_PTR("max-input-size"), GUEST_PTR(&capacity));
    CALL(cpu, 0x7004823fa0ull, md, GUEST_PTR("avcc"), GUEST_PTR(&avcc));
    const uint8_t *data = avcc ? (const void *)(uintptr_t)CALL(cpu, 0x7004823b50ull, avcc) : NULL;
    size_t size = avcc ? CALL(cpu, 0x7004823b70ull, avcc) : 0;
    if (!mime || strcmp(mime, "video/avc") || width < 2 || width > 4096 || height < 2 || height > 2304 ||
        ((width | height) & 1) || capacity > INPUT_LIMIT) return ST_UNSUPPORTED;
#ifdef _WIN32
    if (!movie->worker) return ST_ERROR;
#else
    if (!movie->worker_started) return ST_ERROR;
#endif
    worker_call(movie, COMMAND_CLOSE);
    if (!read_avcc(movie, data, size)) return ST_UNSUPPORTED;
    if (capacity < INPUT_DEFAULT) capacity = INPUT_DEFAULT;
    uint8_t *input = realloc(movie->input_bytes, (size_t)capacity * INPUT_COUNT);
    if (!input) return ST_ERROR;
    movie->input_bytes = input;
    movie->input_capacity = (uint32_t)capacity;
    movie->width = (uint32_t)width;
    movie->height = (uint32_t)height;
    movie->format = (CatherineMovieFrame){ .width = width, .height = height,
        .display_width = width, .display_height = height, .size = (size_t)width * height * 3 / 2 };
    movie->last_error = 0;
    worker_call(movie, COMMAND_OPEN);
    if (movie->trace) {
        fprintf(stderr, "Movie %#llx configure %dx%d input=%d AVCC=%zu SPS/PPS=%zu NAL-width=%u result=%d\n",
                (unsigned long long)movie->object, width, height, capacity, size, movie->headers_size,
                movie->nal_length, movie->configured);
    }
    return movie->configured ? ST_OK : ST_ERROR;
}

static void set_format(Cpu *cpu, uint64_t md, const CatherineMovieFrame *format) {
    uint32_t width = format->width, height = format->height;
    uint32_t left = format->display_x, top = format->display_y;
    CALL(cpu, 0x7004823f50ull, md, GUEST_PTR("mime"), GUEST_PTR("video/raw"));
    CALL(cpu, 0x7004823f70ull, md, GUEST_PTR("crop"), left, top,
         left + format->display_width - 1, top + format->display_height - 1);
    struct { const char *name; uint32_t value; } values[] = {
        {"width", width}, {"height", height}, {"stride", width}, {"slice-height", height}, {"color-format", 21},
        {"crop-width", format->display_width}, {"crop-height", format->display_height},
        {"nv12-y-stride", width}, {"nv12-uv-stride", width}, {"nv12-y-offset", 0}, {"nv12-uv-offset", width * height}
    };
    for (unsigned i = 0; i < ARRAYSIZE(values); ++i)
        CALL(cpu, 0x7004823e90ull, md, GUEST_PTR(values[i].name), values[i].value);
}

static int acquire_input(Movie *movie, int *index, int *remaining) {
    if (!index || !remaining) return ST_NULL;
    *index = -1;
    *remaining = 0;
    if (!movie->started || movie->last_error || movie->finished) return ST_NO_INPUT;
    for (unsigned i = 0; i < INPUT_COUNT; ++i) {
        if (movie->input[i].state != SLOT_FREE) continue;
        if (*index < 0) {
            *index = (int)i;
            movie->input[i].state = SLOT_HELD;
        } else ++*remaining;
    }
    return *index >= 0 ? ST_OK : ST_NO_INPUT;
}

static int acquire_output(Movie *movie, int *index, int64_t *pts, uint32_t *flags, int *remaining) {
    if (!index || !pts || !flags || !remaining) return ST_NULL;
    *index = -1;
    *pts = 0;
    *flags = 0;
    *remaining = 0;
    if (!movie->ready_count) return ST_NO_OUTPUT;
    *index = (int)movie->ready[0];
    memmove(movie->ready, movie->ready + 1, --movie->ready_count * sizeof movie->ready[0]);
    Output *output = &movie->output[*index];
    output->state = SLOT_HELD;
    if (output->frame.has_pts) *pts = output->frame.pts_100ns / 10;
    *flags = output->flags;
    *remaining = (int)movie->ready_count;
    return ST_OK;
}

static int send_input(Cpu *cpu, Movie *movie) {
    uint32_t index = (uint32_t)cpu->x[1], offset = (uint32_t)cpu->x[2], length = (uint32_t)cpu->x[3];
    uint32_t flags = (uint32_t)cpu->x[5];
    if (!movie->started || movie->last_error || index >= INPUT_COUNT || movie->input[index].state != SLOT_HELD ||
        offset > movie->input_capacity || length > movie->input_capacity - offset) return ST_ERROR;
    if (movie->eos_queued) {
        movie->input[index].state = SLOT_FREE;
        movie->owed |= EV_INPUT;
        return ST_OK;
    }
    movie->input[index] = (Input){SLOT_QUEUED, offset, length, flags, (int64_t)cpu->x[4]};
    movie->queued[movie->queued_count++] = index;
    movie->eos_queued = (flags & MOVIE_EOS) != 0;
    worker_call(movie, COMMAND_PUMP);
    return movie->last_error ? ST_ERROR : ST_OK;
}

static int get_output(Cpu *cpu, Output *output, uint64_t buffer) {
    if (!buffer) return ST_NULL;
    CALL(cpu, 0x7004823b30ull, buffer, 0, 0);
    if (!output->frame.size) return ST_OK;
    uint64_t data = CALL(cpu, 0x7004823b40ull, buffer);
    uint64_t capacity = CALL(cpu, 0x7004823b60ull, buffer);
    if (!data || capacity < output->frame.size) return ST_ERROR;
    memcpy((void *)(uintptr_t)data, output->bytes, output->frame.size);
    CALL(cpu, 0x7004823b30ull, buffer, 0, output->frame.size);
    return ST_OK;
}

static int decoder_call(Cpu *cpu, Movie *movie, uint64_t address) {
    uint64_t a = cpu->x[1], b = cpu->x[2];
    uint32_t index = (uint32_t)a;
    Output *output = index < OUTPUT_COUNT && movie->output[index].state == SLOT_HELD ? &movie->output[index] : NULL;
    switch (address) {
    case 0x7004820ed0ull: return configure(cpu, movie, a);
    case 0x7004820ef0ull:
        if (a) memcpy(movie->events, (const void *)(uintptr_t)a, sizeof movie->events);
        else memset(movie->events, 0, sizeof movie->events);
        return ST_OK;
    case 0x7004820fd0ull:
        if (!a) return ST_NULL;
        if (!movie->configured) return ST_NOT_CONFIGURED;
        set_format(cpu, a, &movie->format);
        return ST_OK;
    case 0x7004820f10ull:
        if (!movie->configured) return ST_NOT_CONFIGURED;
        if (movie->last_error) return ST_ERROR;
        movie->started = true;
        movie->owed |= EV_INPUT;
        return ST_OK;
    case 0x7004820f30ull:
    case 0x7004820f50ull:
        if (movie->worker) worker_call(movie, COMMAND_CLOSE);
        if (address == 0x7004820f50ull) movie->last_error = 0;
        return ST_OK;
    case 0x7004820f70ull:
        if (movie->configured) worker_call(movie, COMMAND_FLUSH);
        return movie->last_error ? ST_ERROR : ST_OK;
    case 0x7004821050ull: return acquire_input(movie, (int *)(uintptr_t)a, (int *)(uintptr_t)b);
    case 0x7004821070ull:
        if (!b) return ST_NULL;
        if (index >= INPUT_COUNT || movie->input[index].state != SLOT_HELD) return ST_ERROR;
        CALL(cpu, 0x7004823c40ull, b, GUEST_PTR(movie->input_bytes + (size_t)index * movie->input_capacity),
             movie->input_capacity);
        CALL(cpu, 0x7004823b30ull, b, 0, movie->input_capacity);
        return ST_OK;
    case 0x7004820f90ull: return send_input(cpu, movie);
    case 0x7004820ff0ull:
        return acquire_output(movie, (int *)(uintptr_t)a, (int64_t *)(uintptr_t)b,
                              (uint32_t *)(uintptr_t)cpu->x[3], (int *)(uintptr_t)cpu->x[4]);
    case 0x7004821030ull: return output ? get_output(cpu, output, b) : ST_ERROR;
    case 0x7004821010ull:
        if (!a || !b) return ST_NULL;
        *(int *)(uintptr_t)a = movie->configured ? (int)movie->format.size : 0;
        *(int *)(uintptr_t)b = 1;
        return ST_OK;
    case 0x7004820fb0ull:
        if (!output) return ST_ERROR;
        output->state = SLOT_FREE;
        if (movie->started) worker_call(movie, COMMAND_PUMP);
        return ST_OK;
    case 0x7004821090ull: return (int)movie->last_error;
    case 0x70048210b0ull:
        if (!output) return ST_ERROR;
        if (!cpu->x[3]) return ST_NULL;
        set_format(cpu, cpu->x[3], &output->frame);
        return get_output(cpu, output, b);
    case 0x70048210d0ull:
        if (!output) return ST_ERROR;
        if (!b) return ST_NULL;
        set_format(cpu, b, &output->frame);
        return ST_OK;
    default: return ST_UNSUPPORTED;
    }
}

static void destroy(Movie *movie) {
#ifdef _WIN32
    if (movie->worker) {
        worker_call(movie, COMMAND_QUIT);
        WaitForSingleObject(movie->worker, INFINITE);
        CloseHandle(movie->worker);
    }
#else
    if (movie->worker_started) {
        worker_call(movie, COMMAND_QUIT);
        pthread_join(movie->worker, NULL);
    }
    pthread_cond_destroy(&movie->complete);
    pthread_cond_destroy(&movie->work);
    pthread_mutex_destroy(&movie->lock);
#endif
    for (unsigned i = 0; i < OUTPUT_COUNT; ++i) free(movie->output[i].bytes);
    free(movie->scratch);
    free(movie->input_bytes);
    free(movie);
}

static bool decoder_method(uint64_t address) {
    switch (address) {
    case GUEST_D2: case GUEST_D0: case 0x7004820ed0ull: case 0x7004820ef0ull: case 0x7004820fd0ull:
    case 0x7004820f10ull: case 0x7004820f30ull: case 0x7004820f50ull: case 0x7004820f70ull:
    case 0x7004821050ull: case 0x7004821070ull: case 0x7004820f90ull: case 0x7004820ff0ull:
    case 0x7004821030ull: case 0x7004821010ull: case 0x7004820fb0ull: case 0x7004821090ull:
    case 0x70048210b0ull: case 0x70048210d0ull: return true;
    default: return false;
    }
}

int catherine_movie_guest_dispatch(Cpu *cpu) {
    const uint64_t address = cpu->pc, object = cpu->x[0];
    if (address == GUEST_CTOR) {
#ifdef _WIN32
        const bool trace = GetEnvironmentVariableW(L"CATHERINE_TRACE_MOVIE", NULL, 0) != 0;
#else
        const bool trace = getenv("CATHERINE_TRACE_MOVIE") != NULL;
#endif
        if (trace) {
            fprintf(stderr, "Movie %#llx constructor lr=%#llx type=%u format=%u mode=%u priority=%u\n",
                    (unsigned long long)object, (unsigned long long)cpu->x[30], (unsigned)cpu->x[1],
                    (unsigned)cpu->x[2], (unsigned)cpu->x[3], (unsigned)cpu->x[4]);
        }
        if (!object || cpu->x[1] != 1 || cpu->x[2] != 4 || cpu->x[3] != 1) return 0;
        uint64_t *guest = (void *)(uintptr_t)object;
        guest[0] = GUEST_VPTR;
        guest[1] = GUEST_FAILURE;
        Movie *movie = calloc(1, sizeof *movie);
        if (!movie) return 1;
        movie->object = object;
        movie->trace = trace;
#ifdef _WIN32
        InitializeSRWLock(&movie->lock);
        InitializeConditionVariable(&movie->work);
        InitializeConditionVariable(&movie->complete);
        movie->worker = CreateThread(NULL, 0, decoder_worker, movie, 0, NULL);
#else
        if (pthread_mutex_init(&movie->lock, NULL) != 0) {
            free(movie);
            return 1;
        }
        if (pthread_cond_init(&movie->work, NULL) != 0) {
            pthread_mutex_destroy(&movie->lock);
            free(movie);
            return 1;
        }
        if (pthread_cond_init(&movie->complete, NULL) != 0) {
            pthread_cond_destroy(&movie->work);
            pthread_mutex_destroy(&movie->lock);
            free(movie);
            return 1;
        }
        movie->worker_started = pthread_create(&movie->worker, NULL, decoder_worker, movie) == 0;
#endif
        movie_lock(&movies_lock);
        movie->next = movies;
        movies = movie;
        movie_unlock(&movies_lock);
        return 1;
    }
    if (!object || !decoder_method(address)) return 0;
    movie_lock(&movies_lock);
    Movie **slot = &movies;
    while (*slot && (*slot)->object != object) slot = &(*slot)->next;
    Movie *movie = *slot;
    uint64_t *guest = (void *)(uintptr_t)object;
    if (!movie && (guest[0] != GUEST_VPTR || guest[1] != GUEST_FAILURE)) {
        movie_unlock(&movies_lock);
        return 0;
    }
    if (address == GUEST_D0 || address == GUEST_D2) {
        if (movie) *slot = movie->next;
        guest[1] = 0;
        movie_unlock(&movies_lock);
        if (movie) destroy(movie);
        guest_call_original(cpu, address);
        return 1;
    }
    uint64_t events[4] = {0};
    if (movie) {
        unsigned call = movie->trace ? ++movie->trace_calls : 0;
        cpu->x[0] = (uint32_t)decoder_call(cpu, movie, address);
        if (movie->trace && (call <= 140 || call % 240 == 0 ||
                (cpu->x[0] && cpu->x[0] != ST_NO_INPUT && cpu->x[0] != ST_NO_OUTPUT))) {
            fprintf(stderr, "Movie %#llx call %u fn=%#llx lr=%#llx status=%#x events=%#x pending=%u ready=%u\n",
                    (unsigned long long)object, call, (unsigned long long)address, (unsigned long long)cpu->x[30],
                    (unsigned)cpu->x[0], movie->owed, movie->queued_count, movie->ready_count);
        }
        for (unsigned i = 0; i < ARRAYSIZE(events); ++i)
            if (movie->owed & (1u << i)) events[i] = movie->events[i];
        movie->owed = 0;
    } else cpu->x[0] = ST_ERROR;
    movie_unlock(&movies_lock);
    for (unsigned i = 0; i < ARRAYSIZE(events); ++i)
        if (events[i]) CALL(cpu, 0x700532e770ull, events[i]);
    return 1;
}
