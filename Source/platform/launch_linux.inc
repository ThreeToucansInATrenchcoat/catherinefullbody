#include "asset_pak.h"
#include "asset_setup_window_linux.h"
#include "renderer_defaults.h"
#include "dev-start/generated/include/dev_loader_native.h"
#include <SDL2/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

int catherine_dev_save_isolate_startup(void);
int catherine_guest_main(void);
int catherine_linux_run(int (*guest)(void));

extern const unsigned char catherine_asset_setup_start[], catherine_asset_setup_end[];

static int asset_setup_error(const char *message) {
    fprintf(stderr, "%s\n", message);
    const char *headless = getenv("NX_HEADLESS");
    if ((!headless || strcmp(headless, "1")) && (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Catherine asset setup", message, NULL);
    return 2;
}

static int asset_tool_error(const char *text) {
    fprintf(stderr, "%s\n", text);
    return 127;
}

static int run_asset_tool(const char *root, char **args, char **progress_arg) {
    size_t size = (uintptr_t)catherine_asset_setup_end - (uintptr_t)catherine_asset_setup_start;
    char work[4096], temporary[4096], installer[4096];
    if (snprintf(work, sizeof work, "%s/assets-setup-work", root) >= (int)sizeof work ||
        snprintf(temporary, sizeof temporary, "%s/.setup-XXXXXX", work) >= (int)sizeof temporary)
        return asset_tool_error("The installer path is too long.");
    if (mkdir(work, 0700) && errno != EEXIST)
        return asset_tool_error("Could not create the asset setup work directory.");
    if (!mkdtemp(temporary))
        return asset_tool_error("Could not create a directory for the embedded asset installer.");
    if (snprintf(installer, sizeof installer, "%s/CatherineAssetSetup", temporary) >= (int)sizeof installer) {
        rmdir(temporary);
        return asset_tool_error("The installer path is too long.");
    }
    int fd = open(installer, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    if (fd < 0) {
        rmdir(temporary);
        return asset_tool_error("Could not unpack the embedded asset installer.");
    }
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(fd, catherine_asset_setup_start + written, size - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += (size_t)n;
    }
    int failed = written != size;
    if (close(fd) || failed) {
        unlink(installer);
        rmdir(temporary);
        return asset_tool_error("Could not unpack the embedded asset installer.");
    }

    int events[2] = {-1, -1};
    if (progress_arg && !pipe(events)) {
        if (fcntl(events[0], F_SETFL, O_NONBLOCK) < 0) {
            close(events[0]);
            close(events[1]);
            events[0] = events[1] = -1;
        }
    }
    args[0] = installer;
    if (events[0] >= 0) *progress_arg = "--native-progress";
    pid_t child = fork();
    if (child < 0) {
        if (events[0] >= 0) { close(events[0]); close(events[1]); }
        unlink(installer);
        rmdir(temporary);
        return asset_tool_error("Could not start the embedded asset tool.");
    }
    if (!child) {
        if (events[0] >= 0) {
            close(events[0]);
            if (dup2(events[1], STDOUT_FILENO) < 0) _exit(127);
            if (events[1] != STDOUT_FILENO) close(events[1]);
        }
        if (!setenv("DOTNET_BUNDLE_EXTRACT_BASE_DIR", work, 1))
            execv(installer, args);
        _exit(127);
    }
    int status;
    pid_t result;
    if (events[0] >= 0) {
        close(events[1]);
        result = catherine_monitor_setup(child, events[0], &status);
    } else {
        do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    }
    unlink(installer);
    rmdir(temporary);
    if (result > 0 && WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) return 2;
    if (result < 0 || !WIFEXITED(status)) return 127;
    return WEXITSTATUS(status);
}

static int run_asset_setup(const char *root, char **argv) {
    const char *hidden = getenv("NX_HEADLESS");
    int headless = hidden && !strcmp(hidden, "1");
    char *args[] = {NULL, "--root", (char *)root, headless ? "--headless" : NULL, NULL};
    int status = run_asset_tool(root, args, !headless && (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY"))
                                            ? &args[3] : NULL);
    if (status == 127)
        return asset_setup_error("The asset installer stopped unexpectedly. See assets-errors.log.");
    if (status == 3)
        return asset_setup_error("Asset setup failed. Install KDialog or Zenity for desktop prompts and see assets-errors.log.");
    if (status != 0) return status;
    if (!setenv("CATHERINE_ASSET_SETUP_RESTART", "1", 1))
        execv("/proc/self/exe", argv);
    return asset_setup_error("Assets were installed. Start Catherine again to play.");
}

int main(int argc, char **argv) {
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || length == sizeof(executable) - 1) return 2;
    executable[length] = 0;
    *strrchr(executable, '/') = 0;
    if (argc == 5 && !strcmp(argv[1], "--translate-shader")) {
        char *args[] = {NULL, "--translate", argv[2], argv[3], argv[4], NULL};
        return run_asset_tool(executable, args, NULL);
    }

    uint32_t target = 0;
    int menu = 0;
    char *pack = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--assets-pak") && !pack && i + 1 < argc) {
            pack = realpath(argv[++i], NULL);
            if (!pack) return asset_setup_error("Could not open the specified --assets-pak path.");
        } else if (!strcmp(argv[i], "--dev-menu") && !target && !menu) {
            menu = 1;
        } else if (!strcmp(argv[i], "--dev-level") && !menu && !target && i + 1 < argc) {
            const char *value = argv[++i];
            char *end;
            errno = 0;
            unsigned long number = strtoul(value, &end, 10);
            if (errno || *end || *value < '0' || *value > '9' || !number || number > UINT32_MAX) {
                fputs("--dev-level requires a positive decimal destination ID.\n", stderr);
                return 2;
            }
            target = number;
        } else {
            fputs("Usage: catherine [--assets-pak PATH] [--dev-menu | --dev-level ID]\n", stderr);
            return 2;
        }
    }

    int restarted = getenv("CATHERINE_ASSET_SETUP_RESTART") != NULL;
    unsetenv("CATHERINE_ASSET_SETUP_RESTART");
    char recovery[4096];
    int recovery_length = snprintf(recovery, sizeof recovery, "%s/assets.pak.shader-update", executable);
    if (!pack && !restarted && recovery_length < (int)sizeof recovery && !access(recovery, F_OK))
        return run_asset_setup(executable, argv);

    if (!pack && !restarted && catherine_alt_build_mode()) return run_asset_setup(executable, argv);

    int packed = pack ? asset_pak_initialize_from_native_path(pack) : asset_pak_initialize();
    int has_shaders = packed == 1 && asset_pak_contains("spirv/results.tsv");
    int supplied = pack != NULL;
    free(pack);
    if (packed < 0) {
        char message[512];
        snprintf(message, sizeof message, "Could not open assets.pak: %s", asset_pak_error());
        return asset_setup_error(message);
    }
    if (!has_shaders) {
        if (!supplied && !restarted) return run_asset_setup(executable, argv);
        return asset_setup_error("Catherine requires an assets.pak containing the Vulkan shaders.");
    }
    if (chdir(executable)) { perror("Could not enter the executable directory"); return 2; }
    if (target) {
        CatherineDevLoadRequest request = {0};
        request.version = CATHERINE_DEV_LOADER_VERSION;
        request.target_id = target;
        request.difficulty = request.route = -1;
        if (!catherine_dev_loader_request(&request)) {
            fputs("Unknown or unavailable developer destination.\n", stderr);
            return 2;
        }
    }
    if (menu) catherine_dev_loader_set_startup_menu(1);
    if ((target || menu || getenv("CATHERINE_TEST_ISOLATE_SAVE")) && catherine_dev_save_isolate_startup() != 1) {
        fputs("Could not isolate the session save.\n", stderr);
        return 2;
    }
    for (size_t i = 0; i < sizeof(catherine_renderer_defaults) / sizeof(catherine_renderer_defaults[0]); ++i)
        setenv(catherine_renderer_defaults[i][0], catherine_renderer_defaults[i][1], 0);
    const char *geometry = getenv("NX_WATCHED_GEOMETRY");
    if (geometry && (geometry[0] == '1' || geometry[0] == '2') && !geometry[1])
        setenv("NX_LINEAR_HEAP_WRITE_WATCH", "1", 0);
    return catherine_linux_run(catherine_guest_main);
}
