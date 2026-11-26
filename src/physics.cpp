#include "physics.h"
#include "log.h"
#include <cstdio>
#include <string>

namespace hsp {
using namespace ue;

namespace {
constexpr int kGameIslandMin = 50;   // the exe's default for both MinBodiesPerWorker and MinConstraintsPerWorker
struct TArrayRaw { UObject** data; int32_t num, max; };
static_assert(sizeof(TArrayRaw) == 16, "TArray header");

bool Alive(UObject* o) { return o && !IsGarbage(o); }

UObject* Obj(UObject* o, const char* prop) {
    UObject* const* f = Field<UObject*>(o, prop);
    return f ? *f : nullptr;
}

bool CallBool(UObject* o, const char* fn) {
    Call c(o, fn);
    return c.valid() && c.invoke() && c.get<bool>("ReturnValue");
}

int CallByte(UObject* o, const char* fn) {
    Call c(o, fn);
    return c.valid() && c.invoke() ? int(c.get<uint8_t>("ReturnValue")) : -1;
}

bool Simulating(UObject* o) {
    Call c(o, "IsSimulatingPhysics");
    return c.valid() && c.setName("BoneName", FName{}).invoke() && c.get<bool>("ReturnValue");
}
}  // namespace

bool PhysicsFixes::Prepare() {
    if (prepared_ && willie_ && ObjectAt(willieIndex_) != reinterpret_cast<UObject*>(willie_)) {
        prepared_ = unusable_ = false;   // a new level's class: the old spheres are gone with the old fighters
        off_.clear();
    }
    if (prepared_) return !unusable_;
    willie_ = FindClass("Willie_BP_C");
    UObject* gs = FindObject("GameplayStatics", "Class");
    if (!willie_ || !gs) return false;
    prepared_ = true;
    willieIndex_ = ObjectIndex(reinterpret_cast<UObject*>(willie_));
    statics_ = DefaultObject(reinterpret_cast<UClass*>(gs));
    allActors_ = PreparedCall(reinterpret_cast<UClass*>(gs), "GetAllActorsOfClass", {"WorldContextObject", "ActorClass", "OutActors"});
    UClass* prim = FindClass("PrimitiveComponent");
    UFunction* setOverlaps = prim ? FindFunction(prim, "SetGenerateOverlapEvents") : nullptr;
    const bool ok = allActors_.valid() && FindProperty(willie_, "Damage Marker Spheres") && FindProperty(willie_, "Mesh") && setOverlaps &&
                    FindProperty(setOverlaps, "bInGenerateOverlapEvents") && FindFunction(prim, "GetGenerateOverlapEvents") &&
                    FindFunction(prim, "GetCollisionEnabled");
    unusable_ = !ok;
    HSP_LOG("physics: %s", unusable_ ? "this game build differs from the one the mod knows; marker sphere fix off" : "marker sphere fix ready");
    return !unusable_;
}

bool PhysicsFixes::SetOverlaps(UObject* c, bool on) {
    Call call(c, "SetGenerateOverlapEvents");
    return call.valid() && call.set<bool>("bInGenerateOverlapEvents", on).invoke();
}

void PhysicsFixes::Islands(UObject* gameMode, Console console) {
    const int want = s_.physicsIslands && active_ ? s_.islandMinPerWorker : kGameIslandMin;
    if (want == islandsSet_ || (islandsSet_ == 0 && want == kGameIslandMin)) return;
    char cmd[96];
    std::snprintf(cmd, sizeof cmd, "p.Chaos.Solver.IslandGroups.MinBodiesPerWorker %d", want);
    console(gameMode, cmd);
    std::snprintf(cmd, sizeof cmd, "p.Chaos.Solver.IslandGroups.MinConstraintsPerWorker %d", want);
    console(gameMode, cmd);
    HSP_LOG("physics: island groups split from %d bodies / constraints per worker%s", want, want == kGameIslandMin ? " (the game's own)" : "");
    islandsSet_ = want;
}

// Once: what the first fighter's marker spheres, body and armour pieces have, so their cost can be judged.
void PhysicsFixes::LogFighter(UObject* fighter, UObject* body) {
    const auto* spheres = static_cast<TArrayRaw*>(FieldPtr(fighter, "Damage Marker Spheres"));
    const auto* worn = static_cast<TArrayRaw*>(FieldPtr(fighter, "Worn Armor"));
    if (!spheres || spheres->num <= 0 || !spheres->data) return;   // not set up yet
    logged_ = true;
    int collision[4] = {}, overlaps = 0, simulating = 0, other = 0;
    int type = -1;
    bool mixedTypes = false;
    for (int i = 0; i < spheres->num; ++i) {
        UObject* c = spheres->data[i];
        if (!Alive(c)) continue;
        const int ce = CallByte(c, "GetCollisionEnabled");
        if (ce >= 0 && ce < 4) ++collision[ce]; else ++other;
        overlaps += CallBool(c, "GetGenerateOverlapEvents");
        simulating += Simulating(c);
        const int t = CallByte(c, "GetCollisionObjectType");
        if (type < 0) type = t; else if (t != type) mixedTypes = true;
    }
    HSP_LOG("physics: %s marker spheres %d (collision none %d, query %d, physics %d, both %d, other %d; object type %d%s; overlap events %d; "
            "simulating %d); body collision %d, overlap events %d",
            ObjectName(fighter).c_str(), spheres->num, collision[0], collision[1], collision[2], collision[3], other, type,
            mixedTypes ? " and others" : "", overlaps, simulating, CallByte(body, "GetCollisionEnabled"), int(CallBool(body, "GetGenerateOverlapEvents")));
    for (int i = 0; worn && worn->data && i < worn->num; ++i) {
        UObject* p = worn->data[i];
        if (!Alive(p)) continue;
        HSP_LOG("physics: armour %s collision %d, object type %d, overlap events %d, simulating %d", ObjectName(p).c_str(),
                CallByte(p, "GetCollisionEnabled"), CallByte(p, "GetCollisionObjectType"), int(CallBool(p, "GetGenerateOverlapEvents")),
                int(Simulating(p)));
    }
}

void PhysicsFixes::Markers(const std::vector<UObject*>& fighters) {
    const bool want = s_.markerSpheres && active_;
    std::unordered_map<UObject*, bool> bodyOk;   // fighter -> its body mesh gives the actor its overlaps
    int markers = 0;
    for (UObject* w : fighters) {
        if (!Alive(w)) continue;
        UObject* body = Obj(w, "Mesh");
        if (!Alive(body)) continue;
        if (!logged_) LogFighter(w, body);
        // ECollisionEnabled QueryOnly, QueryAndPhysics or QueryAndProbe: the body makes query overlaps of its own.
        const int ce = want ? CallByte(body, "GetCollisionEnabled") : 0;
        const bool ok = want && (ce == 1 || ce == 3 || ce == 5) && CallBool(body, "GetGenerateOverlapEvents");
        bodyOk[w] = ok;
        const auto* spheres = static_cast<TArrayRaw*>(FieldPtr(w, "Damage Marker Spheres"));
        if (!spheres || !spheres->data || spheres->num <= 0) continue;
        markers += spheres->num;
        if (!ok) continue;
        for (int i = 0; i < spheres->num; ++i) {
            UObject* c = spheres->data[i];
            if (!Alive(c) || off_.count(c)) continue;
            // Only a sphere still on this fighter's body; one the game turned overlaps off on itself is left alone.
            if (!CallBool(c, "GetGenerateOverlapEvents")) off_[c] = Marker{ObjectIndex(c), w, false};
            else if (SetOverlaps(c, false)) off_[c] = Marker{ObjectIndex(c), w, true};
        }
    }
    // Back on wherever the body stopped covering for them (no collision, a destroyed fighter's leftovers), or when off.
    for (auto it = off_.begin(); it != off_.end();) {
        UObject* c = it->first;
        const bool alive = ObjectAt(it->second.index) == c && Alive(c);
        auto b = bodyOk.find(it->second.fighter);
        if (alive && b != bodyOk.end() && b->second) { ++it; continue; }
        if (alive && it->second.ours) SetOverlaps(c, true);
        it = off_.erase(it);
    }
    counts_.markers = markers;
    counts_.markersOff = 0;
    for (const auto& [c, m] : off_) counts_.markersOff += m.ours;
}

void PhysicsFixes::Pass(UObject* gameMode, Console console) {
    Islands(gameMode, console);
    counts_.islandMin = islandsSet_ ? islandsSet_ : kGameIslandMin;
    if (!Prepare()) return;
    if (!s_.markerSpheres || !active_) {
        if (off_.empty() && logged_) return;   // nothing of ours to put back (the diagnostic still runs once)
    }
    allActors_.put<UObject*>(0, gameMode).put<UClass*>(1, willie_);
    if (!allActors_.invoke(statics_)) return;
    const TArrayRaw found = allActors_.take<TArrayRaw>(2);
    std::vector<UObject*> fighters(found.data, found.data + (found.data && found.num > 0 ? found.num : 0));
    Markers(fighters);
}

}  // namespace hsp
