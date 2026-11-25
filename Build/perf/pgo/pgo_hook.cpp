// Test-only object linked into the PGO *instrumented* build (never into catherine.exe).
// The benchmark harness terminates the game, so counters are reset/dumped on demand:
//   Local\CatherinePGOReset-<pid>  -> zero all counters (start of the gameplay sample)
//   Local\CatherinePGODump-<pid>   -> write LLVM_PROFILE_FILE, then signal ...Done-<pid>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>

extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_reset_counters(void);

namespace {
DWORD WINAPI pgo_thread(void*) {
    const DWORD pid = GetCurrentProcessId();
    wchar_t name[96];
    swprintf(name, 96, L"Local\\CatherinePGOReset-%lu", pid);
    HANDLE reset = CreateEventW(nullptr, FALSE, FALSE, name);
    swprintf(name, 96, L"Local\\CatherinePGODump-%lu", pid);
    HANDLE dump = CreateEventW(nullptr, FALSE, FALSE, name);
    swprintf(name, 96, L"Local\\CatherinePGODone-%lu", pid);
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, name);
    if (!reset || !dump || !done) return 1;
    HANDLE both[2] = {reset, dump};
    for (;;) {
        const DWORD r = WaitForMultipleObjects(2, both, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0) __llvm_profile_reset_counters();
        else if (r == WAIT_OBJECT_0 + 1) { __llvm_profile_write_file(); SetEvent(done); }
        else return 2;
    }
}
const int started = (CloseHandle(CreateThread(nullptr, 0, pgo_thread, nullptr, 0, nullptr)), 0);
}  // namespace
