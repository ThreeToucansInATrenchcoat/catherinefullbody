#include <limits.h>
#include <linux/futex.h>
#include <poll.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <ucontext.h>
#include "heap_watch_linux.inc"

// The guest runtime owns the fixed mappings below the 39-bit address limit.
// Keep the read lock through the copy so decommit and protection changes
// cannot invalidate a previously checked page.
#define GUEST_ADDRESS_LIMIT (UINT64_C(1) << 39)
#define GUEST_PAGE_SIZE UINT64_C(4096)
static pthread_rwlock_t guest_mapping_lock = PTHREAD_RWLOCK_INITIALIZER;
static uint64_t readable_guest_pages[GUEST_ADDRESS_LIMIT / GUEST_PAGE_SIZE / 64];

static int mapped_guest_range(const void *address, size_t size) {
    const uintptr_t start = (uintptr_t)address;
    return size && start >= 0x8000000 && start < GUEST_ADDRESS_LIMIT &&
           size <= GUEST_ADDRESS_LIMIT - start;
}

static void mark_readable_guest(const void *address, size_t size, int readable) {
    if (!mapped_guest_range(address, size)) return;
    const uint64_t first = (uintptr_t)address / GUEST_PAGE_SIZE;
    const uint64_t last = ((uintptr_t)address + size - 1) / GUEST_PAGE_SIZE;
    for (uint64_t word = first / 64; word <= last / 64; ++word) {
        const uint64_t low = word == first / 64 ? UINT64_MAX << (first % 64) : UINT64_MAX;
        const uint64_t high = word == last / 64 ? UINT64_MAX >> (63 - last % 64) : UINT64_MAX;
        const uint64_t mask = low & high;
        if (readable) readable_guest_pages[word] |= mask;
        else readable_guest_pages[word] &= ~mask;
    }
}

int catherine_read_mapped_guest(const void *address, void *out, size_t size) {
    if (!mapped_guest_range(address, size)) return 0;
    const uint64_t first = (uintptr_t)address / GUEST_PAGE_SIZE;
    const uint64_t last = ((uintptr_t)address + size - 1) / GUEST_PAGE_SIZE;
    pthread_rwlock_rdlock(&guest_mapping_lock);
    for (uint64_t page = first; page <= last; ++page) {
        if (!(readable_guest_pages[page / 64] & (UINT64_C(1) << (page % 64)))) {
            pthread_rwlock_unlock(&guest_mapping_lock);
            return 0;
        }
    }
    memmove(out, address, size);
    pthread_rwlock_unlock(&guest_mapping_lock);
    return 1;
}

static int native_protection(DWORD protection) {
    switch (protection) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: return PROT_READ;
    case PAGE_READWRITE: return PROT_READ | PROT_WRITE;
    default: abort();
    }
}

size_t VirtualQuery(const void *address, MEMORY_BASIC_INFORMATION *info, size_t capacity) {
    if (capacity < sizeof(*info)) return 0;
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    uintptr_t ptr = (uintptr_t)address, previous = 0, first, last;
    char line[512], permissions[5];
    *info = (MEMORY_BASIC_INFORMATION){ .RegionSize = SIZE_MAX };
    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%lx-%lx %4s", &first, &last, permissions) != 3) continue;
        if (ptr < first) {
            info->BaseAddress = (void *)previous;
            info->RegionSize = first - previous;
            break;
        }
        if (ptr < last) {
            info->BaseAddress = (void *)first;
            info->RegionSize = last - first;
            info->State = permissions[0] == '-' && permissions[1] == '-' && permissions[2] == '-'
                          ? MEM_RESERVE : MEM_COMMIT;
            info->Protect = permissions[2] == 'x' && permissions[1] == 'w' ? PAGE_EXECUTE_READWRITE :
                            permissions[1] == 'w' ? PAGE_READWRITE :
                            permissions[0] == 'r' ? PAGE_READONLY : PAGE_NOACCESS;
            break;
        }
        previous = last;
        info->BaseAddress = (void *)previous;
        info->RegionSize = SIZE_MAX - previous;
    }
    fclose(maps);
    return sizeof(*info);
}

void *VirtualAlloc(void *address, size_t size, DWORD flags, DWORD protection) {
    if (flags & MEM_RESERVE) {
        int map_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
        if (address) map_flags |= MAP_FIXED_NOREPLACE;
        pthread_rwlock_wrlock(&guest_mapping_lock);
        void *result = mmap(address, size, native_protection(protection), map_flags, -1, 0);
        if (result != MAP_FAILED && address && result != address) {
            munmap(result, size);
            errno = EEXIST;
            result = MAP_FAILED;
        }
        if (result != MAP_FAILED && (flags & MEM_WRITE_WATCH) && !start_guest_write_watch(result, size)) {
            const int error = errno;
            munmap(result, size);
            errno = error;
            result = MAP_FAILED;
        }
        if (result != MAP_FAILED && (flags & MEM_COMMIT) && protection != PAGE_NOACCESS)
            mark_readable_guest(result, size, 1);
        pthread_rwlock_unlock(&guest_mapping_lock);
        return result != MAP_FAILED ? result : NULL;
    }
    pthread_rwlock_wrlock(&guest_mapping_lock);
    const int result = mprotect(address, size, native_protection(protection));
    mark_readable_guest(address, size, !result && protection != PAGE_NOACCESS);
    pthread_rwlock_unlock(&guest_mapping_lock);
    return result == 0 ? address : NULL;
}

