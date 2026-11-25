/* Included with the save filesystem lock held, or before guest startup. */
static volatile LONG dev_save_active;
static char dev_save_filename[256];

const char *catherine_dev_save_path(void) {
    return InterlockedCompareExchange(&dev_save_active, 0, 0) ? dev_save_filename : "save.dat";
}

static int dev_save_copy(const char *source, int checkpoint, unsigned slot, unsigned char *record) {
    HANDLE input = CreateFileA(source, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (input == INVALID_HANDLE_VALUE && (checkpoint || (errno != ENOENT && errno != ENOTDIR)))
        return -errno;

    int result = 1;
    LARGE_INTEGER size;
    BY_HANDLE_FILE_INFORMATION info;
    if (input != INVALID_HANDLE_VALUE &&
        (!GetFileInformationByHandle(input, &info) || !GetFileSizeEx(input, &size))) result = -errno;
    else if (input != INVALID_HANDLE_VALUE && (info.dwFileAttributes || (checkpoint && size.QuadPart != 0xd2000)))
        result = -EINVAL;
    if (result == 1 && record) {
        LARGE_INTEGER offset = { (int64_t)slot * 0x8000 };
        DWORD count;
        if (!SetFilePointerEx(input, offset, NULL, FILE_BEGIN) ||
            !ReadFile(input, record, 0x8000, &count, NULL)) result = -errno;
        else {
            uint32_t version;
            memcpy(&version, record + 0x3674, sizeof(version));
            if (count != 0x8000 || record[0] != slot || record[1] != 1 || version != 0x10300)
                result = -EINVAL;
        }
    }
    char directory[] = "CatherineDev-XXXXXX";
    if (result == 1) {
        if (!mkdtemp(directory)) result = -errno;
        else {
            char candidate[256];
            snprintf(candidate, sizeof(candidate), "%s/save.dat", directory);
            if (input != INVALID_HANDLE_VALUE) {
                HANDLE output = CreateFileA(candidate, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
                if (output == INVALID_HANDLE_VALUE) result = -errno;
                else {
                    char buffer[32768];
                    DWORD count, written;
                    if (!SetFilePointerEx(input, (LARGE_INTEGER){0}, NULL, FILE_BEGIN)) result = -errno;
                    while (result == 1) {
                        if (!ReadFile(input, buffer, sizeof(buffer), &count, NULL)) result = -errno;
                        else if (!count) break;
                        else if (!WriteFile(output, buffer, count, &written, NULL)) result = -errno;
                    }
                    if (result == 1 && !FlushFileBuffers(output)) result = -errno;
                    CloseHandle(output);
                }
            }
            if (result == 1) {
                memcpy(dev_save_filename, candidate, strlen(candidate) + 1);
                InterlockedExchange(&dev_save_active, 1);
            } else {
                unlink(candidate);
                rmdir(directory);
            }
        }
    }
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    return result;
}

int catherine_dev_save_isolate_startup(void) {
    const char *active = catherine_dev_save_path();
    const char *source = active;
    if (!strcmp(active, "save.dat") && GetFileAttributesA(active) == INVALID_FILE_ATTRIBUTES &&
        errno == ENOENT) source = "save/system_dat";
    return dev_save_copy(source, 0, 0, NULL);
}

static int save_handles_open(void) {
    for (unsigned i = 0; i < SAVE_FILE_COUNT; ++i)
        if (save_files[i].handle) return 1;
    return 0;
}

int catherine_dev_save_isolate_movie(void) {
    AcquireSRWLockExclusive(&save_lock);
    int result = dev_save_active ? 1 : save_handles_open() ? 0 : catherine_dev_save_isolate_startup();
    ReleaseSRWLockExclusive(&save_lock);
    return result;
}

int catherine_dev_save_begin_record(const char *source_utf8, unsigned slot, unsigned char *record) {
    if (!source_utf8 || !*source_utf8 || (record && slot >= 20)) return -EINVAL;
    AcquireSRWLockExclusive(&save_lock);
    int result = save_handles_open() ? 0 : dev_save_copy(source_utf8, 1, slot, record);
    ReleaseSRWLockExclusive(&save_lock);
    return result;
}

int catherine_dev_save_begin(const char *source_utf8) {
    return catherine_dev_save_begin_record(source_utf8, 0, NULL);
}
