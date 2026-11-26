// Startup and the game-thread tick.
//
// No code in the game is patched. The mod swaps one pointer: UFunction::Func of BP_HalfSwordGameMode_C's ReceiveTick,
// which ProcessEvent calls through Invoke every frame. The swapped-in thunk calls the original (UObject::ProcessInternal,
// or whatever another mod put there first) and then runs this mod's work. HSMMod, HS Enhancer and UE4SS detour
// ProcessEvent and the script VM with inline hooks; a pointer swap stays out of their way in any load order.
#include "armor.h"
#include "config.h"
#include "corpses.h"
#include "core/perf_core.h"
#include "log.h"
#include "menu.h"
#include "profiler.h"
#include "threads.h"
#include "weapons.h"
#include "ue/engine.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
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
std::unique_ptr<WeaponFixes> g_weapons;
std::unique_ptr<CorpseFixes> g_corpses;
ThreadTimes g_threads;
// The original Func is ProcessInternal running the game mode's ReceiveTick body, which is empty in this build
// (it only stores DeltaSeconds): with skip_empty_gamemode_tick it is not run. Never set when another mod's Func is there.
std::atomic<bool> g_skipOrig{false};
std::atomic<bool> g_origIsVm{false};   // the Func found on ReceiveTick was the script VM (so its body may be skipped)
Menu g_menu;

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
    double avgMs = 0;   // frame time, smoothed (the menu's stats line)
};
TickState g_tick;

void TickThunk(UObject* context, void* stack, void* result);

std::string ToggleProfile(UObject* gameMode) {
    const std::string line = prof::Toggle(gameMode, reinterpret_cast<void*>(&TickThunk));
    HSP_LOG("%s", line.c_str());
    return line;
}

void ToggleFixes() {
    g_armor->SetActive(!g_armor->Active());
    g_weapons->SetActive(g_armor->Active());
    g_corpses->SetActive(g_armor->Active());
    HSP_LOG("key: armour, weapon and corpse fixes %s", g_armor->Active() ? "ON" : "OFF (as the game ships)");
    g_tick.frames.Reset();   // the next perf line covers one setting only
    g_tick.lastLog = g_tick.lastFrame;
}

void ToggleOverlay(UObject* gameMode) {
    g_tick.overlay = !g_tick.overlay;
    Console(gameMode, "stat fps");
    Console(gameMode, "stat unit");
}

void Keys(UObject* gameMode) {
    if (!g_settings.overlayKey) return;
    const bool down = GameHasFocus() && (GetAsyncKeyState(g_settings.overlayKey) & 0x8000);
    if (down && !g_tick.keyDown) {
        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) ToggleProfile(gameMode);
        else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) ToggleFixes();
        else ToggleOverlay(gameMode);
    }
    g_tick.keyDown = down;
}

void MenuTick(UObject* gameMode) {
    MenuInfo info;
    if (g_menu.IsOpen()) {
        info.overlay = g_tick.overlay;
        info.fixesActive = g_armor->Active();
        info.recording = prof::Recording();
        const ArmorCounts c = g_armor->Counts();
        const WeaponCounts wc = g_weapons->Counts();
        const CorpseCounts cc = g_corpses->Counts();
        char line[320];
        std::snprintf(line, sizeof line,
                      "%.1f ms a frame (%.0f fps)   fighters %d (%d dead, %d asleep)   armour pieces %d (%d follow the body)   weapons %d (%d asleep)",
                      g_tick.avgMs, g_tick.avgMs > 0 ? 1000.0 / g_tick.avgMs : 0.0, c.fighters, cc.dead, cc.tickOff, c.pieces, c.leader,
                      wc.weapons, wc.tickOff);
        info.stats = line;
    }
    switch (g_menu.Tick(gameMode, g_settings, info)) {
    case MenuAction::SettingsChanged:
        g_skipOrig.store(g_origIsVm.load() && g_settings.skipEmptyGameModeTick, std::memory_order_relaxed);
        break;
    case MenuAction::ToggleOverlay: ToggleOverlay(gameMode); break;
    case MenuAction::ToggleFixes:
        ToggleFixes();
        g_menu.Status(g_armor->Active() ? "Fixes on." : "Fixes off: the game runs as it ships.");
        break;
    case MenuAction::ToggleProfile: g_menu.Status(ToggleProfile(gameMode)); break;
    case MenuAction::None: break;
    }
}

