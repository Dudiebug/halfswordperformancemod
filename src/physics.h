// Physics work that gives the same result for less, run with the armour pass (every 250 ms).
//
// Island groups: Chaos solves each island (bodies joined by contacts or constraints) on its own, so how many workers
// share them changes nothing in the answer. The game ships the engine minimum of 50 bodies / 50 constraints per worker;
// a fighter is about 25 bodies, so a fight of 8 rarely gets more than one or two workers. The two console variables
// (ECVF_Default, not cheats) are set lower while this is on and back to 50 when it is off.
//
// Threading model: nothing to change. The engine makes the scene solver TaskGraph whenever the app is multithreaded
// (the ini's DefaultThreadingModel=SingleThread is not read for it); the log says which one this run got.
//
// Damage marker spheres and armour pieces: left alone. The stuck-weapon box (Constraint_Weapon_Stuck_BP) collects the
// fighter's components it overlaps, by their bone and HP tags, for dismemberment, so their overlap events are read.
// Their collision, object type, overlap events and simulation are logged once for the first fighter.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>

namespace hsp {

struct PhysicsCounts { int islandMin = 0; };

class PhysicsFixes {
public:
    explicit PhysicsFixes(const core::Settings& s) : s_(s) {}   // s is read on every pass: the menu changes it live
    // console: runs a console command (world context = the game mode).
    using Console = void (*)(ue::UObject* worldContext, const char* command);
    void Pass(ue::UObject* gameMode, Console console);
    void SetActive(bool on) { active_ = on; }   // false: everything put back on the next pass (the A/B key)
    PhysicsCounts Counts() const { return counts_; }

private:
    bool Prepare();
    void Islands(ue::UObject* gameMode, Console console);
    void LogFighter(ue::UObject* fighter, ue::UObject* body);

    const core::Settings& s_;
    bool active_ = true;
    bool prepared_ = false, unusable_ = false, threadingLogged_ = false;
    ue::UClass* willie_ = nullptr;
    int32_t willieIndex_ = -1;
    ue::UObject* statics_ = nullptr;
    ue::PreparedCall allActors_;
    int islandsSet_ = 0;   // the value this mod last set (0 = never touched)
    bool logged_ = false;
    PhysicsCounts counts_;
};

}  // namespace hsp