BOOL VirtualFree(void *address, size_t size, DWORD flags) {
    // Only one-page guest TLS slots are released; other regions are decommitted.
    if (flags != MEM_RELEASE && flags != MEM_DECOMMIT) return FALSE;
    pthread_rwlock_wrlock(&guest_mapping_lock);
    int result;
    if (flags == MEM_RELEASE) {
        size = GUEST_PAGE_SIZE;
        result = munmap(address, size);
    } else {
        result = mprotect(address, size, PROT_NONE);
        mark_readable_guest(address, size, 0);
    }
    if (!result) {
        mark_readable_guest(address, size, 0);
        if (flags == MEM_DECOMMIT) result = madvise(address, size, MADV_DONTNEED);
    }
    pthread_rwlock_unlock(&guest_mapping_lock);
    return result == 0;
}

BOOL VirtualProtect(void *address, size_t size, DWORD protection, DWORD *previous) {
    MEMORY_BASIC_INFORMATION info;
    pthread_rwlock_wrlock(&guest_mapping_lock);
    int result = -1;
    if (VirtualQuery(address, &info, sizeof(info))) {
        *previous = info.Protect;
        result = mprotect(address, size, native_protection(protection));
        mark_readable_guest(address, size, !result && protection != PAGE_NOACCESS);
    }
    pthread_rwlock_unlock(&guest_mapping_lock);
    return result == 0;
}

struct CatherineHostHandle {
    int fd;
    int interrupted_fd;
    unsigned refs;
    pthread_mutex_t start_lock;
    pthread_cond_t start_signal;
    BOOL started;
    BOOL auto_reset;
    DWORD (*entry)(void *);
    void *argument;
};

static _Thread_local HANDLE running_thread;

static HANDLE new_handle(unsigned initial, int thread) {
    HANDLE handle = calloc(1, sizeof(*handle));
    if (!handle) return NULL;
    handle->fd = eventfd(initial, EFD_CLOEXEC | EFD_NONBLOCK);
    handle->interrupted_fd = thread ? eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK) : -1;
    if (handle->fd < 0 || (thread && handle->interrupted_fd < 0)) {
        int error = errno;
        if (handle->fd >= 0) close(handle->fd);
        if (handle->interrupted_fd >= 0) close(handle->interrupted_fd);
        free(handle);
        errno = error;
        return NULL;
    }
    handle->refs = 1;
    pthread_mutex_init(&handle->start_lock, NULL);
    pthread_cond_init(&handle->start_signal, NULL);
    return handle;
}

BOOL CloseHandle(HANDLE handle) {
    if (__atomic_sub_fetch(&handle->refs, 1, __ATOMIC_ACQ_REL)) return TRUE;
    close(handle->fd);
    if (handle->interrupted_fd >= 0) close(handle->interrupted_fd);
    pthread_mutex_destroy(&handle->start_lock);
    pthread_cond_destroy(&handle->start_signal);
    free(handle);
    return TRUE;
}

BOOL DuplicateHandle(HANDLE source, HANDLE handle, HANDLE target, HANDLE *copy,
                     DWORD access, BOOL inherit, DWORD flags) {
    (void)source; (void)target; (void)access; (void)inherit; (void)flags;
    if (!handle) return FALSE;
    __atomic_add_fetch(&handle->refs, 1, __ATOMIC_RELAXED);
    *copy = handle;
    return TRUE;
}

static BOOL notify(int fd) {
    int result;
    do result = eventfd_write(fd, 1); while (result < 0 && errno == EINTR);
    return result == 0 || errno == EAGAIN;
}

static BOOL clear_notification(int fd) {
    eventfd_t count;
    int result;
    do result = eventfd_read(fd, &count); while (result < 0 && errno == EINTR);
    return result == 0 || errno == EAGAIN;
}

HANDLE CreateEventA(void *attributes, BOOL manual_reset, BOOL initial, const char *name) {
    (void)attributes; (void)name;
    HANDLE event = new_handle(initial != 0, 0);
    if (event) event->auto_reset = !manual_reset;
    return event;
}
BOOL SetEvent(HANDLE event) { return event && notify(event->fd); }
BOOL ResetEvent(HANDLE event) { return event && clear_notification(event->fd); }

