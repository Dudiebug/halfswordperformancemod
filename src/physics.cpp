#include "physics.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cwctype>
#include <string>
#include <thread>

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

// The engine's own choice for the scene solver (FChaosScene: TaskGraph when FApp::ShouldUseThreadingForPerformance,
// which is false only with -onethread or -noperfthreads, or on one core). The ini's DefaultThreadingModel is not read.
void LogSolverThreading() {
    std::wstring cmd = GetCommandLineW();
    for (auto& ch : cmd) ch = wchar_t(std::towlower(ch));
    const bool switchOff = cmd.find(L"onethread") != std::wstring::npos || cmd.find(L"noperfthreads") != std::wstring::npos;
    const unsigned cores = std::thread::hardware_concurrency();
    HSP_LOG("physics: scene solver %s (the engine's choice; %u logical cores%s)", switchOff || cores == 1 ? "single thread" : "on the task graph",
            cores, switchOff ? ", -onethread or -noperfthreads on the command line" : "");
}
}  // namespace

bool PhysicsFixes::Prepare() {
    if (prepared_ && willie_ && ObjectAt(willieIndex_) != reinterpret_cast<UObject*>(willie_)) prepared_ = unusable_ = false;   // a new level's class
    if (prepared_) return !unusable_;
    willie_ = FindClass("Willie_BP_C");
    UObject* gs = FindObject("GameplayStatics", "Class");
    if (!willie_ || !gs) return false;
    prepared_ = true;
    willieIndex_ = ObjectIndex(reinterpret_cast<UObject*>(willie_));
    statics_ = DefaultObject(reinterpret_cast<UClass*>(gs));
    allActors_ = PreparedCall(reinterpret_cast<UClass*>(gs), "GetAllActorsOfClass", {"WorldContextObject", "ActorClass", "OutActors"});
    unusable_ = !allActors_.valid() || !FindProperty(willie_, "Mesh");
    return !unusable_;
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

void PhysicsFixes::Pass(UObject* gameMode, Console console) {
    if (!threadingLogged_) { LogSolverThreading(); threadingLogged_ = true; }
    Islands(gameMode, console);
    counts_.islandMin = islandsSet_ ? islandsSet_ : kGameIslandMin;
    if (logged_ || !Prepare()) return;
    allActors_.put<UObject*>(0, gameMode).put<UClass*>(1, willie_);
    if (!allActors_.invoke(statics_)) return;
    const TArrayRaw found = allActors_.take<TArrayRaw>(2);
    for (int i = 0; found.data && i < found.num && !logged_; ++i) {
        UObject* w = found.data[i];
        UObject* body = Alive(w) ? Obj(w, "Mesh") : nullptr;
        if (Alive(body)) LogFighter(w, body);
    }
}

}  // namespace hsp
