#include <fcntl.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>

#if defined(UFFD_FEATURE_WP_ASYNC) && defined(UFFD_USER_MODE_ONLY) && defined(PAGEMAP_SCAN)
static int guest_write_watch = -1;
static int guest_write_pagemap = -1;

static int start_guest_write_watch(void *address, size_t size) {
    if (guest_write_watch >= 0) { errno = EBUSY; return 0; }
    int watch = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    if (watch < 0) return 0;
    struct uffdio_api api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_UNPOPULATED
    };
    struct uffdio_register range = {
        .range = {(uintptr_t)address, size},
        .mode = UFFDIO_REGISTER_MODE_WP
    };
    int pagemap = -1;
    if (ioctl(watch, UFFDIO_API, &api) == 0 &&
        (api.features & (UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_UNPOPULATED)) ==
                        (UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_UNPOPULATED) &&
        ioctl(watch, UFFDIO_REGISTER, &range) == 0) {
        pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    }
    if (pagemap < 0) {
        const int error = errno;
        close(watch);
        errno = error;
        return 0;
    }
    // Check support before exposing the registered heap to the game. This
    // also establishes the initial protected state for the first page.
    struct pm_scan_arg probe = {
        .size = sizeof(probe),
        .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
        .start = (uintptr_t)address,
        .end = (uintptr_t)address + 4096,
        .category_mask = PAGE_IS_WRITTEN
    };
    if (ioctl(pagemap, PAGEMAP_SCAN, &probe) < 0 || probe.walk_end != probe.end) {
        const int error = errno;
        close(pagemap);
        close(watch);
        errno = error ? error : ENOTSUP;
        return 0;
    }
    guest_write_watch = watch;
    guest_write_pagemap = pagemap;
    return 1;
}

int catherine_heap_watch_query(uint64_t address, uint64_t bytes, void **pages, size_t *count) {
    // The arena takes a heap pin around this call and supplies up to 256 pages.
    struct page_region ranges[256];
    if (guest_write_pagemap < 0 || !*count || *count > ARRAYSIZE(ranges)) return 0;
    struct pm_scan_arg scan = {
        .size = sizeof(scan),
        .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
        .start = address,
        .end = address + bytes,
        .vec = (uintptr_t)ranges,
        .vec_len = *count,
        .category_mask = PAGE_IS_WRITTEN,
        .return_mask = PAGE_IS_WRITTEN
    };
    const int found = ioctl(guest_write_pagemap, PAGEMAP_SCAN, &scan);
    if (found < 0 || (size_t)found > *count || scan.walk_end != scan.end) return 0;
    size_t total = 0;
    for (int i = 0; i < found; ++i) {
        const uint64_t start = ranges[i].start;
        const uint64_t end = ranges[i].end;
        if (start < address || end > scan.end || start >= end || ((start | end) & 4095)) return 0;
        for (uint64_t page = start; page < end; page += 4096) {
            if (total == *count) return 0;
            pages[total++] = (void *)(uintptr_t)page;
        }
    }
    *count = total;
    return 1;
}
#else
static int start_guest_write_watch(void *address, size_t size) {
    (void)address;
    (void)size;
    errno = ENOTSUP;
    return 0;
}

int catherine_heap_watch_query(uint64_t address, uint64_t bytes, void **pages, size_t *count) {
    (void)address;
    (void)bytes;
    (void)pages;
    (void)count;
    return 0;
}
#endif