HANDLE GetCurrentThread(void) {
    if (!running_thread) running_thread = new_handle(0, 1);
    return running_thread;
}

static void finish_thread(void *argument) {
    HANDLE thread = argument;
    notify(thread->fd);
    CloseHandle(thread);
}

static void *run_thread(void *argument) {
    HANDLE thread = argument;
    running_thread = thread;
    pthread_mutex_lock(&thread->start_lock);
    while (!thread->started) pthread_cond_wait(&thread->start_signal, &thread->start_lock);
    pthread_mutex_unlock(&thread->start_lock);
    pthread_cleanup_push(finish_thread, thread);
    thread->entry(thread->argument);
    pthread_cleanup_pop(1);
    return NULL;
}

HANDLE CreateThread(void *attributes, size_t stack, DWORD (*entry)(void *),
                    void *arg, DWORD flags, DWORD *id) {
    (void)attributes; (void)id;
    HANDLE handle = new_handle(0, 1);
    if (!handle) return NULL;
    handle->entry = entry;
    handle->argument = arg;
    handle->started = !(flags & CREATE_SUSPENDED);
    handle->refs = 2;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int error = stack ? pthread_attr_setstacksize(&attr, stack) : 0;
    pthread_t thread;
    if (!error) error = pthread_create(&thread, &attr, run_thread, handle);
    pthread_attr_destroy(&attr);
    if (!error) return handle;
    handle->refs = 1;
    CloseHandle(handle);
    errno = error;
    return NULL;
}

DWORD ResumeThread(HANDLE thread) {
    pthread_mutex_lock(&thread->start_lock);
    DWORD was_suspended = !thread->started;
    thread->started = TRUE;
    pthread_cond_signal(&thread->start_signal);
    pthread_mutex_unlock(&thread->start_lock);
    return was_suspended;
}
_Noreturn void ExitThread(DWORD result) { pthread_exit((void *)(uintptr_t)result); }

DWORD QueueUserAPC(void (*callback)(ULONG_PTR), HANDLE thread, ULONG_PTR argument) {
    // Guest wait cancellation carries the payload in cancelled_waits.
    (void)callback; (void)argument;
    return notify(thread->interrupted_fd);
}

DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL all, DWORD ms, BOOL alertable) {
    if (all || count > MAXIMUM_WAIT_OBJECTS) return WAIT_FAILED;
    struct pollfd fds[MAXIMUM_WAIT_OBJECTS + 1];
    for (DWORD i = 0; i < count; ++i) fds[i] = (struct pollfd){handles[i]->fd, POLLIN, 0};
    if (alertable) {
        HANDLE thread = GetCurrentThread();
        if (!thread) return WAIT_FAILED;
        fds[count] = (struct pollfd){thread->interrupted_fd, POLLIN, 0};
    }
    uint64_t deadline = GetTickCount64() + ms;
    for (;;) {
        uint64_t now = GetTickCount64();
        uint64_t remaining = now < deadline ? deadline - now : 0;
        int timeout = ms == INFINITE ? -1 : remaining > INT_MAX ? INT_MAX : (int)remaining;
        int ready = poll(fds, count + alertable, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return WAIT_FAILED;
        if (!ready && timeout == INT_MAX && GetTickCount64() < deadline) continue;
        if (!ready) return WAIT_TIMEOUT;
        BOOL consumed_elsewhere = FALSE;
        for (DWORD i = 0; i < count; ++i) {
            if (!(fds[i].revents & POLLIN)) continue;
            if (!handles[i]->auto_reset) return WAIT_OBJECT_0 + i;
            eventfd_t value;
            int result;
            do result = eventfd_read(fds[i].fd, &value); while (result < 0 && errno == EINTR);
            if (result == 0) return WAIT_OBJECT_0 + i;
            if (errno != EAGAIN) return WAIT_FAILED;
            consumed_elsewhere = TRUE;
        }
        if (alertable && (fds[count].revents & POLLIN)) {
            clear_notification(fds[count].fd);
            return WAIT_IO_COMPLETION;
        }
        if (consumed_elsewhere) continue;
        return WAIT_FAILED;
    }
}

DWORD SleepEx(DWORD ms, BOOL alertable) {
    if (!ms && !alertable) { sched_yield(); return 0; }
    DWORD result = WaitForMultipleObjectsEx(0, NULL, FALSE, ms, alertable);
    return result == WAIT_TIMEOUT ? 0 : result;
}

struct HostFiber {
    ucontext_t context;
    void *stack;
    size_t stack_size;
    void (*entry)(void *);
    void *argument;
};
static _Thread_local struct HostFiber root_context;
static _Thread_local struct HostFiber *current_context;

void *ConvertThreadToFiber(void *argument) {
    (void)argument;
    current_context = &root_context;
    return current_context;
}

static void run_fiber(void) {
    current_context->entry(current_context->argument);
    abort();
}

void *CreateFiber(size_t stack, void (*entry)(void *), void *argument) {
    struct HostFiber *fiber = calloc(1, sizeof(*fiber));
    if (!fiber) return NULL;
    void *memory = mmap(NULL, stack + 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) { free(fiber); return NULL; }
    fiber->stack = memory;
    fiber->stack_size = stack + 4096;
    if (mprotect((char *)memory + 4096, stack, PROT_READ | PROT_WRITE) || getcontext(&fiber->context)) {
        DeleteFiber(fiber);
        return NULL;
    }
    fiber->context.uc_stack.ss_sp = (char *)memory + 4096;
    fiber->context.uc_stack.ss_size = stack;
    fiber->entry = entry;
    fiber->argument = argument;
    makecontext(&fiber->context, run_fiber, 0);
    return fiber;
}

void DeleteFiber(void *fiber_ptr) {
    struct HostFiber *fiber = fiber_ptr;
    munmap(fiber->stack, fiber->stack_size);
    free(fiber);
}

void SwitchToFiber(void *fiber_ptr) {
    struct HostFiber *next = fiber_ptr, *previous = current_context;
    current_context = next;
    if (swapcontext(&previous->context, &next->context)) abort();
}

int BCryptGenRandom(void *algorithm, PUCHAR out, DWORD size, DWORD flags) {
    (void)algorithm; (void)flags;
    while (size) {
        ssize_t got = getrandom(out, size, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        out += got;
        size -= got;
    }
    return 0;
}

BOOL WaitOnAddress(volatile LONG *address, const LONG *value, size_t bytes, DWORD timeout) {
    if (bytes != sizeof(LONG) || timeout != INFINITE) { errno = ENOTSUP; return FALSE; }
    long result = syscall(SYS_futex, address, FUTEX_WAIT_PRIVATE, *value, NULL, NULL, 0);
    return result == 0 || errno == EAGAIN || errno == EINTR;
}

void WakeByAddressAll(void *address) {
    syscall(SYS_futex, address, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}

#include <fcntl.h>
#include <sys/stat.h>

static DWORD file_attributes(mode_t mode) {
    return S_ISREG(mode) ? FILE_ATTRIBUTE_NORMAL :
           S_ISDIR(mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_REPARSE_POINT;
}
DWORD GetFileAttributesA(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0 ? file_attributes(info.st_mode) : INVALID_FILE_ATTRIBUTES;
}
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template_file) {
    (void)share; (void)security; (void)flags; (void)template_file;
    int mode = access == GENERIC_READ ? O_RDONLY : access == GENERIC_WRITE ? O_WRONLY : O_RDWR;
    if (disposition == CREATE_NEW) mode |= O_CREAT | O_EXCL;
    int fd = open(path, mode | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return INVALID_HANDLE_VALUE;
    HANDLE file = new_handle(0, 0);
    if (!file) { int error = errno; close(fd); errno = error; return INVALID_HANDLE_VALUE; }
    close(file->fd);
    file->fd = fd;
    return file;
}
BOOL DeleteFileA(const char *path) { return unlink(path) == 0; }
BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION *info) {
    struct stat status;
    if (fstat(file->fd, &status)) return FALSE;
    info->dwFileAttributes = file_attributes(status.st_mode);
    info->nNumberOfLinks = status.st_nlink;
    return TRUE;
}
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER *size) {
    struct stat status;
    if (fstat(file->fd, &status)) return FALSE;
    size->QuadPart = status.st_size;
    return TRUE;
}
BOOL FlushFileBuffers(HANDLE file) { return fsync(file->fd) == 0; }
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER offset, LARGE_INTEGER *position, DWORD origin) {
    off_t result = lseek(file->fd, offset.QuadPart, origin);
    if (result < 0) return FALSE;
    if (position) position->QuadPart = result;
    return TRUE;
}
BOOL SetEndOfFile(HANDLE file) {
    off_t position = lseek(file->fd, 0, SEEK_CUR);
    return position >= 0 && ftruncate(file->fd, position) == 0;
}
BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *bytes, void *overlapped) {
    (void)overlapped;
    *bytes = 0;
    while (*bytes < size) {
        ssize_t count = read(file->fd, (char *)buffer + *bytes, size - *bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return FALSE;
        if (!count) break;
        *bytes += count;
    }
    return TRUE;
}
BOOL WriteFile(HANDLE file, const void *buffer, DWORD size, DWORD *bytes, void *overlapped) {
    (void)overlapped;
    *bytes = 0;
    while (*bytes < size) {
        ssize_t count = write(file->fd, (const char *)buffer + *bytes, size - *bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (!count) errno = EIO; return FALSE; }
        *bytes += count;
    }
    return TRUE;
}
