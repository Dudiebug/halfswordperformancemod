// Idle weapons stop ticking.
//
// ModularWeaponBP_C::ReceiveTick runs every frame on every weapon in the level, held or lying on the floor. It drives
// the swing and slide sounds from the weapon's velocity, decays Just Parried Rate, and (through two Delays it issues
// again every frame) re-enables collision with the weapon's last owner once it is 300 units away and credits weapon
// skill. On a weapon that is asleep on the floor with none of that pending, the tick computes nothing that changes.
// This turns the actor tick off on exactly those weapons and back on, before the next frame's tick, as soon as one
// wakes up or is picked up. Weapons whose class has its own ReceiveTick (candlesticks, the light crossbow) are left
// alone.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>
#include <vector>

namespace hsp {

struct WeaponCounts { int weapons = 0, tickOff = 0; };

class WeaponFixes {
public:
    explicit WeaponFixes(const core::Settings& s) : s_(s) {}   // s is read on every pass: the menu changes it live
    void Pass(ue::UObject* gameMode);   // every 250 ms: finds idle weapons and turns their tick off
    void Frame();                       // every frame: turns the tick back on for any of them that woke up
    void SetActive(bool on);            // false: every weapon this mod stopped ticks again (the A/B key)
    WeaponCounts Counts() const { return counts_; }

private:
    struct Off { ue::UObject* obj; int32_t index; ue::UClass* cls; };
    bool Prepare();
    bool Idle(ue::UObject* w);
    void Wake(ue::UObject* w);
    void WakeAll();
    ue::UObject* Resolve(const Off& o) const;   // nullptr when that weapon is gone (read through the object array only)

    const core::Settings& s_;
    bool active_ = true;
    bool prepared_ = false, unusable_ = false;
    ue::UClass* weapon_ = nullptr;
    int32_t weaponIndex_ = -1;            // its object array slot: a level change that unloads the class prepares again
    ue::UFunction* baseTick_ = nullptr;   // ModularWeaponBP_C's own ReceiveTick
    ue::UObject* statics_ = nullptr;
    ue::PreparedCall allActors_;
    std::vector<Off> off_;                // weapons whose tick this mod turned off
    WeaponCounts counts_;
};

}  // namespace hsp
