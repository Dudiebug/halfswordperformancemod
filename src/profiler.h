// Blueprint profiler, ported from HSMMod's (same report and timeline format).
//
// Ctrl + Shift + the overlay key (F10) records up to 30 s; the same key stops early. Stopping writes HSPerfProfile-<date>-
// <time>.txt (functions by self and inclusive time, the frame times) and a .json timeline (Chrome trace format:
// ui.perfetto.dev, chrome://tracing, speedscope) next to the game exe.
//
// HSMMod instruments ProcessEvent and the script VM with inline hooks; this mod patches no code. A capture instead
// swaps UFunction::Func on every loaded function for a small stub that times the call and chains to the original, and
// puts every Func back when it stops. The engine calls Func when it enters a Blueprint from native code (ticks,
// delegates, animation graph inputs, Delay resumes) and when a Blueprint calls a native function, so both are timed.
// A Blueprint calling another Blueprint function does not go through Func: that time is inside the caller's.
//
// With HSMMod installed the capture refuses (its VM hooks look for a stock Func; use its Ctrl+Shift+P instead).
// Functions loaded after a capture starts are not timed; a level change ends the capture.
#pragma once
#include "core/prof.h"
#include "ue/engine.h"
#include <string>

namespace hsp::prof {

// Game thread. Start or stop a capture; returns a line for the log.
std::string Toggle(ue::UObject* gameMode, void* tickThunk);
// Every game mode tick: the frame mark, and the end of a capture at its time limit or on a level change. Returns a line
// for the log when it stopped one (empty otherwise).
std::string Frame(ue::UObject* gameMode, double frameGapMs);
bool Recording();

bool BeginMod(const char* name);
void EndMod();
// One of this mod's own passes on the profiler's stack while a capture runs.
struct Scope {
    bool on;
    explicit Scope(const char* name) : on(Recording() && BeginMod(name)) {}
    ~Scope() { if (on) EndMod(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace hsp::prof
