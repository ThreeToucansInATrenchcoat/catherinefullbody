#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "movie_decoder.h"
#include "guest-timing/include/movie_interpolation.h"

using Microsoft::WRL::ComPtr;

struct CatherineMovieDecoder {
    ComPtr<IMFTransform> transform;
    std::unique_ptr<uint8_t[]> pixels;
    CatherineMovieFrame format{};
    MFT_OUTPUT_STREAM_INFO output{};
    uint32_t stride = 0;
    LONGLONG duration = 0;
    HRESULT error = S_OK;
    bool com_started = false;
    bool mf_started = false;
    bool draining = false;
    bool ended = false;

    ~CatherineMovieDecoder() {
        transform.Reset();
        if (mf_started) MFShutdown();
        if (com_started) CoUninitialize();
    }

    int fail(HRESULT hr) {
        error = hr;
        return CATHERINE_MOVIE_ERROR;
    }
};

static HRESULT select_output(CatherineMovieDecoder &decoder) {
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        HRESULT hr = decoder.transform->GetOutputAvailableType(0, index, &type);
        if (FAILED(hr)) return hr;
        GUID subtype;
        if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_NV12) continue;

        CatherineMovieFrame format{};
        hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &format.width, &format.height);
        if (FAILED(hr)) return hr;
        if (format.width < 2 || format.width > 4096 || format.height < 2 || format.height > 2304 ||
            ((format.width | format.height) & 1)) return MF_E_INVALIDMEDIATYPE;
        format.size = static_cast<size_t>(format.width) * format.height * 3 / 2;
        format.display_width = format.width;
        format.display_height = format.height;

        MFVideoArea aperture{};
        hr = type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8 *>(&aperture),
                           sizeof aperture, nullptr);
        if (FAILED(hr)) {
            hr = type->GetBlob(MF_MT_GEOMETRIC_APERTURE, reinterpret_cast<UINT8 *>(&aperture),
                               sizeof aperture, nullptr);
        }
        if (SUCCEEDED(hr) && aperture.OffsetX.value >= 0 && aperture.OffsetY.value >= 0 &&
            aperture.Area.cx > 0 && aperture.Area.cy > 0 &&
            aperture.Area.cx <= static_cast<LONG>(format.width) - aperture.OffsetX.value &&
            aperture.Area.cy <= static_cast<LONG>(format.height) - aperture.OffsetY.value) {
            format.display_x = aperture.OffsetX.value;
            format.display_y = aperture.OffsetY.value;
            format.display_width = aperture.Area.cx;
            format.display_height = aperture.Area.cy;
        }

        uint32_t stride;
        if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) stride = format.width;
        if (stride < format.width || stride > INT32_MAX) return MF_E_INVALIDMEDIATYPE;
        std::unique_ptr<uint8_t[]> pixels(new (std::nothrow) uint8_t[format.size]);
        if (!pixels) return E_OUTOFMEMORY;

        hr = decoder.transform->SetOutputType(0, type.Get(), 0);
        if (FAILED(hr)) return hr;
        hr = decoder.transform->GetOutputStreamInfo(0, &decoder.output);
        if (FAILED(hr)) return hr;
        decoder.stride = stride;
        decoder.pixels = std::move(pixels);
        decoder.format = format;
        return S_OK;
    }
}

static HRESULT open_decoder(CatherineMovieDecoder &decoder, uint32_t width, uint32_t height,
                            uint32_t rate_num, uint32_t rate_den) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;
    decoder.com_started = SUCCEEDED(hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) return hr;
    decoder.mf_started = true;
    hr = CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(decoder.transform.GetAddressOf()));
    if (FAILED(hr)) return hr;

    // The guest owns A/V scheduling. Avoid the transform's extra presentation queue.
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(decoder.transform->GetAttributes(&attributes)))
        attributes->SetUINT32(MF_LOW_LATENCY, TRUE);

    ComPtr<IMFMediaType> input;
    hr = MFCreateMediaType(&input);
    if (SUCCEEDED(hr)) hr = input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
    if (SUCCEEDED(hr) && width) hr = MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height);
    if (SUCCEEDED(hr) && rate_num) {
        hr = MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, rate_num, rate_den);
        decoder.duration = (10000000LL * rate_den) / rate_num;
    }
    if (SUCCEEDED(hr)) hr = decoder.transform->SetInputType(0, input.Get(), 0);
    if (SUCCEEDED(hr)) hr = select_output(decoder);
    if (SUCCEEDED(hr)) hr = decoder.transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (SUCCEEDED(hr)) hr = decoder.transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return hr;
}

