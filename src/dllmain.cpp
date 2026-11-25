// Entry point. DllMain only wires the winhttp forwarders and starts the mod's worker thread; all game work runs later
// on the game thread, inside the game mode's ReceiveTick (mod.cpp).
#include <windows.h>

bool HspInitProxy();
namespace hsp { DWORD WINAPI ModMain(void*); }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HspInitProxy();
        // Only in the game itself (another program that loads winhttp.dll from this folder gets the forwarders only).
        wchar_t exe[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH || !wcsstr(exe, L"HalfSwordUE5-Win64-Shipping.exe")) return TRUE;
        wchar_t name[64];
        wsprintfW(name, L"Local\\HalfSwordPerformanceMod-%lu", GetCurrentProcessId());
        HANDLE once = CreateMutexW(nullptr, FALSE, name);
        if (once && GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
        if (HANDLE t = CreateThread(nullptr, 0, hsp::ModMain, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
