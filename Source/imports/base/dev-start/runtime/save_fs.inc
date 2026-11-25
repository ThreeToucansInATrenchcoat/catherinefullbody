/* Local save objects for the FSP domain. Included after domain_reply in ipc.c. */
enum {
    SAVE_FILESYSTEM = 3,
    SAVE_FILE_BASE = 1000,
    SAVE_FILE_COUNT = 64,
    FS_NOT_FOUND = 0x202,
    FS_ALREADY_EXISTS = 0x402,
    FS_NO_SPACE = 0xe02,
    FS_INVALID_PATH = 0x2ee402,
    FS_INVALID_MODE = 0x307002,
    FS_NO_APPEND = 0x307202,
    FS_IO_ERROR = 0x271002
};

static struct {
    HANDLE handle;
    unsigned mode;
} save_files[SAVE_FILE_COUNT];
static SRWLOCK save_lock = SRWLOCK_INIT;
#ifndef _WIN32
void catherine_linux_freeze_saves(void) {
    // Retain the lock until process exit so no guest thread can modify a
    // save after the final flush. Other save operations use this same lock.
    AcquireSRWLockExclusive(&save_lock);
    for (unsigned i = 0; i < SAVE_FILE_COUNT; ++i) {
        if (save_files[i].handle && (save_files[i].mode & 2))
            FlushFileBuffers(save_files[i].handle);
    }
}
#endif
#ifdef _WIN32
#include "dev_save.inc"
#else
#include "C:/catherine/recomp/Source/platform/dev_save_linux.inc"
#endif

static unsigned save_result(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return FS_NOT_FOUND;
        case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return FS_ALREADY_EXISTS;
        case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return FS_NO_SPACE;
        case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return FS_INVALID_MODE;
        default: return FS_IO_ERROR;
    }
}

static int save_missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static unsigned save_mount(void) {
    if (InterlockedCompareExchange(&dev_save_active, 0, 0)) {
        DWORD attributes = GetFileAttributesA(dev_save_filename);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            return save_missing(error) ? 0 : save_result(error);
        }
        return attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) ? FS_INVALID_PATH : 0;
    }
