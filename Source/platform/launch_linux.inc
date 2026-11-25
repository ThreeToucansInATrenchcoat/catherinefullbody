#include "asset_pak.h"
#include "renderer_defaults.h"
#include "dev-start/generated/include/dev_loader_native.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int catherine_dev_save_isolate_startup(void);
int catherine_guest_main(void);
int catherine_linux_run(int (*guest)(void));

int main(int argc, char **argv) {
    uint32_t target = 0;
    int menu = 0;
    char *pack = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--assets-pak") && !pack && i + 1 < argc) {
            pack = realpath(argv[++i], NULL);
            if (!pack) { perror("Could not resolve --assets-pak"); return 2; }
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

    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || length == sizeof(executable) - 1) return 2;
    executable[length] = 0;
    *strrchr(executable, '/') = 0;
    if (chdir(executable)) { perror("Could not enter the executable directory"); return 2; }

    int packed = pack ? asset_pak_initialize_from_native_path(pack) : asset_pak_initialize();
    free(pack);
    if (packed < 0) {
        fprintf(stderr, "Could not open assets.pak: %s\n", asset_pak_error());
        return 2;
    }
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
