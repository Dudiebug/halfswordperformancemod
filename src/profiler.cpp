#include "profiler.h"
#include "log.h"
#include "paths.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

namespace hsp::prof {
using namespace ue;

namespace {
constexpr double kSeconds = 30;
constexpr double kTimelineUs = 20;
constexpr size_t kTimelineMaxEvents = 1000000;   // about 100 MB of JSON at most
constexpr uint32_t kFuncNative = 0x00000400;     // EFunctionFlags::FUNC_Native
constexpr size_t kStubSize = 32, kPageSize = 1 << 20;

using NativeFn = void (*)(UObject* context, void* stack, void* result);

// One function this mod has given a stub. Never freed: a thread that read Func just before a restore may still be
// inside the stub, and the stub reads this.
struct Hooked {
    std::atomic<NativeFn> orig{nullptr};
    UFunction* fn = nullptr;
    UClass* cls = nullptr;
    int32_t index = -1;
    uint8_t kind = kNative;
    uint8_t* stub = nullptr;
    bool stuck = false;   // another mod swapped Func over the stub: it stays (passing through) and is never hooked again
};

Profiler g_prof;   // game thread only
std::atomic<bool> g_on{false};
std::atomic<DWORD> g_thread{0};
std::deque<Hooked> g_hooks;
std::unordered_map<UFunction*, Hooked*> g_byFn;
std::vector<Hooked*> g_installed;
uint8_t* g_page = nullptr;
size_t g_pageUsed = kPageSize;
UObject* g_gameMode = nullptr;
double g_overheadNs = 0;
uint64_t g_tsc0 = 0;
int64_t g_qpc0 = 0;

int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double QpcMs(int64_t ticks) {
    static const double freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    return double(ticks) * 1000.0 / freq;
}
uint64_t Now() { return __rdtsc(); }
double TicksPerMs() {
    const double ms = QpcMs(Qpc() - g_qpc0);
    return ms > 0 ? double(Now() - g_tsc0) / ms : 1.0;
}

std::string NameOf(const UFunction* fn) {
    const UObject* o = reinterpret_cast<const UObject*>(fn);
    const UObject* owner = OuterOf(o);
    return (owner ? ObjectName(owner) + "." : std::string()) + ObjectName(o);
}

// Every stub jumps here with its Hooked in r9 (the fourth argument register, unused by a native thunk's three).
void Timed(UObject* context, void* stack, void* result, Hooked* h) {
    const NativeFn orig = h->orig.load(std::memory_order_relaxed);
    if (!g_on.load(std::memory_order_relaxed) || GetCurrentThreadId() != g_thread.load(std::memory_order_relaxed)) {
        orig(context, stack, result);
        return;
    }
    const bool pushed = g_prof.Begin(h, h->kind, Now(), [h] { return NameOf(h->fn); });
    orig(context, stack, result);
    if (pushed) g_prof.End(Now());
}

// mov r9, h; mov rax, Timed; jmp rax
uint8_t* NewStub(Hooked* h) {
    if (g_pageUsed + kStubSize > kPageSize) {
        g_page = static_cast<uint8_t*>(VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_page) return nullptr;
        g_pageUsed = 0;
    }
    uint8_t* p = g_page + g_pageUsed;
    g_pageUsed += kStubSize;
    void* target = reinterpret_cast<void*>(&Timed);
    p[0] = 0x49; p[1] = 0xB9; std::memcpy(p + 2, &h, 8);
    p[10] = 0x48; p[11] = 0xB8; std::memcpy(p + 12, &target, 8);
    p[20] = 0xFF; p[21] = 0xE0;
    std::memset(p + 22, 0xCC, kStubSize - 22);
    return p;   // Install flushes the instruction cache once, before any Func points here
}

// HSMMod's dwmapi.dll next to the game: its script hooks only take a function whose Func is the stock VM.
bool HsmModLoaded() {
    HMODULE m = GetModuleHandleW(L"dwmapi.dll");
    if (!m) return false;
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    const std::wstring local = ModFilePath(L"dwmapi.dll");
    return n && _wcsicmp(path, local.c_str()) == 0 && GetFileAttributesW(ModFilePath(L"HSMMod.log").c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Gives every loaded function a stub. Returns how many.
size_t Install(UFunction* skipFn, void* skipFunc) {
    UClass* kinds[] = {FindClass("Function"), FindClass("DelegateFunction"), FindClass("SparseDelegateFunction")};
    const int32_t count = ObjectCount();
    // First every stub is written and the instruction cache flushed once; only then does any Func point at one.
    std::vector<std::pair<Hooked*, void*>> todo;
    for (int32_t i = 0; i < count; ++i) {
        UObject* o = ObjectAt(i);
        if (!o) continue;
        UClass* c = ClassOf(o);
        if (!c || (c != kinds[0] && c != kinds[1] && c != kinds[2])) continue;
        UFunction* fn = reinterpret_cast<UFunction*>(o);
        void* cur = FunctionNative(fn);
        if (!cur || fn == skipFn || cur == skipFunc) continue;
        Hooked*& h = g_byFn[fn];
        if (!h) {
            h = &g_hooks.emplace_back();
            h->stub = NewStub(h);
        }
        if (!h->stub || h->stuck || cur == h->stub) continue;
        h->fn = fn;
        h->cls = c;
        h->index = i;
        h->kind = (FunctionFlags(fn) & kFuncNative) ? kNative : kBlueprint;
        h->orig.store(reinterpret_cast<NativeFn>(cur), std::memory_order_relaxed);
        todo.push_back({h, cur});
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    for (auto [h, cur] : todo) {
        if (FunctionNative(h->fn) != cur) continue;   // changed meanwhile (another thread): left alone
        SetFunctionNative(h->fn, h->stub);
        g_installed.push_back(h);
    }
    return g_installed.size();
}

void Restore() {
    size_t left = 0;
    for (Hooked* h : g_installed) {
        UObject* now = ObjectAt(h->index);
        if (now != reinterpret_cast<UObject*>(h->fn) || ClassOf(now) != h->cls) continue;   // freed (a level change): nothing to put back
        if (FunctionNative(h->fn) == h->stub) SetFunctionNative(h->fn, reinterpret_cast<void*>(h->orig.load()));
        else { h->stuck = true; ++left; }
    }
    g_installed.clear();
    if (left) HSP_LOG("profiler: %zu functions' Func changed during the capture (another mod, or a function freed and its slot reused); their stubs stay and pass calls through", left);
}

// One Begin/End pair as a stub pays it (thread check included), measured on a scratch profiler, ns.
double MeasureOverheadNs() {
    Profiler p;
    p.Start(Now(), {});
    static int key;
    constexpr int kIters = 20000;
    const DWORD self = GetCurrentThreadId();
    const int64_t t0 = Qpc();
    for (int i = 0; i < kIters; ++i)
        if (GetCurrentThreadId() == self && p.Begin(&key, kNative, Now(), [] { return std::string("x"); })) p.End(Now());
    return QpcMs(Qpc() - t0) * 1e6 / kIters;
}

std::string Stamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[32];
    std::snprintf(b, sizeof b, "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII file names only

std::string Start(UObject* gameMode, void* tickThunk) {
    if (HsmModLoaded()) return "profiler: HSMMod is installed; use its profiler (Ctrl+Shift+P) instead";
    g_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_gameMode = gameMode;
    g_overheadNs = MeasureOverheadNs();
    g_qpc0 = Qpc();
    g_tsc0 = Now();
    Sleep(1);   // the TSC rate is only known once the capture ends; the timeline threshold uses a 1 ms estimate of it
    const double tpm = TicksPerMs();
    UFunction* tick = FindFunctionNow(ClassOf(gameMode), "ReceiveTick");
    const int64_t i0 = Qpc();
    const size_t n = Install(tick, tickThunk);
    const double installMs = QpcMs(Qpc() - i0);
    g_qpc0 = Qpc();
    g_tsc0 = Now();
    Options o;
    o.traceMinTicks = uint64_t(kTimelineUs * tpm / 1000.0);
    o.traceMaxEvents = kTimelineMaxEvents;
    g_prof.Start(g_tsc0, o);
    g_on.store(true, std::memory_order_release);
    char b[200];
    std::snprintf(b, sizeof b, "profiler: recording for up to %.0f s (Ctrl+Shift+F10 stops); %zu functions timed (%.1f ms to set up), %.0f ns per call measured",
                  kSeconds, n, installMs, g_overheadNs);
    return b;
}

std::string Stop() {
    g_on.store(false, std::memory_order_release);
    const double tpm = TicksPerMs();
    g_prof.Stop(Now());
    Restore();
    const std::string base = "HSPerfProfile-" + Stamp();
    ReportInfo r;
    r.ticksPerMs = tpm;
    r.overheadNsPerCall = g_overheadNs;
    r.title = "Half Sword Performance Mod Blueprint profile " + base;
    if (!g_prof.trace().empty() || g_prof.frameMarks().size() > 1) {
        bool ok = false;
        if (FILE* f = _wfopen(ModFilePath(Wide(base + ".json").c_str()).c_str(), L"wb")) {
            ok = true;
            WriteTrace(g_prof, tpm, [&](std::string_view s) { ok = ok && std::fwrite(s.data(), 1, s.size(), f) == s.size(); });
            ok = std::fclose(f) == 0 && ok;
        }
        if (ok) r.traceFile = base + ".json";
        else HSP_LOG("profiler: could not write %s.json", base.c_str());
    }
    const std::string report = Report(g_prof, r);
    bool saved = false;
    if (FILE* f = _wfopen(ModFilePath(Wide(base + ".txt").c_str()).c_str(), L"wb")) {
        saved = std::fwrite(report.data(), 1, report.size(), f) == report.size();
        saved = std::fclose(f) == 0 && saved;
    }
    const std::vector<uint64_t> ft = g_prof.FrameTicks();
    double avg = 0;
    for (uint64_t t : ft) avg += double(t);
    if (!ft.empty()) avg = avg / double(ft.size()) / tpm;
    char b[300];
    std::snprintf(b, sizeof b, "profiler: %s %s.txt (%.1f s, %zu frames, avg %.1f ms); %llu calls, %zu functions, %zu timeline events (%llu not kept)",
                  saved ? "saved" : "could not write", base.c_str(), double(g_prof.durationTicks()) / tpm / 1000.0, ft.size(), avg,
                  (unsigned long long)g_prof.calls(), g_prof.stats().size(), g_prof.trace().size(), (unsigned long long)g_prof.dropped());
    return b;
}
}  // namespace

bool Recording() { return g_on.load(std::memory_order_relaxed); }

std::string Toggle(UObject* gameMode, void* tickThunk) { return Recording() ? Stop() : Start(gameMode, tickThunk); }

std::string Frame(UObject* gameMode, double frameGapMs) {
    if (!Recording()) return {};
    // A level change (new game mode, or a load-screen pause): the old level's functions are being freed.
    if (gameMode != g_gameMode || frameGapMs > 1000) return Stop() + " (level change)";
    g_prof.FrameMark(Now());
    return QpcMs(Qpc() - g_qpc0) >= kSeconds * 1000.0 ? Stop() : std::string();
}

bool BeginMod(const char* name) {
    if (GetCurrentThreadId() != g_thread.load(std::memory_order_relaxed)) return false;
    return g_prof.Begin(name, kMod, Now(), [name] { return std::string("HSPerf ") + name; });
}

void EndMod() { g_prof.End(Now()); }

}  // namespace hsp::prof