CatherineMovieDecoder *catherine_movie_create(uint32_t width, uint32_t height,
    uint32_t rate_num, uint32_t rate_den, int32_t *error) {
    std::unique_ptr<CatherineMovieDecoder> decoder;
    HRESULT hr = E_INVALIDARG;
    if ((width == 0) == (height == 0) && (rate_num == 0) == (rate_den == 0)) {
        decoder.reset(new (std::nothrow) CatherineMovieDecoder);
        hr = decoder ? open_decoder(*decoder, width, height, rate_num, rate_den) : E_OUTOFMEMORY;
    }
    if (error) *error = hr;
    return SUCCEEDED(hr) ? decoder.release() : nullptr;
}

void catherine_movie_destroy(CatherineMovieDecoder *decoder) {
    delete decoder;
}

int catherine_movie_submit(CatherineMovieDecoder *decoder, const void *data, uint32_t size,
                           int64_t pts_100ns) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    if (!data || !size || decoder->draining) return decoder->fail(E_INVALIDARG);

    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> sample;
    HRESULT hr = MFCreateMemoryBuffer(size, &buffer);
    BYTE *bytes;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&bytes, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        std::memcpy(bytes, data, size);
        hr = buffer->Unlock();
    }
    if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(size);
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(pts_100ns);
    if (SUCCEEDED(hr) && decoder->duration > 0) hr = sample->SetSampleDuration(decoder->duration);
    if (SUCCEEDED(hr)) hr = decoder->transform->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) return CATHERINE_MOVIE_AGAIN;
    return SUCCEEDED(hr) ? CATHERINE_MOVIE_OK : decoder->fail(hr);
}

static HRESULT copy_frame(CatherineMovieDecoder &decoder, IMFSample *sample) {
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) return hr;

    ComPtr<IMF2DBuffer> buffer2d;
    DWORD length;
    if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->GetContiguousLength(&length)) &&
        length == decoder.format.size) return buffer2d->ContiguousCopyTo(decoder.pixels.get(), length);

    BYTE *bytes;
    hr = buffer->Lock(&bytes, nullptr, &length);
    if (FAILED(hr)) return hr;
    const size_t rows = decoder.format.height * 3 / 2;
    const size_t width = decoder.format.width;
    const size_t stride = decoder.stride;
    if (length < (rows - 1) * stride + width) {
        buffer->Unlock();
        return MF_E_BUFFERTOOSMALL;
    }
    for (size_t row = 0; row < rows; ++row)
        std::memcpy(decoder.pixels.get() + row * width, bytes + row * stride, width);
    return buffer->Unlock();
}

int catherine_movie_receive(CatherineMovieDecoder *decoder, CatherineMovieFrame *frame) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    if (!frame) return decoder->fail(E_POINTER);
    if (decoder->ended) return CATHERINE_MOVIE_END;
    for (;;) {
        ComPtr<IMFSample> sample;
        const auto &info = decoder->output;
        if (!(info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
            ComPtr<IMFMediaBuffer> buffer;
            DWORD size = info.cbSize ? info.cbSize : static_cast<DWORD>(decoder->format.size);
            DWORD alignment = info.cbAlignment ? info.cbAlignment - 1 : MF_1_BYTE_ALIGNMENT;
            HRESULT hr = MFCreateAlignedMemoryBuffer(size, alignment, &buffer);
            if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
            if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
            if (FAILED(hr)) return decoder->fail(hr);
        }

        MFT_OUTPUT_DATA_BUFFER output{};
        output.pSample = sample.Get();
        DWORD status;
        HRESULT hr = decoder->transform->ProcessOutput(0, 1, &output, &status);
        if (output.pEvents) output.pEvents->Release();
        if (output.pSample != sample.Get()) sample.Attach(output.pSample);
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            decoder->ended = decoder->draining;
            return decoder->ended ? CATHERINE_MOVIE_END : CATHERINE_MOVIE_AGAIN;
        }
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            hr = select_output(*decoder);
            if (FAILED(hr)) return decoder->fail(hr);
            continue;
        }
        if (FAILED(hr)) return decoder->fail(hr);
        if (!sample || (output.dwStatus & MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE) == MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE)
            continue;
        hr = copy_frame(*decoder, sample.Get());
        if (FAILED(hr)) return decoder->fail(hr);
        *frame = decoder->format;
        frame->data = decoder->pixels.get();
        frame->has_pts = SUCCEEDED(sample->GetSampleTime(&frame->pts_100ns));
        return CATHERINE_MOVIE_OK;
    }
}

