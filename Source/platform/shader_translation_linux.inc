#include "../imports/base/asset_setup.h"
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

namespace {
namespace fs = std::filesystem;

bool shader_file(const fs::path& path, void** data, size_t* length, bool text) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    auto bytes = input.tellg();
    if (bytes < 0 || bytes > 32 * 1024 * 1024 || (!text && (!bytes || bytes % 4))) return false;
    auto* buffer = static_cast<char*>(std::malloc(static_cast<size_t>(bytes) + 1));
    if (!buffer) return false;
    input.seekg(0);
    if (!input.read(buffer, bytes)) {
        std::free(buffer);
        return false;
    }
    buffer[bytes] = 0;
    *data = buffer;
    *length = static_cast<size_t>(bytes);
    return true;
}

bool shader_cache_read(const fs::path& dir, void** binary, size_t* length, char** resources) {
    void* text = nullptr;
    size_t text_size;
    if (!shader_file(dir / "shader.resources", &text, &text_size, true)) return false;
    if (!shader_file(dir / "shader.spv", binary, length, false)) {
        std::free(text);
        return false;
    }
    *resources = static_cast<char*>(text);
    return true;
}

bool write_shader_input(const fs::path& path, const uint8_t* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data), size);
    out.close();
    return out.good();
}
}

extern "C" int catherine_translate_shader(const char* hash, const uint8_t* code, size_t size,
                                           const uint8_t* control, void** binary, size_t* length,
                                           char** resources) {
    if (std::strlen(hash) != 16 || std::strspn(hash, "0123456789abcdef") != 16 ||
        !size || size > 16 * 1024 * 1024) return 0;
    try {
        struct stat executable;
        if (stat("/proc/self/exe", &executable)) return 0;
        const auto stamp = std::to_string(executable.st_mtim.tv_sec) + "-" + std::to_string(executable.st_mtim.tv_nsec);
        auto cache = catherine_data_root() / (getenv("RECOMPLAUNCHER_DATA") ? "cache" : "assets-setup-work") / "runtime-shaders" / stamp;
        fs::create_directories(cache);
        auto ready = cache / hash;
        if (shader_cache_read(ready, binary, length, resources)) return 1;

        std::string scratch = (cache / ".translate-XXXXXX").string();
        if (!mkdtemp(scratch.data())) return 0;
        struct Cleanup {
            fs::path directory;
            ~Cleanup() { std::error_code error; fs::remove_all(directory, error); }
        } cleanup{scratch};
        auto code_path = (cleanup.directory / "code.bin").string();
        auto ctrl_path = (cleanup.directory / "control.bin").string();
        auto output = (cleanup.directory / "shader").string();
        if (!write_shader_input(code_path, code, size) || !write_shader_input(ctrl_path, control, 0x800))
            return 0;
        char program[] = "/proc/self/exe", option[] = "--translate-shader";
        char* args[] = {program, option, code_path.data(), ctrl_path.data(), output.data(), nullptr};
        pid_t child;
        if (posix_spawn(&child, program, nullptr, nullptr, args, environ)) return 0;
        int status;
        pid_t result;
        do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
        if (result < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
            !shader_cache_read(cleanup.directory, binary, length, resources)) return 0;

        std::error_code error;
        fs::remove(code_path, error);
        fs::remove(ctrl_path, error);
        fs::rename(cleanup.directory, ready, error);
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Runtime shader translation for %s: %s\n", hash, e.what());
        return 0;
    }
}
