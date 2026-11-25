#pragma once

#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C"
#endif
int catherine_read_mapped_guest(const void *, void *, size_t) __attribute__((weak));
#endif

// Read our address space without crashing if a guest pointer is stale.
static inline int catherine_host_read(const void *address, void *out, size_t size) {
    if (!size) return 1;
#ifdef _WIN32
    SIZE_T transferred = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, out, size, &transferred) &&
           transferred == size;
#else
    if (catherine_read_mapped_guest && catherine_read_mapped_guest(address, out, size)) return 1;
    struct iovec local = {out, size};
    struct iovec remote = {(void *)address, size};
    const long transferred = syscall(SYS_process_vm_readv, getpid(), &local, 1UL, &remote, 1UL, 0UL);
    return transferred >= 0 && (size_t)transferred == size;
#endif
}
