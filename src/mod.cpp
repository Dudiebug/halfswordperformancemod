// Startup and the game-thread tick.
//
// No code in the game is patched. The mod swaps one pointer: UFunction::Func of BP_HalfSwordGameMode_C's ReceiveTick,
// which ProcessEvent calls through Invoke every frame. The swapped-in thunk calls the original (UObject::ProcessInternal,
// or whatever another mod put there first) and then runs this mod's work. HSMMod, HS Enhancer and UE4SS detour
// ProcessEvent and the script VM with inline hooks; a pointer swap stays out of their way in any load order.
#include "armor.h"
#include "config.h"
#include "core/perf_core.h"
#include "log.h"
#include "ue/engine.h"
#include <windows.h>
#include <atomic>
#include <memory>

namespace hsp {
using namespace ue;

namespace {
constexpr const char* kGameModeClass = "BP_HalfSwordGameMode_C";
constexpr double kPassMs = 250;   // armour pass interval

using NativeFn = void (*)(UObject* context, void* stack, void* result);
std::atomic<NativeFn> g_orig{nullptr};
std::atomic<uint64_t> g_lastTickMs{0};
core::Settings g_settings;
std::unique_ptr<ArmorFixes> g_armor;

double NowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1000.0 / double(freq.QuadPart);
}

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void Console(UObject* worldContext, const char* command) {
    static UObject* ksl = nullptr;
    if (!ksl) if (UObject* c = FindObject("KismetSystemLibrary", "Class")) ksl = DefaultObject(reinterpret_cast<UClass*>(c));
    if (!ksl) return;
    FStringArgHolder cmd(command);
    Call(ksl, "ExecuteConsoleCommand").set<UObject*>("WorldContextObject", worldContext).set("Command", cmd.arg)
        .set<UObject*>("SpecificPlayer", nullptr).invoke();
}

struct TickState {
    double lastFrame = 0, lastPass = 0, lastLog = 0;
    core::FrameStats frames;
    bool keyDown = false, overlay = false;
};
TickState g_tick;

void Keys(UObject* gameMode) {
    if (!g_settings.overlayKey) return;
    const bool down = GameHasFocus() && (GetAsyncKeyState(g_settings.overlayKey) & 0x8000);
    if (down && !g_tick.keyDown) {
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            g_armor->SetActive(!g_armor->Active());
            HSP_LOG("key: armour fixes %s", g_armor->Active() ? "ON" : "OFF (as the game ships)");
            g_tick.frames.Reset();   // the next perf line covers one setting only
            g_tick.lastLog = g_tick.lastFrame;
        } else {
            g_tick.overlay = !g_tick.overlay;
            Console(gameMode, "stat fps");
            Console(gameMode, "stat unit");
        }
    }
    g_tick.keyDown = down;
}

void OnGameTick(UObject* gameMode) {
    const double now = NowMs();
    if (g_tick.lastFrame > 0) g_tick.frames.Add(now - g_tick.lastFrame);
    g_tick.lastFrame = now;
    Keys(gameMode);
    if (now - g_tick.lastPass >= kPassMs) {
        g_tick.lastPass = now;
        g_armor->Pass(gameMode);
    }
    if (g_settings.logSeconds > 0 && now - g_tick.lastLog >= g_settings.logSeconds * 1000.0) {
        if (g_tick.frames.Count() > 0) {
            const ArmorCounts c = g_armor->Counts();
            HSP_LOG("perf: frames %zu avg %.1f ms p99 %.1f max %.1f | fighters %d, armour pieces %d (leader pose %d, shadow off %d), fixes %s",
                    g_tick.frames.Count(), g_tick.frames.Average(), g_tick.frames.P99(), g_tick.frames.Max(), c.fighters, c.pieces,
                    c.leader, c.shadowOff, g_armor->Active() ? "on" : "OFF");
        }
        g_tick.frames.Reset();
        g_tick.lastLog = now;
    }
}

// Func of the game mode's ReceiveTick. Signature of every native thunk: (Context, FFrame& Stack, RESULT_DECL).
void TickThunk(UObject* context, void* stack, void* result) {
    if (NativeFn orig = g_orig.load(std::memory_order_acquire)) orig(context, stack, result);
    g_lastTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    OnGameTick(context);
}

// Worker thread, only while no tick has arrived for 2 s (startup, a level change that loaded the class again).
void TryInstall() {
    UClass* cls = FindClass(kGameModeClass);
    if (!cls) return;
    UFunction* fn = FindFunctionNow(cls, "ReceiveTick");
    // The class's own override, not AActor::ReceiveTick (shared by every actor).
    if (!fn || OuterOf(reinterpret_cast<UObject*>(fn)) != reinterpret_cast<UObject*>(cls)) return;
    void* cur = FunctionNative(fn);
    if (cur == reinterpret_cast<void*>(&TickThunk) || !cur) return;
    NativeFn prev = g_orig.load();
    if (prev && reinterpret_cast<void*>(prev) != cur)
        HSP_LOG("tick: ReceiveTick's Func changed (%p, was %p): another mod swapped it; chaining to the new one", cur, reinterpret_cast<void*>(prev));
    const bool vm = IsScriptFunction(fn);
    g_orig.store(reinterpret_cast<NativeFn>(cur), std::memory_order_release);
    SetFunctionNative(fn, reinterpret_cast<void*>(&TickThunk));
    HSP_LOG("tick: on %s.ReceiveTick (Func was %s)", kGameModeClass, vm ? "the script VM" : "another mod's");
}
}  // namespace

DWORD WINAPI ModMain(void*) {
    LogInit();
    HSP_LOG("Half Sword Performance Mod %s", HSP_VERSION);
    g_settings = LoadSettings();
    if (!g_settings.enabled) { HSP_LOG("disabled in HSPerf.ini"); return 0; }
    if (!Init(g_settings.forceUnknownBuild)) { HSP_LOG("engine: not a supported game build; the mod stays off"); return 0; }
    for (int i = 0; !FindObject("KismetMathLibrary", "Class"); ++i) {
        if (i > 600) { HSP_LOG("engine: Kismet classes never loaded; the mod stays off"); return 0; }
        Sleep(500);
    }
    if (!SelfTest()) { HSP_LOG("engine: self test failed; the mod stays off"); return 0; }
    g_armor = std::make_unique<ArmorFixes>(g_settings);
    for (;;) {
        if (GetTickCount64() - g_lastTickMs.load(std::memory_order_relaxed) > 2000) TryInstall();
        Sleep(1000);
    }
}

}  // namespace hsp