int catherine_movie_drain(CatherineMovieDecoder *decoder) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    if (decoder->draining) return CATHERINE_MOVIE_OK;
    HRESULT hr = decoder->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    if (SUCCEEDED(hr)) hr = decoder->transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    if (FAILED(hr)) return decoder->fail(hr);
    decoder->draining = true;
    return CATHERINE_MOVIE_OK;
}

int catherine_movie_flush(CatherineMovieDecoder *decoder) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    HRESULT hr = decoder->transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    if (SUCCEEDED(hr)) hr = decoder->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) return decoder->fail(hr);
    decoder->draining = false;
    decoder->ended = false;
    return CATHERINE_MOVIE_OK;
}

int32_t catherine_movie_last_error(const CatherineMovieDecoder *decoder) {
    return decoder ? decoder->error : E_POINTER;
}

namespace {

constexpr unsigned kMaxInterpolationIntervals = 16;
constexpr unsigned kInterpolationPairs = 6;

struct InterpolationJob {
    uint64_t generation = 0, right_frame = 0, expires_qpc = 0;
    uint32_t width = 0, height = 0;
    unsigned intervals = 0;
    std::vector<uint8_t> left, right;
};

struct InterpolationImage {
    size_t size = 0;
    std::unique_ptr<uint8_t[]> pixels;
};

struct InterpolationPair {
    uint64_t right_frame = 0;
    unsigned intervals = 0;
    std::array<InterpolationImage, kMaxInterpolationIntervals> phases;
};

struct InterpolationWorker {
    std::atomic<bool> started{false}, available{false};
    std::mutex mutex;
    std::condition_variable work;
    std::unique_ptr<InterpolationJob> pending;
    uint64_t generation = 0, right_frame = 0;
    std::array<InterpolationPair, kInterpolationPairs> ready;
};

InterpolationWorker* interpolation_worker() {
    // The interpolation worker belongs to the process. Movie changes only invalidate jobs.
    static InterpolationWorker* worker = new (std::nothrow) InterpolationWorker;
    return worker;
}

bool interpolation_expired(const InterpolationJob& job) {
    LARGE_INTEGER now;
    return job.expires_qpc && QueryPerformanceCounter(&now) &&
           static_cast<uint64_t>(now.QuadPart) >= job.expires_qpc;
}

inline uint32_t interpolation_bgra(uint8_t y, uint8_t u, uint8_t v) {
    const auto clamp_byte = [](int value) { return uint32_t(std::clamp(value, 0, 255)); };
    const int luma = 298 * (int(y) - 16);
    const int cb = int(u) - 128;
    const int cr = int(v) - 128;
    return clamp_byte((luma + 516 * cb + 128) >> 8) |
           (clamp_byte((luma - 100 * cb - 208 * cr + 128) >> 8) << 8) |
           (clamp_byte((luma + 409 * cr + 128) >> 8) << 16) | 0xff000000u;
}

void interpolation_to_bgra(const uint8_t* nv12, uint8_t* bgra, uint32_t width, uint32_t height) {
    const uint8_t* uv = nv12 + size_t(width) * height;
    for (uint32_t y = 0; y < height; ++y) {
        const auto* y_row = nv12 + size_t(y) * width;
        const auto* uv_row = uv + size_t(y / 2) * width;
        auto* destination = bgra + size_t(y) * width * 4;
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t pixel = interpolation_bgra(y_row[x], uv_row[x & ~1u], uv_row[(x & ~1u) + 1]);
            std::memcpy(destination + x * 4, &pixel, sizeof pixel);
        }
    }
}

