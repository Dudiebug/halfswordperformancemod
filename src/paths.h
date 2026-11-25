#pragma once
#include <windows.h>
#include <string>

namespace hsp {
// <game exe dir>\name
inline std::wstring ModFilePath(const wchar_t* name) {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    const size_t slash = dir.find_last_of(L"\\/");
    dir.resize(slash == std::wstring::npos ? 0 : slash + 1);
    return dir + name;
}
}  // namespace hsp
