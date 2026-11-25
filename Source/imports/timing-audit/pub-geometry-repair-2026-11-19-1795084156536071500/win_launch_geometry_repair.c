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

static void configure_high_refresh_renderer(void) {
    // Resolve settings beside the executable before creating the guest or window.
    (void)catherine_internal_render_height();

    static const char *const defaults[][2] = {
        {"CATHERINE_Q1_NATIVE_SEMAPHORES", "1"},
        {"CATHERINE_FAST_NVN_DRIVER", "1"},
        {"CATHERINE_NVN_ATTACHMENT_BARRIERS", "1"},
        {"CATHERINE_GUEST_PRECISE_SLEEP", "1"},
        {"CATHERINE_NVN_LIST_RESERVE", "1"},
        {"NX_PASS_PIPELINE_ATTACHMENTS", "1"},
        {"NX_SHADER_SCOPE_TWO_WAY", "1"},
        {"NX_WATCHED_GEOMETRY", "2"},
        {"NX_WATCHED_GEOMETRY_RECYCLE", "1"},
        {"NX_WRITTEN_PAGE_PREFILTER", "1"},
        {"NX_DEDUP_FLAT", "1"},
        {"NX_ASYNC_GPU", "2"},
        {"NX_REPLAY_THREAD", "1"},
        {"NX_DEFERRED_SNAPSHOT", "1"},
        {"NX_GPU_TIMELINE_SYNC", "1"},
        {"NX_CPU_ASYNC", "1"},
        {"NX_CPU_MORPH", "1"},
        {"NX_CPU_SKIN", "1"},
        {"NX_CPU_SKIN_FUSE_VALIDATION", "1"},
        {"NX_CPU_SKIN_NAN_TANGENTS", "1"},
        {"NX_CPU_SKIN_NAN_POSITIONS", "1"},
        {"NX_SHADOW_SNAPSHOT_CACHE", "1"},
        {"NX_CPU_LAZY_TAIL", "1"},
        {"NX_CPU_SIMD_WRITEBACK", "1"},
        {"NX_DESCRIPTOR_BUFFER", "1"},
        {"NX_COMPACT_DESCRIPTOR_VALUES", "1"},
        {"NX_UNIFORM_STATIC_EXTENT", "1"},
        {"CATHERINE_SKIN_DEVICE_MIRROR", "1"},
        {"CATHERINE_VULKAN_TEMPLATE_ADMISSION", "1"},
        {"CATHERINE_ASYNC_CPU_SKIN", "0"},
        {"CATHERINE_ZERO_INPLACE_MORPH", "1"},
        {"CATHERINE_VULKAN_DESCRIPTOR_LAYOUTS", "1"},
        {"CATHERINE_VULKAN_DESCRIPTOR_SET2_REUSE", "0"},  // perf 2026-11-22: was 1 (see below)
        {"CATHERINE_SKIN_REPEAT_JOINTS", "0"},
        {"CATHERINE_VULKAN_PREPARED_IMAGE", "1"},
        {"CATHERINE_INACTIVE_C3", "1"},
        {"CATHERINE_MATERIAL_PLAN_LOOKUP", "1"},
        {"CATHERINE_VULKAN_PIPELINE_STATE_BUCKETS", "1"},
        {"CATHERINE_VULKAN_NATIVE_PIPELINE_MEMO", "1"},
        {"CATHERINE_MORPH_FINITE_SCAN", "1"},
        {"CATHERINE_NATIVE_VERTEX_RANGE", "1"},
        {"CATHERINE_VULKAN_SELECTIVE_DYNAMIC_UBO", "1"},
        {"NX_DYNAMIC_UBO", "0"},
        // perf 2026-11-22: the legacy (reference) image-material and texture-binding paths: same descriptors,
        // ~2.5% faster in the Day 5 bar. Set either to 1 to use the native layer.
        {"CATHERINE_VULKAN_IMAGE_MATERIALS", "0"},
        {"CATHERINE_VULKAN_TEXTURE_BINDINGS", "0"},
        // perf 2026-11-22: the set-2 descriptor cache and native submit-resource layer cost more than they save in the
        // Day 5 bar (~3.5% and ~2%); their legacy paths produce the same descriptors and bindings.
        {"CATHERINE_VULKAN_SUBMIT_RESOURCES", "0"},
        // perf 2026-11-22 (user request): keep OBS Studio's implicit Vulkan capture layer (graphics-hook64.dll) out of
        // the game; it hooked every present. OBS window/display capture still works. Set to 0 to allow OBS game capture.
        {"DISABLE_VULKAN_OBS_CAPTURE", "1"},
    };
    for (size_t index = 0; index < ARRAYSIZE(defaults); ++index)
        if (!getenv(defaults[index][0])) _putenv_s(defaults[index][0], defaults[index][1]);

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

static const wchar_t *read_launch_arguments(wchar_t *path, DWORD capacity) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return L"Could not read the launch arguments.";
    const wchar_t *error = NULL;
    uint32_t target = 0;
    BOOL startup_menu = FALSE;
    for (int index = 1; index < argc; ++index) {
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
    if (argument_error) return startup_error(argument_error, !redirected);
    wchar_t directory[32768];
    DWORD length = GetModuleFileNameW(NULL, directory, ARRAYSIZE(directory));
    wchar_t *filename = length && length < ARRAYSIZE(directory) ? wcsrchr(directory, L'\\') : NULL;
    if (!filename)
        return startup_error(L"Could not locate the Catherine executable.", !redirected);
    filename[1] = L'\0';
    // Keep saves, settings and the default package relative to the executable.
    if (!SetCurrentDirectoryW(directory))
        return startup_error(L"Could not open the folder containing Catherine.exe.", !redirected);
    if ((catherine_dev_loader_pending() || catherine_dev_loader_startup_menu()) &&
        catherine_dev_save_isolate_startup() != 1)
        return startup_error(L"Could not prepare a temporary save for the requested DEV launch.", !redirected);

    FILE *log;
    if (!redirected) _wfreopen_s(&log, L"catherine-runtime.log", L"w", stderr);
    if (!has_standard_handle(STD_OUTPUT_HANDLE))
        _wfreopen_s(&log, L"catherine-startup.log", L"w", stdout);

    if (catherine_asset_setup(pack_path))
        return startup_error(L"Asset setup did not complete. See assets-errors.log beside Catherine.exe.",
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