unsigned __stdcall run_interpolation_worker(void* argument) {
    auto& worker = *static_cast<InterpolationWorker*>(argument);
    wchar_t executable[32768];
    const DWORD count = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (!count || count >= ARRAYSIZE(executable)) return 0;
    const wchar_t* filename = std::wcsrchr(executable, L'\\');
    if (!filename) return 0;

    try {
        const std::wstring directory(executable, static_cast<size_t>(filename + 1 - executable));
        const std::wstring dll = directory + L"CatherineInterpolation.dll";
        HMODULE module = LoadLibraryExW(dll.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            std::fprintf(stderr, "Catherine interpolation backend unavailable (%lu)\n", GetLastError());
            return 0;
        }

        using Open = void* (*)();
        using Begin = int (*)(void*, const uint8_t*, const uint8_t*, uint32_t, uint32_t, unsigned);
        using Frame = int (*)(void*, unsigned, uint8_t*);
        using Close = void (*)(void*);
        auto open = reinterpret_cast<Open>(GetProcAddress(module, "catherine_interpolation_open"));
        auto begin = reinterpret_cast<Begin>(GetProcAddress(module, "catherine_interpolation_begin_nv12"));
        auto frame = reinterpret_cast<Frame>(GetProcAddress(module, "catherine_interpolation_frame_nv12"));
        auto close = reinterpret_cast<Close>(GetProcAddress(module, "catherine_interpolation_close"));
        if (!open || !begin || !frame || !close) {
            std::fprintf(stderr, "Catherine interpolation backend has an incompatible interface\n");
            FreeLibrary(module);
            return 0;
        }
        void* context = open();
        if (!context) {
            std::fprintf(stderr, "Catherine interpolation backend could not initialize\n");
            FreeLibrary(module);
            return 0;
        }

        std::fprintf(stderr, "Catherine interpolation backend ready\n");
        worker.available.store(true, std::memory_order_release);
        for (;;) {
            std::unique_ptr<InterpolationJob> job;
            {
                std::unique_lock lock(worker.mutex);
                worker.work.wait(lock, [&] { return worker.pending != nullptr; });
                job = std::move(worker.pending);
            }
            if (interpolation_expired(*job) || begin(context, job->left.data(), job->right.data(), job->width,
                                                    job->height, job->intervals) != 1) continue;
            try {
                std::vector<uint8_t> nv12(job->left.size());
                for (unsigned phase = 1; phase < job->intervals && !interpolation_expired(*job); ++phase) {
                    if (frame(context, phase, nv12.data()) != 1 || interpolation_expired(*job)) break;
                    // Scene cuts can return an exact endpoint. Let the native clock cut to B itself.
                    if (!std::memcmp(nv12.data(), job->left.data(), nv12.size()) ||
                        !std::memcmp(nv12.data(), job->right.data(), nv12.size())) continue;
                    const size_t size = size_t(job->width) * job->height * 4;
                    auto bgra = std::make_unique_for_overwrite<uint8_t[]>(size);
                    interpolation_to_bgra(nv12.data(), bgra.get(), job->width, job->height);
                    std::lock_guard lock(worker.mutex);
                    auto& pair = worker.ready[job->right_frame % kInterpolationPairs];
                    if (worker.generation != job->generation || pair.right_frame != job->right_frame) break;
                    pair.phases[phase] = {size, std::move(bgra)};
                }
            } catch (const std::bad_alloc&) {
            }
        }
    } catch (const std::bad_alloc&) {
        return 0;
    }
}

}  // namespace

void catherine_interpolation_worker_warmup(void) {
    auto* worker = interpolation_worker();
    if (!worker || worker->started.exchange(true, std::memory_order_acq_rel)) return;
    const uintptr_t thread = _beginthreadex(nullptr, 0, run_interpolation_worker, worker, 0, nullptr);
    if (thread) CloseHandle(reinterpret_cast<HANDLE>(thread));
    else worker->started.store(false, std::memory_order_release);
}

int catherine_interpolation_worker_available(void) {
    const auto* worker = interpolation_worker();
    return worker && worker->available.load(std::memory_order_acquire);
}

void catherine_interpolation_worker_reset(uint64_t generation) {
    auto* worker = interpolation_worker();
    if (!worker) return;
    std::lock_guard lock(worker->mutex);
    if (worker->generation != generation) return;
    worker->generation = worker->right_frame = 0;
    worker->pending.reset();
    for (auto& pair : worker->ready) pair = {};
}

