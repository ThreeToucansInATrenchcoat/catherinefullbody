#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "asset_pak.h"
#include "asset_setup.h"
#include "dev-start/generated/include/dev_loader_native.h"

int main(void);
int catherine_dev_save_isolate_startup(void);

#ifdef CATHERINE_HIGH_REFRESH_BUILD
#include "catherine_refresh.h"
#include "../../../platform/renderer_defaults.h"

static void configure_high_refresh_renderer(void) {
    // Resolve settings beside the executable before creating the guest or window.
    (void)catherine_internal_render_height();

    for (size_t index = 0; index < ARRAYSIZE(catherine_renderer_defaults); ++index)
        if (!getenv(catherine_renderer_defaults[index][0])) _putenv_s(catherine_renderer_defaults[index][0], catherine_renderer_defaults[index][1]);

    const char *geometry = getenv("NX_WATCHED_GEOMETRY");
    if (geometry && (geometry[0] == '1' || geometry[0] == '2') && geometry[1] == '\0' &&
        !getenv("NX_LINEAR_HEAP_WRITE_WATCH"))
        _putenv_s("NX_LINEAR_HEAP_WRITE_WATCH", "1");
}
#endif

static BOOL has_standard_handle(DWORD kind) {
    HANDLE handle = GetStdHandle(kind);
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

static int startup_error(const wchar_t *message, BOOL show_dialog) {
    fwprintf(stderr, L"%ls\n", message);
    if (show_dialog)
        MessageBoxW(NULL, message, L"Catherine: Full Body", MB_OK | MB_ICONERROR);
    return 2;
}

static int prepare_only;
static wchar_t game_path[32768], update_path[32768];

static const wchar_t *read_launch_arguments(wchar_t *path, DWORD capacity) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return L"Could not read the launch arguments.";
    for (int i = 1; i < argc; ++i) if (!wcscmp(argv[i], L"--prepare")) prepare_only = 1;
    const wchar_t *error = NULL;
    uint32_t target = 0;
    BOOL startup_menu = FALSE;
    for (int index = 1; index < argc; ++index) {
        if (wcscmp(argv[index], L"--prepare") == 0) { prepare_only = 1; continue; }
        wchar_t *source = NULL;
        const wchar_t *value = NULL;
        if (!wcsncmp(argv[index], L"--game=", 7)) { source = game_path; value = argv[index] + 7; }
        else if (!wcsncmp(argv[index], L"--update=", 9)) { source = update_path; value = argv[index] + 9; }
        else if (!wcscmp(argv[index], L"--game") || !wcscmp(argv[index], L"--update")) {
            source = !wcscmp(argv[index], L"--game") ? game_path : update_path;
            if (++index < argc) value = argv[index];
        }
        if (source) {
            if (*source || !value || !*value || !wcsncmp(value, L"--", 2)) {
                error = L"Use --game=FOLDER and optionally --update=FOLDER for your extracted Switch files."; break;
            }
            DWORD count = GetFullPathNameW(value, 32768, source, NULL);
            DWORD attributes = count && count < 32768 ? GetFileAttributesW(source) : INVALID_FILE_ATTRIBUTES;
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                error = L"The supplied game or update folder does not exist or cannot be read."; break;
            }
            continue;
        }
        if (wcscmp(argv[index], L"--dev-level") == 0) {
            if (target || startup_menu || ++index >= argc || argv[index][0] < L'0' || argv[index][0] > L'9') {
                error = L"Use --dev-level followed by one numeric destination ID, or use --dev-menu.";
                break;
            }
            wchar_t *end;
            const unsigned long long value = wcstoull(argv[index], &end, 10);
            if (*end || !value || value > UINT32_MAX) {
                error = L"The --dev-level destination ID must be a positive decimal number.";
                break;
            }
            target = (uint32_t)value;
            continue;
        }
        if (wcscmp(argv[index], L"--dev-menu") == 0) {
            if (target || startup_menu) {
                error = L"Choose one startup option, --dev-level ID or --dev-menu.";
                break;
            }
            startup_menu = TRUE;
            continue;
        }
        if (wcscmp(argv[index], L"--assets-pak") != 0) continue;
        if (*path || index + 1 >= argc || !*argv[index + 1] ||
            wcsncmp(argv[index + 1], L"--", 2) == 0) {
            error = L"Use --assets-pak followed by the path to your shared asset pack.";
            break;
        }
        // Resolve relative paths before the launcher changes the working directory.
        const DWORD length = GetFullPathNameW(argv[++index], capacity, path, NULL);
        if (!length || length >= capacity) {
            error = L"Could not resolve the path supplied to --assets-pak.";
            break;
        }
    }
    LocalFree(argv);
    if (error) return error;
    if (prepare_only && (target || startup_menu || *path)) return L"--prepare cannot be combined with developer or shared-pack options.";
    if (target) {
        CatherineDevLoadRequest request = {0};
        request.version = CATHERINE_DEV_LOADER_VERSION;
        request.target_id = target;
        request.difficulty = -1;
        request.route = -1;
        if (!catherine_dev_loader_request(&request))
            return L"Unknown or unavailable destination ID. Run --dev-menu to see the available destinations and IDs.";
    }
    if (startup_menu) catherine_dev_loader_set_startup_menu(1);
    return NULL;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;

    BOOL redirected = has_standard_handle(STD_ERROR_HANDLE);
    wchar_t pack_path[32768] = {0};
    const wchar_t *argument_error = read_launch_arguments(pack_path, ARRAYSIZE(pack_path));
    if (argument_error) return startup_error(argument_error, !redirected && !prepare_only);
    wchar_t directory[32768];
    DWORD length = GetModuleFileNameW(NULL, directory, ARRAYSIZE(directory));
    wchar_t *filename = length && length < ARRAYSIZE(directory) ? wcsrchr(directory, L'\\') : NULL;
    if (!filename)
        return startup_error(L"Could not locate the Catherine executable.", !redirected);
    filename[1] = L'\0';
    wchar_t data[32768], manifest[32768];
    DWORD data_length = GetEnvironmentVariableW(L"RECOMPLAUNCHER_DATA", data, ARRAYSIZE(data));
    if (data_length) {
        if (data_length >= ARRAYSIZE(data) || !SetCurrentDirectoryW(data))
            return startup_error(L"RECOMPLAUNCHER_DATA must name an existing writable folder.", FALSE);
        if (!GetCurrentDirectoryW(ARRAYSIZE(data), data) || _wputenv_s(L"RECOMPLAUNCHER_DATA", data) != 0)
            return startup_error(L"Could not resolve the launcher data directory.", FALSE);
        if (swprintf_s(manifest, ARRAYSIZE(manifest), L"%lsassets-required.json", directory) < 0 ||
            !CopyFileW(manifest, L"assets-required.json", FALSE))
            return startup_error(L"Could not copy assets-required.json into the launcher data directory.", FALSE);
        if (!CreateDirectoryW(L"cache", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return startup_error(L"Could not create the launcher cache directory.", FALSE);
        _putenv_s("NX_DATA_ROOT", ".");
        wcscpy_s(directory, ARRAYSIZE(directory), data);
    }
    // Use the launcher data folder when supplied; ordinary launches use the executable folder.
    if (!SetCurrentDirectoryW(directory))
        return startup_error(L"Could not open the folder containing Catherine.exe.", !redirected);
    if ((catherine_dev_loader_pending() || catherine_dev_loader_startup_menu()) &&
        catherine_dev_save_isolate_startup() != 1)
        return startup_error(L"Could not prepare a temporary save for the requested DEV launch.", !redirected);

    FILE *log;
    if (!redirected) _wfreopen_s(&log, L"catherine-runtime.log", L"w", stderr);
    if (!has_standard_handle(STD_OUTPUT_HANDLE))
        _wfreopen_s(&log, L"catherine-startup.log", L"w", stdout);

    int setup_status = catherine_asset_setup_launch(pack_path, game_path, update_path, prepare_only);
    if (prepare_only) return setup_status;
    if (setup_status)
        return startup_error(L"Asset setup did not complete. See assets-errors.log in the game data folder.",
                             !redirected && !getenv("NX_HEADLESS"));
    int packed = *pack_path ? asset_pak_initialize_from(pack_path) : asset_pak_initialize();
    if (packed < 0) {
        if (*pack_path) {
            fwprintf(stderr, L"Could not read %ls: %hs\n", pack_path, asset_pak_error());
            return startup_error(L"Could not read the shared asset pack. Check the file passed to --assets-pak.",
                                 !redirected);
        }
        fwprintf(stderr, L"assets.pak: %hs\n", asset_pak_error());
        return startup_error(L"assets.pak could not be opened after the launch attempt. See assets-errors.log for details.", !redirected);
    }
    if (!packed) { // Development and legacy packages may still use loose assets.
        const DWORD loose_assets = GetFileAttributesW(L"assets");
        if (loose_assets == INVALID_FILE_ATTRIBUTES || !(loose_assets & FILE_ATTRIBUTE_DIRECTORY))
            return startup_error(L"Missing assets.pak. Place it beside Catherine.exe or pass its path with --assets-pak.",
                                 !redirected);
        const wchar_t *required[] = {
            L"assets\\main.image", L"assets\\subsdk0.image", L"assets\\subsdk1.image", L"assets\\sdk.image",
            L"assets\\romfs\\romfs.bin", L"assets\\spirv\\results.tsv"
        };
        for (size_t index = 0; index < ARRAYSIZE(required); ++index) {
            DWORD attributes = GetFileAttributesW(required[index]);
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                wchar_t message[256];
                swprintf_s(message, ARRAYSIZE(message), L"Missing %ls. Run the Catherine asset extractor to prepare assets.pak.",
                           required[index]);
                return startup_error(message, !redirected);
            }
        }
    }
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    configure_high_refresh_renderer();
#endif
    return main();
}

#else
#include "C:/catherine/recomp/Source/platform/launch_linux.inc"
#endif
