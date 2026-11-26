#include "threads.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>

namespace hsp {

namespace {
using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

GetThreadDescriptionFn Describe() {
    static GetThreadDescriptionFn fn = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    return fn;
}

uint64_t Ticks(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

std::string Narrow(const wchar_t* w) {
    std::string s;
    for (; w && *w && s.size() < 48; ++w) s += *w < 128 ? char(*w) : '?';
    return s;
}

bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
}  // namespace

std::string ThreadTimes::Line(double frameMs) {
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    const uint64_t wall = Ticks(nowFt);
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return "";
    std::unordered_map<uint32_t, Thread> now;
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetThreadTimes(h, &c, &e, &k, &u)) {
            Thread& t = now[te.th32ThreadID];
            t.cpu = Ticks(k) + Ticks(u);
            if (auto it = last_.find(te.th32ThreadID); it != last_.end() && it->second.named) {
                t.name = it->second.name;
                t.named = true;
            } else if (GetThreadDescriptionFn fn = Describe()) {
                PWSTR w = nullptr;
                if (SUCCEEDED(fn(h, &w)) && w) { t.name = Narrow(w); LocalFree(w); }
                t.named = true;
            }
        }
        CloseHandle(h);
    }
    CloseHandle(snap);

    std::string out;
    if (lastWall_ && frameMs > 0 && wall > lastWall_) {
        double game = 0, render = 0, rhi = 0, other = 0, top = 0;
        std::string topName;
        for (const auto& [tid, t] : now) {
            auto it = last_.find(tid);
            // A thread started since the last line: all of its time is in this interval.
            const double ms = double(t.cpu - (it != last_.end() && it->second.cpu <= t.cpu ? it->second.cpu : 0)) / 1e4;
            if (tid == game_) game += ms;
            else if (Has(t.name, "RenderThread")) render += ms;
            else if (Has(t.name, "RHIThread")) rhi += ms;
            else {
                other += ms;
                if (ms > top) { top = ms; topName = t.name.empty() ? "thread " + std::to_string(tid) : t.name; }
            }
        }
        const double wallMs = double(wall - lastWall_) / 1e4;
        const double f = wallMs / frameMs;   // frames in the interval
        const double busiest = std::max(game, std::max(render, rhi)) / wallMs;
        char buf[256];
        std::snprintf(buf, sizeof buf, " | cpu ms a frame: game %.1f, render %.1f, rhi %.1f, other %.1f (most: %s %.1f); busiest %.0f%%%s",
                      game / f, render / f, rhi / f, other / f, topName.empty() ? "none" : topName.c_str(), top / f, busiest * 100,
                      busiest < 0.85 ? " (waiting on the GPU or a frame cap)" : "");
        out = buf;
    }
    last_.swap(now);
    lastWall_ = wall;
    return out;
}

}  // namespace hsp