int catherine_interpolation_worker_submit(uint64_t generation, uint64_t right_frame,
        uint32_t width, uint32_t height, const uint8_t* left, const uint8_t* right,
        unsigned intervals, uint64_t expires_qpc) {
    auto* worker = interpolation_worker();
    if (!worker || !worker->available.load(std::memory_order_acquire) || !generation ||
        !left || !right || width < 2 || height < 2 || width > 4096 || height > 2304 || ((width | height) & 1) ||
        intervals < 2 || intervals > kMaxInterpolationIntervals) return 0;
    try {
        auto job = std::make_unique<InterpolationJob>();
        job->generation = generation;
        job->right_frame = right_frame;
        job->expires_qpc = expires_qpc;
        job->width = width;
        job->height = height;
        job->intervals = intervals;
        if (interpolation_expired(*job)) return 0;
        const size_t bytes = size_t(width) * height * 3 / 2;
        job->left.assign(left, left + bytes);
        job->right.assign(right, right + bytes);
        {
            std::lock_guard lock(worker->mutex);
            if (worker->generation == generation && worker->right_frame >= right_frame) return 0;
            if (worker->generation != generation) for (auto& pair : worker->ready) pair = {};
            worker->generation = generation;
            worker->right_frame = right_frame;
            auto& pair = worker->ready[right_frame % kInterpolationPairs];
            pair = {};
            pair.right_frame = right_frame;
            pair.intervals = intervals;
            worker->pending = std::move(job);
        }
        worker->work.notify_one();
        return 1;
    } catch (const std::bad_alloc&) {
        return 0;
    }
}

int catherine_interpolation_worker_take(uint64_t generation, uint64_t right_frame,
        unsigned intervals, unsigned phase, uint8_t* output, size_t size) {
    auto* worker = interpolation_worker();
    if (!worker || !output || !phase || phase >= intervals || intervals > kMaxInterpolationIntervals) return 0;
    std::unique_ptr<uint8_t[]> frame;
    {
        std::lock_guard lock(worker->mutex);
        auto& pair = worker->ready[right_frame % kInterpolationPairs];
        if (worker->generation != generation || pair.right_frame != right_frame ||
            pair.intervals != intervals || !pair.phases[phase].pixels || pair.phases[phase].size != size)
            return 0;
        frame.swap(pair.phases[phase].pixels);
    }
    std::memcpy(output, frame.get(), size);
    return 1;
}

int catherine_interpolation_worker_has(uint64_t generation, uint64_t right_frame,
        unsigned intervals, unsigned phase) {
    auto* worker = interpolation_worker();
    if (!worker || !phase || phase >= intervals || intervals > kMaxInterpolationIntervals) return 0;
    std::lock_guard lock(worker->mutex);
    const auto& pair = worker->ready[right_frame % kInterpolationPairs];
    return worker->generation == generation && pair.right_frame == right_frame &&
           pair.intervals == intervals && pair.phases[phase].pixels != nullptr;
}

namespace {

constexpr unsigned kSignatureSamples = 16;
constexpr unsigned kNativeMovieRate = 30;
constexpr unsigned kMovieTextureSlots = 6;

struct MovieSignature {
    uint64_t generation = 0, ordinal = 0;
    uint32_t width = 0, height = 0;
    std::array<uint32_t, kSignatureSamples> pixels;
};

struct MovieTextureFrame {
    uint64_t memory = 0, generation = 0, ordinal = 0, expires = 0, first_present = 0;
    uint32_t width = 0, height = 0, stride = 0;
    unsigned target = 0;
};

struct MovieInterpolation {
    std::mutex mutex;
    std::atomic<uint64_t> generation{0}, active_until{0};
    std::atomic<unsigned> active_target{0};
    std::array<MovieSignature, 16> signatures;
    std::array<MovieTextureFrame, kMovieTextureSlots> textures;
    size_t next_signature = 0, next_texture = 0;
    uint64_t display_generation = 0;
};

struct MoviePreviousFrame {
    uint64_t generation = 0, ordinal = 0;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

MovieInterpolation& movie_interpolation() {
    static MovieInterpolation* state = new MovieInterpolation;
    return *state;
}

thread_local MoviePreviousFrame previous_movie_frame;

uint64_t movie_qpc() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

uint64_t movie_qpf() {
    static const uint64_t frequency = [] {
        LARGE_INTEGER result{};
        QueryPerformanceFrequency(&result);
        return result.QuadPart;
    }();
    return frequency;
}

unsigned movie_intervals(unsigned target) {
    return (target + kNativeMovieRate - 1) / kNativeMovieRate;
}

bool movie_interpolation_trace() {
    static const bool trace = GetEnvironmentVariableW(L"CATHERINE_TRACE_INTERPOLATION", nullptr, 0) != 0;
    return trace;
}

uint32_t sample_x(uint32_t width, unsigned sample) { return (width * ((sample * 5) % 16 + 1) / 18) & ~1u; }
uint32_t sample_y(uint32_t height, unsigned sample) { return (height * ((sample * 7) % 16 + 1) / 18) & ~1u; }

bool movie_signature_matches(const MovieSignature& signature, const uint8_t* pixels, uint32_t stride) {
    for (unsigned i = 0; i < kSignatureSamples; ++i) {
        const auto* pixel = pixels + size_t(sample_y(signature.height, i)) * stride +
                            size_t(sample_x(signature.width, i)) * 4;
        if (pixel[3] != 255) return false;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const int original = (signature.pixels[i] >> (channel * 8)) & 255;
            const int difference = original - int(pixel[channel]);
            if (difference < -2 || difference > 2) return false;
        }
    }
    return true;
}

}  // namespace