#ifdef _WIN32
    const DWORD current = GetFileAttributesW(L"save.dat");
    if (current != INVALID_FILE_ATTRIBUTES)
        return (current & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ? FS_INVALID_PATH : 0;
    DWORD error = GetLastError();
    if (!save_missing(error)) return save_result(error);

    const DWORD old_folder = GetFileAttributesW(L"save");
    if (old_folder == INVALID_FILE_ATTRIBUTES)
        return save_missing(GetLastError()) ? 0 : save_result(GetLastError());
    if (!(old_folder & FILE_ATTRIBUTE_DIRECTORY) || (old_folder & FILE_ATTRIBUTE_REPARSE_POINT))
        return FS_INVALID_PATH;
    const DWORD old_file = GetFileAttributesW(L"save\\system_dat");
    if (old_file != INVALID_FILE_ATTRIBUTES) {
        if (old_file & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return FS_INVALID_PATH;
        if (!MoveFileExW(L"save\\system_dat", L"save.dat", MOVEFILE_WRITE_THROUGH)) {
            error = GetLastError();
            // Another instance may have migrated it between the attribute check and rename.
            const DWORD migrated = GetFileAttributesW(L"save.dat");
            if (migrated == INVALID_FILE_ATTRIBUTES) return save_result(error);
            return (migrated & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ? FS_INVALID_PATH : 0;
        }
    } else if (!save_missing(GetLastError())) {
        return save_result(GetLastError());
    }
    // RemoveDirectory leaves a legacy folder with any additional files intact.
    RemoveDirectoryW(L"save");
#else
    DWORD current = GetFileAttributesA("save.dat");
    if (current != INVALID_FILE_ATTRIBUTES) return current ? FS_INVALID_PATH : 0;
    if (!save_missing(GetLastError())) return save_result(GetLastError());

    DWORD folder = GetFileAttributesA("save");
    if (folder == INVALID_FILE_ATTRIBUTES)
        return save_missing(GetLastError()) ? 0 : save_result(GetLastError());
    if (folder != FILE_ATTRIBUTE_DIRECTORY) return FS_INVALID_PATH;

    DWORD legacy = GetFileAttributesA("save/system_dat");
    if (legacy == INVALID_FILE_ATTRIBUTES)
        return save_missing(GetLastError()) ? 0 : save_result(GetLastError());
    if (legacy) return FS_INVALID_PATH;
    if (syscall(SYS_renameat2, AT_FDCWD, "save/system_dat", AT_FDCWD, "save.dat", RENAME_NOREPLACE) != 0) {
        DWORD error = GetLastError();
        current = GetFileAttributesA("save.dat");
        if (current == INVALID_FILE_ATTRIBUTES) return save_result(error);
        return current ? FS_INVALID_PATH : 0;
    }
    rmdir("save");
#endif
    return 0;
}

static int save_path(const uint32_t *message, unsigned descriptor, char path[256]) {
    if (!(message[0] & 0xf0000)) return 0;
    const uint32_t *d = message + descriptor;
    uint64_t address = d[1] | ((uint64_t)((d[0] >> 12) & 15) << 32) |
                       ((uint64_t)((d[0] >> 6) & 7) << 36);
    size_t capacity = d[0] >> 16;
    const char *name = (const char *)(uintptr_t)address;
    if (!name || capacity < 2 || name[0] != '/') return 0;
    static const char guest_name[] = "/system_dat";
    if (capacity < sizeof guest_name || memcmp(name, guest_name, sizeof guest_name)) return 0;
    const char *native_path = catherine_dev_save_path();
    memcpy(path, native_path, strlen(native_path) + 1);
    return 1;
}

static int save_file_object(unsigned object) {
    return object >= SAVE_FILE_BASE && object < SAVE_FILE_BASE + SAVE_FILE_COUNT;
}

static void save_close(unsigned object) {
    if (!save_file_object(object)) return;
    unsigned slot = object - SAVE_FILE_BASE;
    AcquireSRWLockExclusive(&save_lock);
    if (save_files[slot].handle) {
        CloseHandle(save_files[slot].handle);
        save_files[slot].handle = NULL;
    }
    ReleaseSRWLockExclusive(&save_lock);
}

static unsigned save_file_cmd(unsigned slot, unsigned command, const uint32_t *m,
                              unsigned payload, unsigned descriptor, unsigned end, uint64_t *value) {
    HANDLE file = save_files[slot].handle;
    unsigned mode = save_files[slot].mode;
    if (!file) return FS_NOT_FOUND;
    LARGE_INTEGER file_size;
    if (command == 4) {
        if(dev_save_active)fprintf(stderr,"DEVFS size slot=%u mode=%u\n",slot,mode);
        if (!GetFileSizeEx(file, &file_size)) return save_result(GetLastError());
        *value = (uint64_t)file_size.QuadPart;
        return 0;
    }
    if (command == 2) {
        if (!(mode & 2) || FlushFileBuffers(file)) return 0;
        return save_result(GetLastError());
    }
    if (command == 3 && payload + 6 <= end) {
        if(dev_save_active)fprintf(stderr,"DEVFS resize slot=%u mode=%u\n",slot,mode);
        LARGE_INTEGER size;
        memcpy(&size.QuadPart, m + payload + 4, 8);
        if (size.QuadPart < 0 || !(mode & 2)) return FS_INVALID_MODE;
        if (SetFilePointerEx(file, size, NULL, FILE_BEGIN) && SetEndOfFile(file)) return 0;
        return save_result(GetLastError());
    }
    if ((command != 0 && command != 1) || payload + 10 > end) return FS_INVALID_MODE;
    unsigned write = command == 1;
    if (!(mode & (write ? 2u : 1u))) return FS_INVALID_MODE;
    unsigned descriptors = (m[0] >> (write ? 20 : 24)) & 15;
    if (descriptors != 1) return FS_INVALID_MODE;
    const uint32_t *d = m + descriptor + ((m[0] >> 16) & 15) * 2 +
                        (write ? 0 : ((m[0] >> 20) & 15) * 3);
    uint64_t address = d[1] | ((uint64_t)(d[2] >> 28) << 32) |
                       ((uint64_t)((d[2] >> 2) & 0x3fffff) << 36);
    uint64_t capacity = d[0] | ((uint64_t)((d[2] >> 24) & 15) << 32);
    LARGE_INTEGER offset;
    uint64_t length;
    memcpy(&offset.QuadPart, m + payload + 6, 8);
    memcpy(&length, m + payload + 8, 8);
    if (offset.QuadPart < 0 || length > capacity || length > MAXDWORD ||
        length > INT64_MAX - (uint64_t)offset.QuadPart || (length && !address))
        return FS_INVALID_MODE;
    if(dev_save_active){
        LARGE_INTEGER actual={0};GetFileSizeEx(file,&actual);
        fprintf(stderr,"DEVFS io slot=%u write=%u mode=%u offset=%lld length=%llu actual=%lld\n",slot,write,mode,(long long)offset.QuadPart,(unsigned long long)length,(long long)actual.QuadPart);
    }
    if (write && !(mode & 4)) {
        if (!GetFileSizeEx(file, &file_size)) return save_result(GetLastError());
        if ((uint64_t)offset.QuadPart + length > (uint64_t)file_size.QuadPart) return FS_NO_APPEND;
    }
    if (!SetFilePointerEx(file, offset, NULL, FILE_BEGIN)) return save_result(GetLastError());
    if (!length) return 0;
    DWORD transferred = 0;
    void *buffer = (void *)(uintptr_t)address;
    if (write) {
        if (!WriteFile(file, buffer, (DWORD)length, &transferred, NULL)) return save_result(GetLastError());
        if (transferred != length) return FS_IO_ERROR;
        if ((m[payload + 4] & 1) && !FlushFileBuffers(file)) return save_result(GetLastError());
    } else {
        if (!ReadFile(file, buffer, (DWORD)length, &transferred, NULL)) return save_result(GetLastError());
        *value = transferred;
    }
    return 0;
}

static unsigned save_filesystem_cmd(unsigned command, const uint32_t *m, unsigned payload,
                                    unsigned descriptor, unsigned end, uint64_t *value, unsigned *child) {
    if (command == 10) {
        for (unsigned slot = 0; slot < SAVE_FILE_COUNT; ++slot)
            if (save_files[slot].handle && (save_files[slot].mode & 2) &&
                !FlushFileBuffers(save_files[slot].handle)) return save_result(GetLastError());
        return 0;
    }
    char path[256];
    if (!save_path(m, descriptor, path)) return FS_INVALID_PATH;
    unsigned mount = save_mount();
    if (mount) return mount;
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return FS_INVALID_PATH;
    if (command == 7) {
        if (attributes == INVALID_FILE_ATTRIBUTES) return save_result(GetLastError());
        *value = !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        return 0;
    }
    if (command == 0 && payload + 8 <= end) {
        LARGE_INTEGER size;
        memcpy(&size.QuadPart, m + payload + 6, 8);
        if (size.QuadPart < 0) return FS_INVALID_MODE;
        HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE) return save_result(GetLastError());
        unsigned result = 0;
        if (!SetFilePointerEx(file, size, NULL, FILE_BEGIN) || !SetEndOfFile(file))
            result = save_result(GetLastError());
        CloseHandle(file);
        if (result) DeleteFileA(path);
        return result;
    }
    if (command == 1) return DeleteFileA(path) ? 0 : save_result(GetLastError());
    if (command == 8 && payload + 5 <= end) {
        if(dev_save_active)fprintf(stderr,"DEVFS open mode=%u path=%s\n",m[payload+4],path);
        unsigned mode = m[payload + 4];
        if (!(mode & 3) || (mode & ~7u)) return FS_INVALID_MODE;
        unsigned slot = 0;
        while (slot < SAVE_FILE_COUNT && save_files[slot].handle) ++slot;
        if (slot == SAVE_FILE_COUNT) return FS_IO_ERROR;
        DWORD access = (mode & 1 ? GENERIC_READ : 0) | (mode & 2 ? GENERIC_WRITE : 0);
        HANDLE file = CreateFileA(path, access, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE) return save_result(GetLastError());
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(file, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            ((mode & 2) && info.nNumberOfLinks != 1)) {
            CloseHandle(file);
            return FS_INVALID_PATH;
        }
        save_files[slot].handle = file;
        save_files[slot].mode = mode;
        *child = SAVE_FILE_BASE + slot;
        return 0;
    }
    fprintf(stderr, "Unhandled save filesystem command %u\n", command);
    return FS_INVALID_MODE;
}

static void save_command(uint32_t *m, unsigned object, unsigned command,
                         unsigned payload, unsigned descriptor, unsigned end) {
    uint64_t value = 0;
    unsigned child = 0;
    unsigned size = 0;
    AcquireSRWLockExclusive(&save_lock);
    unsigned result;
    if (object == SAVE_FILESYSTEM) {
        result = save_filesystem_cmd(command, m, payload, descriptor, end, &value, &child);
        if (command == 7) size = 4;
    } else {
        result = save_file_cmd(object - SAVE_FILE_BASE, command, m, payload, descriptor, end, &value);
        if (command == 0 || command == 4) size = 8;
    }
    ReleaseSRWLockExclusive(&save_lock);
    domain_reply(m, result, value, size, child, 0);
}
