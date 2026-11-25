// Loads build/winhttp.dll by path and calls two forwarded exports (Windows only). Built and run by tests/run_tests.sh on Windows
#include <windows.h>
#include <cstdio>
int main() {
    HMODULE m = LoadLibraryW(L"build/winhttp.dll");
    if (!m) { std::printf("FAIL load %lu\n", GetLastError()); return 1; }
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(m, path, MAX_PATH);
    if (!wcsstr(path, L"build")) { std::printf("FAIL loaded the system DLL instead: %ls\n", path); return 1; }
    auto check = reinterpret_cast<BOOL(WINAPI*)()>(GetProcAddress(m, "WinHttpCheckPlatform"));
    auto open = reinterpret_cast<void*(WINAPI*)(const wchar_t*, DWORD, const wchar_t*, const wchar_t*, DWORD)>(GetProcAddress(m, "WinHttpOpen"));
    auto close = reinterpret_cast<BOOL(WINAPI*)(void*)>(GetProcAddress(m, "WinHttpCloseHandle"));
    if (!check || !open || !close) { std::printf("FAIL exports\n"); return 1; }
    void* h = open(L"smoke", 0, nullptr, nullptr, 0);
    const bool ok = check() && h && close(h);
    std::printf(ok ? "proxy ok\n" : "FAIL calls\n");
    return ok ? 0 : 1;
}
