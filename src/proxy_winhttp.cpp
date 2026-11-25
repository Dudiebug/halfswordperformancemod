// winhttp.dll proxy: every export is a one-instruction jump to System32's winhttp.dll, so the game and every other
// module behave exactly as without this DLL. The export table comes from tools/gen_winhttp_proxy.py.
#include <windows.h>

#define HSP_FORWARD(i) __asm__(".text\n.globl HspFwd" #i "\nHspFwd" #i ":\n\tjmp *g_hspForward+" #i "*8(%rip)\n");
#include "proxy_winhttp_table.inc"

namespace {
constexpr const char* kNames[] = { HSP_PROXY_NAMES };
static_assert(sizeof(kNames) / sizeof(kNames[0]) == HSP_PROXY_COUNT, "name and stub count differ");

// An export the real DLL lacks (Wine has fewer): 0 is FALSE / a null HINTERNET, which every caller reads as failure.
extern "C" UINT_PTR WINAPI HspMissing() { SetLastError(ERROR_PROC_NOT_FOUND); return 0; }
}  // namespace

extern "C" void* g_hspForward[HSP_PROXY_COUNT];
void* g_hspForward[HSP_PROXY_COUNT];

HSP_PROXY_STUBS

bool HspInitProxy() {
    for (void*& p : g_hspForward) p = reinterpret_cast<void*>(&HspMissing);
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 16 >= MAX_PATH) return false;
    lstrcatW(path, L"\\winhttp.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) return false;
    for (size_t i = 0; i < HSP_PROXY_COUNT; ++i)
        if (FARPROC p = GetProcAddress(real, kNames[i])) g_hspForward[i] = reinterpret_cast<void*>(p);
    return true;
}
