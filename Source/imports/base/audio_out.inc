// The SDK mixes PCM into guest memory and submits it through IAudioOut. Keep the tag
// pending until the Windows voice releases its copy of the samples.
extern int host_audio_open(void (*done)(void *), uint32_t channels);
extern int host_audio_submit(const void *data, size_t size, void *tag);
extern void host_audio_set_running(int running);
extern uint64_t host_audio_played_samples(void);
extern int host_audio_flush(void);
extern void host_audio_set_volume(float volume);

#ifdef _WIN32
static INIT_ONCE audio_once = INIT_ONCE_STATIC_INIT;
#else
static pthread_mutex_t audio_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int audio_initialized;
#endif
static SRWLOCK audio_lock = SRWLOCK_INIT;
static uint64_t completed_audio[32];
static unsigned completed_count, audio_pending_count;
static uint32_t audio_device_channels;
static volatile LONG audio_native, audio_state = 1, audio_volume_bits = 0x3f800000;
static volatile LONG64 audio_sample_origin;

static void audio_release(void *tag) {
    AcquireSRWLockExclusive(&audio_lock);
    completed_audio[completed_count++] = (uint64_t)(uintptr_t)tag;
    SetEvent(output_event);
    ReleaseSRWLockExclusive(&audio_lock);
}

static void audio_init_channels(uint32_t channels) {
    if (host_audio_open(audio_release, channels)) {
        audio_device_channels = channels;
        host_audio_set_running(0);
    }
}

#ifdef _WIN32
static BOOL CALLBACK audio_init(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)context;
    audio_init_channels((uint32_t)(uintptr_t)parameter);
    return TRUE;
}
#endif

static int audio_buffer_access(uint64_t address, uint64_t size, int writable) {
    const uint64_t guest_limit = 1ull << 39;
    if (!address || address >= guest_limit || size > guest_limit - address) return 0;
    const uint64_t end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)(uintptr_t)address, &region, sizeof region) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return 0;
        if (writable && !(region.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return 0;
        address = (uintptr_t)region.BaseAddress + region.RegionSize;
    }
    return 1;
}

// The buffer descriptor is either a send map-alias (A) or an auto-selected
// send static (X). The raw command argument is a separate completion tag.
static int audio_pcm(const uint32_t *message, unsigned descriptor, const void **data, size_t *size) {
    const unsigned statics = (message[0] >> 16) & 15;
    const unsigned buffers = (message[0] >> 20) & 15;
    uint64_t address = 0, length = 0;
    if (buffers) {
        const uint32_t *entry = message + descriptor + 2 * statics;
        address = entry[1] | ((uint64_t)(entry[2] >> 28) << 32) |
                  ((uint64_t)((entry[2] >> 2) & 0x3fffff) << 36);
        length = entry[0] | ((uint64_t)((entry[2] >> 24) & 15) << 32);
    }
    if (!length && statics) {
        const uint32_t *entry = message + descriptor;
        address = entry[1] | ((uint64_t)((entry[0] >> 12) & 15) << 32) |
                  ((uint64_t)((entry[0] >> 6) & 63) << 36);
        length = entry[0] >> 16;
    }
    // AudioOutBuffer begins with {next, sample pointer, capacity, data size}.
    // The SDK passes the pointer directly to the start of the samples.
    uint64_t buffer[4];
    if (length < sizeof buffer || !audio_buffer_access(address, sizeof buffer, 0)) {
        static volatile LONG warned;
        if (!InterlockedExchange(&warned, 1))
            fprintf(stderr, "Audio output buffer descriptor is unreadable: address=%#llx length=%llu\n", address, length);
        return 0;
    }
    memcpy(buffer, (const void *)(uintptr_t)address, sizeof buffer);
    const uint64_t capacity = buffer[2], bytes = buffer[3];
    if (bytes > capacity || bytes > 16 * 1024 * 1024 || !bytes ||
        !audio_buffer_access(buffer[1], bytes, 0)) {
        static volatile LONG warned;
        if (!InterlockedExchange(&warned, 1))
            fprintf(stderr, "Audio output sample data is unreadable: address=%#llx length=%llu capacity=%llu\n",
                    buffer[1], bytes, capacity);
        return 0;
    }
    *data = (const void *)(uintptr_t)buffer[1];
    *size = (size_t)bytes;
    return 1;
}

static void audio_open_device(uint32_t rate, uint32_t channels) {
    const int compatible = rate == 48000 && (channels == 2 || channels == 6);
#ifdef _WIN32
    if (compatible) InitOnceExecuteOnce(&audio_once, audio_init, (void *)(uintptr_t)channels, NULL);
#else
    pthread_mutex_lock(&audio_init_lock);
    if (compatible && !audio_initialized) {
        audio_init_channels(channels);
        audio_initialized = 1;
    }
    pthread_mutex_unlock(&audio_init_lock);
#endif
    const int available = compatible && audio_device_channels == channels;
    InterlockedExchange(&audio_native, available);
    InterlockedExchange(&audio_state, 1);
    printf("Audio output initialized %s (%u Hz, %u channels, PCM16)\n",
           available ? "with host audio" : "in silent mode", rate, channels);
}