void OnGameTick(UObject* gameMode) {
    const double now = NowMs();
    if (g_tick.lastFrame == 0) g_threads.SetGameThread(GetCurrentThreadId());
    const double gap = g_tick.lastFrame > 0 ? now - g_tick.lastFrame : 0;
    if (g_tick.lastFrame > 0) g_tick.frames.Add(gap);
    if (gap > 0 && gap < 1000) g_tick.avgMs = g_tick.avgMs > 0 ? g_tick.avgMs * 0.95 + gap * 0.05 : gap;
    g_tick.lastFrame = now;
    if (const std::string msg = prof::Frame(gameMode, gap); !msg.empty()) HSP_LOG("%s", msg.c_str());
    Keys(gameMode);
    MenuTick(gameMode);
    {
        prof::Scope s("weapon and corpse wake check");
        g_weapons->Frame();
        g_corpses->Frame();
    }
    if (now - g_tick.lastPass >= kPassMs) {
        g_tick.lastPass = now;
        prof::Scope s("armour and weapon pass");
        g_armor->Pass(gameMode);
        g_weapons->Pass(gameMode);
        g_corpses->Pass(gameMode);
    }
    if (g_settings.logSeconds > 0 && now - g_tick.lastLog >= g_settings.logSeconds * 1000.0) {
        if (g_tick.frames.Count() > 0) {
            const ArmorCounts c = g_armor->Counts();
            const WeaponCounts wc = g_weapons->Counts();
            const CorpseCounts cc = g_corpses->Counts();
            const std::string cpu = g_threads.Line(g_tick.frames.Average());
            HSP_LOG("perf: frames %zu avg %.1f ms p99 %.1f max %.1f | fighters %d (dead %d, tick off %d; bounds %d, head/skeleton follow %d), "
                    "armour pieces %d (leader pose %d, shadow off %d, ray tracing off %d), weapons %d (tick off %d), fixes %s%s",
                    g_tick.frames.Count(), g_tick.frames.Average(), g_tick.frames.P99(), g_tick.frames.Max(), c.fighters, cc.dead, cc.tickOff,
                    c.bodyBounds, c.headLeader, c.pieces, c.leader, c.shadowOff, c.rtOff, wc.weapons, wc.tickOff,
                    g_armor->Active() ? "on" : "OFF", cpu.c_str());
        }
        g_tick.frames.Reset();
        g_tick.lastLog = now;
    }
}

// Func of the game mode's ReceiveTick. Signature of every native thunk: (Context, FFrame& Stack, RESULT_DECL).
void TickThunk(UObject* context, void* stack, void* result) {
    if (!g_skipOrig.load(std::memory_order_relaxed))
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
    g_origIsVm.store(vm, std::memory_order_relaxed);
    g_skipOrig.store(vm && g_settings.skipEmptyGameModeTick, std::memory_order_relaxed);
    g_orig.store(reinterpret_cast<NativeFn>(cur), std::memory_order_release);
    SetFunctionNative(fn, reinterpret_cast<void*>(&TickThunk));
    HSP_LOG("tick: on %s.ReceiveTick (Func was %s)%s", kGameModeClass, vm ? "the script VM" : "another mod's",
            g_skipOrig.load() ? "; its empty Blueprint body is skipped" : "");
    // If this is set, ProcessEvent may call the ubergraph directly and the swap above never runs: no perf lines follow.
    UFunction* graph = EventGraphFunction(fn);
    HSP_LOG("tick: ReceiveTick EventGraphFunction %s (offset %d)", graph ? ObjectName(reinterpret_cast<UObject*>(graph)).c_str() : "none",
            EventGraphCallOffset(fn));
}
}  // namespace

DWORD WINAPI ModMain(void*) {
    LogInit();
    HSP_LOG("Half Sword Performance Mod %s", HSP_VERSION);
    g_settings = LoadSettings();
    if (!g_settings.enabled) { HSP_LOG("disabled in HSPerf.ini"); return 0; }
    if (!Init(g_settings.forceUnknownBuild)) { HSP_LOG("engine: not a supported game build; the mod stays off"); return 0; }
    // Wait for the function, not just the class: a native class exists a moment before its functions are linked to it
    // (seen under Proton, where the mod's thread gets here sooner).
    for (int i = 0;; ++i) {
        UObject* math = FindObject("KismetMathLibrary", "Class");
        if (math && FindFunction(reinterpret_cast<UClass*>(math), "Add_IntInt")) {
            if (i) HSP_LOG("engine: Kismet functions ready after %.1f s", i * 0.5);
            break;
        }
        if (i > 600) { HSP_LOG("engine: Kismet functions never loaded (class %s); the mod stays off", math ? "found" : "missing"); return 0; }
        Sleep(500);
    }
    if (!SelfTest()) { HSP_LOG("engine: self test failed; the mod stays off"); return 0; }
    g_armor = std::make_unique<ArmorFixes>(g_settings);
    g_weapons = std::make_unique<WeaponFixes>(g_settings);
    g_corpses = std::make_unique<CorpseFixes>(g_settings);
    for (;;) {
        if (GetTickCount64() - g_lastTickMs.load(std::memory_order_relaxed) > 2000) TryInstall();
        Sleep(1000);
    }
}

}  // namespace hsp