uint64_t catherine_movie_interpolation_generation(void) {
    return movie_interpolation().generation.fetch_add(1, std::memory_order_relaxed) + 1;
}

void catherine_movie_interpolation_decoded(uint64_t generation, uint64_t ordinal,
        const CatherineMovieFrame* frame, int target) {
    if (target <= kNativeMovieRate || target > 240 || !frame || !frame->data) return;
    catherine_interpolation_worker_warmup();
    if (!catherine_interpolation_worker_available()) return;
    const uint32_t w = frame->width, h = frame->height;
    if (w < 2 || h < 2 || w > 1920 || h > 1088 || ((w | h) & 1) || frame->size != size_t(w) * h * 3 / 2)
        return;

    MovieSignature signature{generation, ordinal, w, h, {}};
    for (unsigned i = 0; i < kSignatureSamples; ++i) {
        const auto x = sample_x(w, i), y = sample_y(h, i);
        const auto* uv = frame->data + size_t(w) * h + size_t(y / 2) * w + x;
        signature.pixels[i] = interpolation_bgra(frame->data[size_t(y) * w + x], uv[0], uv[1]);
    }
    auto& state = movie_interpolation();
    {
        std::lock_guard lock(state.mutex);
        state.signatures[state.next_signature++ % state.signatures.size()] = signature;
    }

    auto& previous = previous_movie_frame;
    if (previous.generation == generation && previous.ordinal + 1 == ordinal &&
        previous.width == w && previous.height == h) {
        const uint64_t expires = movie_qpc() + movie_qpf() / 6;
        catherine_interpolation_worker_submit(generation, ordinal, w, h, previous.pixels.data(), frame->data,
                                              movie_intervals(target), expires);
    }
    try {
        previous.pixels.assign(frame->data, frame->data + frame->size);
        previous.generation = generation;
        previous.ordinal = ordinal;
        previous.width = w;
        previous.height = h;
    } catch (const std::bad_alloc&) {
        previous = {};
    }
}

void catherine_movie_interpolation_end(uint64_t generation) {
    if (!generation) return;
    auto& state = movie_interpolation();
    {
        std::lock_guard lock(state.mutex);
        for (auto& signature : state.signatures) if (signature.generation == generation) signature = {};
        for (auto& frame : state.textures) if (frame.generation == generation) frame = {};
        if (state.display_generation == generation) {
            state.display_generation = 0;
            state.active_until.store(0, std::memory_order_release);
        }
    }
    if (previous_movie_frame.generation == generation) previous_movie_frame = {};
    catherine_interpolation_worker_reset(generation);
}

