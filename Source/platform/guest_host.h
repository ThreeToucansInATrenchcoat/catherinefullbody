#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "host_memory.h"

#ifdef __cplusplus
extern "C" {
#define CATHERINE_NORETURN [[noreturn]]
#else
#define CATHERINE_NORETURN _Noreturn
#endif

typedef struct CatherineHostHandle *HANDLE;
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef int64_t LONG64;
typedef int BOOL;
typedef void VOID;
typedef uintptr_t ULONG_PTR;
typedef unsigned char *PUCHAR;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef pthread_rwlock_t SRWLOCK;

#define SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
#define AcquireSRWLockShared pthread_rwlock_rdlock
#define AcquireSRWLockExclusive pthread_rwlock_wrlock
#define ReleaseSRWLockShared pthread_rwlock_unlock
#define ReleaseSRWLockExclusive pthread_rwlock_unlock
#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define ARRAYSIZE(a) (sizeof(a) / sizeof(*(a)))
#define INFINITE UINT32_MAX
#define MAXIMUM_WAIT_OBJECTS 64
#define WAIT_OBJECT_0 0u
#define WAIT_IO_COMPLETION 192u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED UINT32_MAX
#define DUPLICATE_SAME_ACCESS 2
#define CREATE_SUSPENDED 4
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x10000
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 2

enum {
    MEM_FREE = 0, MEM_RESERVE = 1, MEM_COMMIT = 2, MEM_WRITE_WATCH = 4,
    MEM_RELEASE = 8, MEM_DECOMMIT = 16,
    PAGE_NOACCESS = 1, PAGE_READONLY = 2, PAGE_READWRITE = 4,
    PAGE_WRITECOPY = 8, PAGE_EXECUTE_READWRITE = 16, PAGE_EXECUTE_WRITECOPY = 32,
    PAGE_GUARD = 64
};
typedef struct { void *BaseAddress; size_t RegionSize; DWORD State, Protect; }
    MEMORY_BASIC_INFORMATION;

void *VirtualAlloc(void *address, size_t size, DWORD flags, DWORD protection);
BOOL VirtualFree(void *address, size_t size, DWORD flags);
BOOL VirtualProtect(void *address, size_t size, DWORD protection, DWORD *previous);
size_t VirtualQuery(const void *address, MEMORY_BASIC_INFORMATION *info, size_t capacity);

HANDLE GetCurrentThread(void);
HANDLE CreateThread(void *attributes, size_t stack, DWORD (*entry)(void *),
                    void *arg, DWORD flags, DWORD *id);
DWORD ResumeThread(HANDLE thread);
CATHERINE_NORETURN void ExitThread(DWORD result);
HANDLE CreateEventA(void *attributes, BOOL manual_reset, BOOL initial, const char *name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
BOOL CloseHandle(HANDLE handle);
BOOL DuplicateHandle(HANDLE source, HANDLE handle, HANDLE target, HANDLE *copy,
                     DWORD access, BOOL inherit, DWORD flags);
DWORD QueueUserAPC(void (*callback)(ULONG_PTR), HANDLE thread, ULONG_PTR argument);
DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL all, DWORD ms, BOOL alertable);
DWORD SleepEx(DWORD ms, BOOL alertable);
void *ConvertThreadToFiber(void *argument);
void *CreateFiber(size_t stack, void (*entry)(void *), void *argument);
void DeleteFiber(void *fiber);
void SwitchToFiber(void *fiber);
int BCryptGenRandom(void *algorithm, PUCHAR out, DWORD size, DWORD flags);
BOOL WaitOnAddress(volatile LONG *address, const LONG *value, size_t bytes, DWORD timeout);
void WakeByAddressAll(void *address);

static inline HANDLE GetCurrentProcess(void) { return NULL; }
static inline unsigned long GetLastError(void) { return (unsigned long)errno; }
static inline BOOL ReadProcessMemory(HANDLE process, const void *ptr, void *out, size_t n, size_t *got) {
    (void)process;
    BOOL ok = catherine_host_read(ptr, out, n);
    if (got) *got = ok ? n : 0;
    return ok;
}
static inline DWORD WaitForMultipleObjects(DWORD n, const HANDLE *handles, BOOL all, DWORD ms) {
    return WaitForMultipleObjectsEx(n, handles, all, ms, FALSE);
}
static inline DWORD WaitForSingleObject(HANDLE handle, DWORD ms) {
    return WaitForMultipleObjects(1, &handle, FALSE, ms);
}
static inline void Sleep(DWORD ms) { SleepEx(ms, FALSE); }
static inline void QueryPerformanceCounter(LARGE_INTEGER *counter) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    counter->QuadPart = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
static inline void QueryPerformanceFrequency(LARGE_INTEGER *frequency) {
    frequency->QuadPart = 1000000000;
}
static inline uint64_t GetTickCount64(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart / 1000000;
}
static inline DWORD GetEnvironmentVariableA(const char *name, char *out, DWORD size) {
    const char *value = getenv(name);
    if (!value) return 0;
    size_t length = strlen(value);
    if (length >= size) return length + 1;
    memcpy(out, value, length + 1);
    return length;
}
static inline LONG InterlockedCompareExchange(volatile LONG *address, LONG value, LONG expected) {
    __atomic_compare_exchange_n(address, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
static inline void *InterlockedCompareExchangePointer(void *volatile *address, void *value, void *expected) {
    __atomic_compare_exchange_n(address, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
static inline LONG InterlockedExchange(volatile LONG *address, LONG value) {
    return __atomic_exchange_n(address, value, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedExchangeAdd(volatile LONG *address, LONG value) {
    return __atomic_fetch_add(address, value, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedIncrement(volatile LONG *address) {
    return __atomic_add_fetch(address, 1, __ATOMIC_SEQ_CST);
}
static inline LONG InterlockedDecrement(volatile LONG *address) {
    return __atomic_sub_fetch(address, 1, __ATOMIC_SEQ_CST);
}
static inline LONG64 InterlockedIncrement64(volatile LONG64 *address) {
    return __atomic_add_fetch(address, 1, __ATOMIC_SEQ_CST);
}
static inline int fopen_s(FILE **file, const char *path, const char *mode) {
    *file = fopen(path, mode);
    return *file ? 0 : errno;
}
static inline int gmtime_s(struct tm *calendar, const time_t *seconds) {
    return gmtime_r(seconds, calendar) ? 0 : errno;
}
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define MAXDWORD UINT32_MAX
#define INVALID_FILE_ATTRIBUTES UINT32_MAX
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
enum {
    ERROR_FILE_NOT_FOUND = ENOENT, ERROR_PATH_NOT_FOUND = ENOTDIR,
    ERROR_FILE_EXISTS = EEXIST, ERROR_ALREADY_EXISTS = -1,
    ERROR_DISK_FULL = ENOSPC, ERROR_HANDLE_DISK_FULL = EDQUOT,
    ERROR_ACCESS_DENIED = EACCES, ERROR_SHARING_VIOLATION = EBUSY,
    FILE_ATTRIBUTE_NORMAL = 0, FILE_ATTRIBUTE_DIRECTORY = 1, FILE_ATTRIBUTE_REPARSE_POINT = 2,
    FILE_FLAG_OPEN_REPARSE_POINT = 0, FILE_SHARE_READ = 0, FILE_BEGIN = SEEK_SET,
    GENERIC_READ = 1, GENERIC_WRITE = 2, OPEN_EXISTING = 0, CREATE_NEW = 1,
};
typedef struct { DWORD dwFileAttributes; uint64_t nNumberOfLinks; } BY_HANDLE_FILE_INFORMATION;
DWORD GetFileAttributesA(const char *path);
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template_file);
BOOL DeleteFileA(const char *path);
BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION *info);
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER *size);
BOOL FlushFileBuffers(HANDLE file);
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER offset, LARGE_INTEGER *position, DWORD origin);
BOOL SetEndOfFile(HANDLE file);
BOOL ReadFile(HANDLE file, void *buffer, DWORD size, DWORD *bytes, void *overlapped);
BOOL WriteFile(HANDLE file, const void *buffer, DWORD size, DWORD *bytes, void *overlapped);

static inline LONG64 InterlockedExchange64(volatile LONG64 *address, LONG64 value) {
    return __atomic_exchange_n(address, value, __ATOMIC_SEQ_CST);
}
static inline LONG64 InterlockedCompareExchange64(volatile LONG64 *address, LONG64 value, LONG64 expected) {
    __atomic_compare_exchange_n(address, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
#undef CATHERINE_NORETURN
#ifdef __cplusplus
}
#endif
#endif
