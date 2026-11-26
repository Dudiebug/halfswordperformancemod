// Cheaper blood bookkeeping: two constants in Willie_BP's event graph, changed in its loaded bytecode (and put back
// when the setting is turned off). Blood and dents are painted, saved and loaded exactly as before.
//
// 1. Snapshot copy. Every fighter's tick copies its whole saved vertex paint map (Current Vertex Paints, tens of
//    thousands of colours per fighter) into its Character Passport, together with health, bleeding and the rest. A
//    DoOnce gates it and a Delay(0) chain opens the gate again a frame later, so the copy ran about 30 times a second
//    per fighter. The second Delay of that chain waits 0.25 s instead: the passport is never more than a quarter of a
//    second old. The vertex colour readback itself (Paint All Meshes Save, every 0 to 10 s and on death and equipment
//    changes) is untouched.
// 2. Footstep floor lookup. Each footstep asks the paint plugin for the floor vertex closest to the foot (for bloody
//    feet, blood splashes and mud sounds), with the plugin's "accept a vertex within range" early-out off, so it
//    searches every vertex of the floor mesh. The early-out is turned on: the first vertex within the game's own 5 cm
//    is taken.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>
#include <vector>

namespace hsp {

struct BloodCounts { bool snapshot = false, footsteps = false; };

class BloodFixes {
public:
    explicit BloodFixes(const core::Settings& s) : s_(s) {}   // s is read on every pass: the menu changes it live
    void Pass();                  // every 250 ms: puts the patches in or takes them out, as the setting says
    void SetActive(bool on);      // false: the game's own bytecode again (the A/B key)
    BloodCounts Counts() const { return counts_; }

private:
    struct Patch { int32_t at; std::vector<uint8_t> was, now; };
    bool Prepare();
    void Apply();
    void Restore();
    bool Intact() const;          // every patch is still in the loaded bytecode

    const core::Settings& s_;
    bool active_ = true;
    bool prepared_ = false, scanned_ = false;
    ue::UClass* willie_ = nullptr;
    int32_t willieIndex_ = -1;
    ue::UFunction* graph_ = nullptr;   // ExecuteUbergraph_Willie_BP
    int32_t graphIndex_ = -1;
    std::vector<Patch> patches_;       // applied
    BloodCounts counts_;
};

}  // namespace hsp