void catherine_movie_interpolation_display(uint64_t memory, uint32_t width, uint32_t height,
        uint32_t stride, uint64_t ordinal, int target) {
    if (!memory || target <= kNativeMovieRate || target > 240 || !catherine_interpolation_worker_available() ||
        width < 2 || height < 2 || width > 1920 || height > 1088 || stride != width * 4) return;

    const auto* pixels = reinterpret_cast<const uint8_t*>(memory);
    auto& state = movie_interpolation();
    std::lock_guard lock(state.mutex);
    uint64_t generation = 0;
    for (const auto& signature : state.signatures) {
        if (!signature.generation || signature.ordinal != ordinal || signature.width != width ||
            signature.height != height || !movie_signature_matches(signature, pixels, stride)) continue;
        if (signature.generation == state.display_generation) {
            generation = signature.generation;
            break;
        }
        if (generation && generation != signature.generation) return;
        generation = signature.generation;
    }
    // A mismatched original must not inherit a previously tracked image using this memory.
    MovieTextureFrame* frame = nullptr;
    for (auto& candidate : state.textures) if (candidate.memory == memory) { frame = &candidate; break; }
    if (!generation) {
        if (frame) *frame = {};
        return;
    }
    if (!frame) frame = &state.textures[state.next_texture++ % state.textures.size()];

    const uint64_t now = movie_qpc();
    const uint64_t expires = now + movie_qpf() / 10;
    const auto intervals = movie_intervals(target);
    if (state.display_generation != generation) {
        state.active_until.store(0, std::memory_order_release);
        state.display_generation = generation;
    }
    if (catherine_interpolation_worker_has(generation, ordinal + 1, intervals, 1)) {
        state.active_target.store(target, std::memory_order_release);
        state.active_until.store(now + movie_qpf() / 3, std::memory_order_release);
    }
    if (frame->generation != generation || frame->ordinal != ordinal || frame->target != unsigned(target)) {
        *frame = {memory, generation, ordinal, expires, 0, width, height, stride, unsigned(target)};
        if (movie_interpolation_trace())
            std::fprintf(stderr, "FMV interpolation original generation=%llu frame=%llu target=%d texture=%llx qpc=%llu\n",
                static_cast<unsigned long long>(generation), static_cast<unsigned long long>(ordinal), target,
                static_cast<unsigned long long>(memory), static_cast<unsigned long long>(now));
    }
}

int catherine_movie_interpolation_present_rate(void) {
    auto& state = movie_interpolation();
    const auto until = state.active_until.load(std::memory_order_acquire);
    if (!until || movie_qpc() >= until) return 0;
    return state.active_target.load(std::memory_order_acquire);
}

int catherine_movie_interpolation_texture(uint64_t memory, uint32_t width, uint32_t height,
        uint32_t stride, uint64_t present, CatherineInterpolationPlan* plan) {
    *plan = {};
    auto& state = movie_interpolation();
    if (!present || !catherine_movie_interpolation_present_rate()) return 0;
    std::lock_guard lock(state.mutex);
    for (auto& frame : state.textures) {
        if (frame.memory != memory || frame.width != width || frame.height != height || frame.stride != stride ||
            frame.generation != state.display_generation || movie_qpc() >= frame.expires ||
            frame.target != state.active_target.load(std::memory_order_relaxed)) continue;
        if (!frame.first_present) frame.first_present = present;
        if (present <= frame.first_present) return 0;
        const auto intervals = movie_intervals(frame.target);
        const auto age = present - frame.first_present;
        const unsigned phase = age >= intervals ? intervals - 1 : std::min(intervals - 1,
            unsigned((age * kNativeMovieRate * intervals + frame.target / 2) / frame.target));
        *plan = {frame.generation, frame.ordinal + 1, intervals, phase};
        return 1;
    }
    return 0;
}

int catherine_movie_interpolation_ready(const CatherineInterpolationPlan* plan) {
    return catherine_interpolation_worker_has(plan->generation, plan->right_frame, plan->intervals, plan->phase);
}

int catherine_movie_interpolation_copy(const CatherineInterpolationPlan* plan, uint8_t* pixels, size_t size,
        uint64_t present) {
    if (!catherine_interpolation_worker_take(plan->generation, plan->right_frame, plan->intervals,
                                            plan->phase, pixels, size)) return 0;
    if (movie_interpolation_trace())
        std::fprintf(stderr, "FMV interpolation presented generation=%llu right=%llu phase=%u/%u present=%llu qpc=%llu\n",
            static_cast<unsigned long long>(plan->generation), static_cast<unsigned long long>(plan->right_frame),
            plan->phase, plan->intervals, static_cast<unsigned long long>(present),
            static_cast<unsigned long long>(movie_qpc()));
    return 1;
}
