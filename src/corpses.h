// Dead fighters stop ticking.
//
// Willie_BP_C::ReceiveTick costs about 1 ms a frame per fighter and keeps running after Death() (which sets DED but
// never turns the tick off): physics handle targets, ~23 physical animation settings, the movement mode, health,
// temperature, bleeding and the gurgle volume are all set again every frame. On a corpse whose ragdoll has gone to
// sleep and whose values have stopped changing, all of that writes what is already there. This turns the actor tick
// off on exactly those fighters and back on, before the next frame's tick, as soon as the body wakes (a hit, a
// kick, a weapon landing on it) or DED is cleared. A fighter class with a ReceiveTick of its own is left alone.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hsp {

struct CorpseCounts { int dead = 0, tickOff = 0; };

class CorpseFixes {
public:
    explicit CorpseFixes(const core::Settings& s) : s_(s) {}   // s is read on every pass: the menu changes it live
    void Pass(ue::UObject* gameMode);   // every 250 ms: finds still corpses and turns their tick off
    void Frame();                       // every frame: turns the tick back on for any of them that woke up
    void SetActive(bool on);            // false: every corpse this mod stopped ticks again (the A/B key)
    CorpseCounts Counts() const { return counts_; }

private:
    struct Off { ue::UObject* obj; int32_t index; ue::UClass* cls; };
    // The values the tick still advances on a corpse. Equal on kStillPasses passes in a row = it has settled.
    struct Snapshot { double v[6] = {}; int still = 0; };   // one per kWatched
    bool Prepare();
    bool Settled(ue::UObject* f, Snapshot& snap);
    bool Awake(ue::UObject* f);
    void Wake(ue::UObject* f);
    void WakeAll();
    ue::UObject* Resolve(const Off& o) const;

    const core::Settings& s_;
    bool active_ = true;
    bool prepared_ = false, unusable_ = false;
    ue::UClass* willie_ = nullptr;
    int32_t willieIndex_ = -1;
    ue::UFunction* baseTick_ = nullptr;   // Willie_BP_C's own ReceiveTick
    ue::UObject* statics_ = nullptr;
    ue::PreparedCall allActors_;
    std::vector<Off> off_;
    std::unordered_map<ue::UObject*, Snapshot> watch_;   // dead fighters not asleep yet, rebuilt every pass
    CorpseCounts counts_;
};

}  // namespace hsp