static unsigned audio_active_buffers(void) {
    AcquireSRWLockShared(&audio_lock);
    unsigned count = audio_pending_count - completed_count;
    ReleaseSRWLockShared(&audio_lock);
    return count;
}

static void audio_control(uint32_t *message, unsigned command, unsigned payload, unsigned data_end) {
    const int native = InterlockedCompareExchange(&audio_native, 0, 0);
    uint64_t value = 0;
    switch (command) {
    case 0:
        value = (unsigned)InterlockedCompareExchange(&audio_state, 0, 0);
        break;
    case 1:
        if (InterlockedExchange(&audio_state, 0) && native) {
            InterlockedExchange64(&audio_sample_origin, (LONG64)host_audio_played_samples());
            host_audio_set_running(1);
        }
        break;
    case 2:
        if (!InterlockedExchange(&audio_state, 1) && native) {
            host_audio_set_running(0);
            host_audio_flush();
        }
        break;
    case 4:
        reply(message, 0, 104, 0);
        message[2] = 2;
        return;
    case 9:
        value = audio_active_buffers();
        break;
    case 10:
        if (native && !InterlockedCompareExchange(&audio_state, 0, 0)) {
            const uint64_t origin = (uint64_t)InterlockedCompareExchange64(&audio_sample_origin, 0, 0);
            const uint64_t played = host_audio_played_samples();
            if (played >= origin) value = played - origin;
        }
        break;
    case 11:
        if (native && !InterlockedCompareExchange(&audio_state, 0, 0) && audio_active_buffers()) {
            host_audio_set_running(0);
            value = host_audio_flush();
            host_audio_set_running(1);
        }
        break;
    case 12:
        if (payload + 5 <= data_end) {
            float volume;
            memcpy(&volume, message + payload + 4, sizeof volume);
            if (isfinite(volume) && volume >= 0.0f) {
                InterlockedExchange(&audio_volume_bits, (LONG)message[payload + 4]);
                if (native) host_audio_set_volume(volume);
            }
        }
        break;
    case 13:
        value = (uint32_t)InterlockedCompareExchange(&audio_volume_bits, 0, 0);
        break;
    }
    reply(message, 0, 0, (uint32_t)value);
    if (command == 10) {
        message[1]++;
        message[9] = (uint32_t)(value >> 32);
    }
}

static void audio_append(uint32_t *message, unsigned payload, unsigned descriptor) {
    uint64_t tag;
    memcpy(&tag, message + payload + 4, sizeof tag);
    AcquireSRWLockExclusive(&audio_lock);
    if (audio_pending_count == sizeof completed_audio / sizeof completed_audio[0]) {
        ReleaseSRWLockExclusive(&audio_lock);
        reply(message, 0, 0, 0);
        message[6] = 0x1099;  // AudioOut: buffer ring full
        return;
    }
    audio_pending_count++;
    ReleaseSRWLockExclusive(&audio_lock);
    if (InterlockedCompareExchange(&audio_native, 0, 0)) {
        const void *samples;
        size_t size;
        if (audio_pcm(message, descriptor, &samples, &size)) {
            if (host_audio_submit(samples, size, (void *)(uintptr_t)tag)) {
                reply(message, 0, 0, 0);
                return;
            }
            static volatile LONG warned;
            if (!InterlockedExchange(&warned, 1))
                fprintf(stderr, "XAudio2 rejected %zu bytes of %u-channel PCM16\n", size, audio_device_channels);
        }
    }
    audio_release((void *)(uintptr_t)tag);
    reply(message, 0, 0, 0);
}

static void audio_get_released(uint32_t *message, uint64_t address, uint64_t capacity) {
    if (capacity && !audio_buffer_access(address, capacity, 1)) {
        reply(message, 0, 0, 0);
        return;
    }
    AcquireSRWLockExclusive(&audio_lock);
    const unsigned count = capacity / 8 > completed_count ? completed_count : (unsigned)(capacity / 8);
    // The SDK's singular GetReleasedAudioOutBuffer reads its output slot even when count is zero.
    if (capacity > (uint64_t)count * 8)
        memset((void *)(uintptr_t)(address + (uint64_t)count * 8), 0, (size_t)(capacity - (uint64_t)count * 8));
    if (count) {
        memcpy((void *)(uintptr_t)address, completed_audio, count * 8);
        memmove(completed_audio, completed_audio + count, (completed_count - count) * 8);
        completed_count -= count;
        audio_pending_count -= count;
    }
    if (!completed_count) ResetEvent(output_event);
    ReleaseSRWLockExclusive(&audio_lock);
    reply(message, 0, 0, count);
    if (count && !InterlockedCompareExchange(&audio_native, 0, 0)) Sleep(5);
}
