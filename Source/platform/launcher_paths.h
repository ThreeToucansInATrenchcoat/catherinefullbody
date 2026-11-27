#pragma once
#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

// Supplied resources stay beside the executable, even with launcher data roots.
inline std::filesystem::path catherine_executable_root() {
#ifdef _WIN32
    wchar_t executable[32768];
    const DWORD size = GetModuleFileNameW(nullptr, executable, 32768);
    return size && size < 32768 ? std::filesystem::path(executable).parent_path() : std::filesystem::path{};
#else
    std::error_code error;
    auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path();
#endif
}

// Launcher-managed data is opt-in. Ordinary launches retain their existing paths.
inline std::filesystem::path catherine_data_root() {
#ifdef _WIN32
    if (const wchar_t* data = _wgetenv(L"RECOMPLAUNCHER_DATA"); data && *data)
#else
    if (const char* data = std::getenv("RECOMPLAUNCHER_DATA"); data && *data)
#endif
        return std::filesystem::path(data);
    return catherine_executable_root();
}
