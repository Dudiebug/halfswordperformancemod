// Physics work that gives the same result for less, run with the armour pass (every 250 ms).
//
// Island groups: Chaos solves each island (bodies joined by contacts or constraints) on its own, so how many workers
// share them changes nothing in the answer. The game ships the engine minimum of 50 bodies / 50 constraints per worker;
// a fighter is about 25 bodies, so a fight of 8 rarely gets more than one or two workers. The two console variables
// (ECVF_Default, not cheats) are set lower while this is on and back to 50 when it is off.
//
// Damage marker spheres: each fighter has about 230 "DM Sphere" components on its bones. The game reads them only by
// distance to their locations (Willie_BP Get_Damage, Paint_Splash) and keeps their HP in tags. Nothing binds an overlap
// event on them; the overlap events that exist (arena trigger volumes, the barricade door) want the fighter actor,
// which the body mesh still gives them. So their overlap events are turned off (collision itself is kept, so every
// trace and overlap query that could find them still does) while the body mesh has collision and overlap events of
// its own; back on as soon as it does not, or when this is off.
//
// Armour pieces: their physics bodies are logged once (collision, object type, overlap events, simulation) to see
// whether they cost anything; nothing is changed on them.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hsp {

struct PhysicsCounts { int islandMin = 0, markers = 0, markersOff = 0; };

class PhysicsFixes {
public:
    explicit PhysicsFixes(const core::Settings& s) : s_(s) {}   // s is read on every pass: the menu changes it live
    // console: runs a console command (world context = the game mode).
    using Console = void (*)(ue::UObject* worldContext, const char* command);
    void Pass(ue::UObject* gameMode, Console console);
    void SetActive(bool on) { active_ = on; }   // false: everything put back on the next pass (the A/B key)
    PhysicsCounts Counts() const { return counts_; }

private:
    struct Marker { int32_t index; ue::UObject* fighter; bool ours; };   // ours = false: the game had it off already
    bool Prepare();
    void Islands(ue::UObject* gameMode, Console console);
    void Markers(const std::vector<ue::UObject*>& fighters);
    void LogFighter(ue::UObject* fighter, ue::UObject* body);
    bool SetOverlaps(ue::UObject* c, bool on);

    const core::Settings& s_;
    bool active_ = true;
    bool prepared_ = false, unusable_ = false;
    ue::UClass* willie_ = nullptr;
    int32_t willieIndex_ = -1;
    ue::UObject* statics_ = nullptr;
    ue::PreparedCall allActors_;
    int islandsSet_ = 0;   // the value this mod last set (0 = never touched)
    std::unordered_map<ue::UObject*, Marker> off_;   // spheres looked at (overlap events off now)
    bool logged_ = false;
    PhysicsCounts counts_;
};

}  // namespace hsp
