/* Included after save_lock in the native filesystem adapter. */
static volatile LONG dev_save_active;
static char dev_save_filename[256];
static LONG dev_save_sequence;

const char *catherine_dev_save_path(void) {
    return InterlockedCompareExchange(&dev_save_active, 0, 0) ? dev_save_filename : "save.dat";
}

static int dev_save_new_path(char candidate[256]) {
    char folder[256];
    DWORD count = GetTempPathA(sizeof(folder), folder);
    if (!count || count >= sizeof(folder)) return -ERROR_BUFFER_OVERFLOW;
    int written = snprintf(candidate, 256, "%sCatherineDev-%lu-%llu-%ld", folder,
                           GetCurrentProcessId(), (unsigned long long)GetTickCount64(),
                           InterlockedIncrement(&dev_save_sequence));
    if (written < 0 || (unsigned)written + sizeof("\\save.dat") > 256) return -ERROR_BUFFER_OVERFLOW;
    if (!CreateDirectoryA(candidate, NULL)) return -(int)GetLastError();
    strcat(candidate, "\\save.dat");
    return 1;
}

/* Called before any guest thread starts. Preserve existing system options and
   direct creation/migration on first launch into the same private filesystem. */
int catherine_dev_save_isolate_startup(void) {
    char candidate[256];
    int result = dev_save_new_path(candidate);
    if (result != 1) return result;
    if (!CopyFileA("save.dat", candidate, TRUE)) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return -(int)error;
        if (!CopyFileA("save\\system_dat", candidate, TRUE)) {
            error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return -(int)error;
        }
    }
    memcpy(dev_save_filename, candidate, strlen(candidate) + 1);
    InterlockedExchange(&dev_save_active, 1);
    return 1;
}

/* Standalone movie playback does not need a checkpoint. Preserve the current
   session save and isolate later writes just as direct startup does. */
int catherine_dev_save_isolate_movie(void) {
    AcquireSRWLockExclusive(&save_lock);
    if (dev_save_active) {
        ReleaseSRWLockExclusive(&save_lock);
        return 1;
    }
    for (unsigned i = 0; i < SAVE_FILE_COUNT; ++i) {
        if (save_files[i].handle) {
            ReleaseSRWLockExclusive(&save_lock);
            return 0;
        }
    }
    const int result = catherine_dev_save_isolate_startup();
    ReleaseSRWLockExclusive(&save_lock);
    return result;
}

/* The native SaveLoad read copies a single 0x8000-byte record from slot << 15. */
static int dev_save_read_slot(HANDLE file, unsigned slot, unsigned char *record) {
    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)slot * 0x8000;
    DWORD bytes = 0;
    if (!SetFilePointerEx(file, offset, NULL, FILE_BEGIN) ||
        !ReadFile(file, record, 0x8000, &bytes, NULL))
        return -(int)GetLastError();
    uint32_t version;
    if (bytes != 0x8000) return -ERROR_INVALID_DATA;
    memcpy(&version, record + 0x3674, sizeof(version));
    return record[0] == slot && record[1] == 1 && version == 0x10300 ? 1 : -ERROR_INVALID_DATA;
}

int catherine_dev_save_begin_record(const char *source_utf8, unsigned slot, unsigned char *record) {
    if (!source_utf8 || !*source_utf8) return -ERROR_INVALID_PARAMETER;
    if (record && slot >= 20) return -ERROR_INVALID_PARAMETER;
    wchar_t source[32768], destination[32768];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source_utf8, -1,
                             source, sizeof(source) / sizeof(source[0])))
        return -(int)GetLastError();
    AcquireSRWLockExclusive(&save_lock);
    for (unsigned i = 0; i < SAVE_FILE_COUNT; ++i) {
        if (save_files[i].handle) { ReleaseSRWLockExclusive(&save_lock); return 0; }
    }
    int result = 1;
    HANDLE input = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    LARGE_INTEGER length;
    BY_HANDLE_FILE_INFORMATION info;
    if (input == INVALID_HANDLE_VALUE) { result = -(int)GetLastError(); goto finished; }
    if (!GetFileInformationByHandle(input, &info) || !GetFileSizeEx(input, &length))
        result = -(int)GetLastError();
    else if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
             length.QuadPart != 0xd2000)
        result = -ERROR_INVALID_DATA;
    else if (record)
        result = dev_save_read_slot(input, slot, record);
    /* Keep the verified file open without write/delete sharing until CopyFile completes. */
    if (result != 1) goto finished;
    char candidate[256];
    result = dev_save_new_path(candidate);
    if (result != 1) goto finished;
    if (!MultiByteToWideChar(CP_ACP, 0, candidate, -1, destination,
                             sizeof(destination) / sizeof(destination[0]))) {
        result = -(int)GetLastError(); goto finished;
    }
    if (!CopyFileW(source, destination, TRUE)) { result = -(int)GetLastError(); goto finished; }
    memcpy(dev_save_filename, candidate, strlen(candidate) + 1);
    InterlockedExchange(&dev_save_active, 1);
finished:
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    ReleaseSRWLockExclusive(&save_lock);
    return result;
}

int catherine_dev_save_begin(const char *source_utf8) {
    return catherine_dev_save_begin_record(source_utf8, 0, NULL);
}
