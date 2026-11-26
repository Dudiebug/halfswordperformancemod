#include "corpses.h"
#include "log.h"
#include <algorithm>

namespace hsp {
using namespace ue;

namespace {
constexpr int kStillPasses = 4;   // 1 s of passes with nothing changing
struct TArrayRaw { UObject** data; int32_t num, max; };
const char* const kWatched[] = {"Health", "Temperature", "Bleeding", "Consciousness", "Volume Gurgling", "All Body Tonus"};
constexpr int kWatchedCount = int(sizeof kWatched / sizeof kWatched[0]);
static_assert(kWatchedCount == 6, "CorpseFixes::Snapshot holds one value per watched property");

bool Alive(UObject* o) { return o && !IsGarbage(o); }

UObject* Obj(UObject* o, const char* prop) {
    UObject* const* f = Field<UObject*>(o, prop);
    return f ? *f : nullptr;
}

bool CallBool(UObject* o, const char* fn) {
    Call c(o, fn);
    return c.valid() && c.invoke() && c.get<bool>("ReturnValue");
}
}  // namespace

bool CorpseFixes::Prepare() {
    if (prepared_ && willie_ && ObjectAt(willieIndex_) != reinterpret_cast<UObject*>(willie_)) {
        prepared_ = unusable_ = false;
        off_.clear();
        watch_.clear();
    }
    if (prepared_) return !unusable_;
    willie_ = FindClass("Willie_BP_C");
    UObject* gs = FindObject("GameplayStatics", "Class");
    if (!willie_ || !gs) return false;
    prepared_ = true;
    willieIndex_ = ObjectIndex(reinterpret_cast<UObject*>(willie_));
    statics_ = DefaultObject(reinterpret_cast<UClass*>(gs));
    allActors_ = PreparedCall(reinterpret_cast<UClass*>(gs), "GetAllActorsOfClass", {"WorldContextObject", "ActorClass", "OutActors"});
    baseTick_ = FindFunction(willie_, "ReceiveTick");
    if (baseTick_ && OuterOf(reinterpret_cast<UObject*>(baseTick_)) != reinterpret_cast<UObject*>(willie_)) baseTick_ = nullptr;
    bool propsOk = true;
    for (const char* p : {"DED", "Player", "Death Timeline", "Mesh", "DriverSkeleton"})
        if (!FindProperty(willie_, p)) { HSP_LOG("corpses: %s MISSING", p); propsOk = false; }
    for (const char* p : kWatched)
        if (!FindProperty(willie_, p) || PropertySize(FindProperty(willie_, p)) != 8) { HSP_LOG("corpses: %s MISSING", p); propsOk = false; }
    UClass* prim = FindClass("PrimitiveComponent");
    UClass* actor = FindClass("Actor");
    UClass* timeline = FindClass("TimelineComponent");
    const bool fnOk = prim && FindFunction(prim, "IsAnyRigidBodyAwake") && actor && FindFunction(actor, "SetActorTickEnabled") &&
                      FindProperty(FindFunction(actor, "SetActorTickEnabled"), "bEnabled") && FindFunction(actor, "IsActorTickEnabled") &&
                      timeline && FindFunction(timeline, "IsPlaying");
    unusable_ = !allActors_.valid() || !baseTick_ || !propsOk || !fnOk;
    HSP_LOG("corpses: %s", unusable_ ? "this game build differs from the one the mod knows; corpse fix off" : "corpse fix ready");
    return !unusable_;
}

// The ragdoll (or the animation driver) has a body awake, or the fighter is no longer dead: its tick has work to do.
bool CorpseFixes::Awake(UObject* f) {
    bool ded = false;
    if (!GetBool(f, "DED", ded) || !ded) return true;
    UObject* body = Obj(f, "Mesh");
    if (!Alive(body) || CallBool(body, "IsAnyRigidBodyAwake")) return true;
    UObject* driver = Obj(f, "DriverSkeleton");
    return Alive(driver) && CallBool(driver, "IsAnyRigidBodyAwake");
}

bool CorpseFixes::Settled(UObject* f, Snapshot& snap) {
    bool player = true;
    if (!GetBool(f, "Player", player) || player) return false;   // the player's own body is never touched
    if (Awake(f)) { snap.still = 0; return false; }
    UObject* death = Obj(f, "Death Timeline");
    if (Alive(death) && CallBool(death, "IsPlaying")) { snap.still = 0; return false; }
    double now[kWatchedCount];
    for (int i = 0; i < kWatchedCount; ++i) {
        const double* v = Field<double>(f, kWatched[i]);
        if (!v) return false;
        now[i] = *v;
    }
    snap.still = std::equal(now, now + kWatchedCount, snap.v) ? snap.still + 1 : 0;
    std::copy(now, now + kWatchedCount, snap.v);
    return snap.still >= kStillPasses;
}

void CorpseFixes::Wake(UObject* f) { Call(f, "SetActorTickEnabled").set<bool>("bEnabled", true).invoke(); }

UObject* CorpseFixes::Resolve(const Off& o) const {
    UObject* now = ObjectAt(o.index);
    return now == o.obj && ClassOf(now) == o.cls && !IsGarbage(now) ? now : nullptr;
}

void CorpseFixes::Frame() {
    if (off_.empty()) return;
    off_.erase(std::remove_if(off_.begin(), off_.end(), [&](const Off& o) {
        UObject* f = Resolve(o);
        if (!f) return true;
        if (!Awake(f)) return false;
        Wake(f);
        return true;
    }), off_.end());
}

void CorpseFixes::WakeAll() {
    for (const Off& o : off_)
        if (UObject* f = Resolve(o)) Wake(f);
    off_.clear();
    watch_.clear();
}

void CorpseFixes::SetActive(bool on) {
    active_ = on;
    if (!on) WakeAll();
}

void CorpseFixes::Pass(UObject* gameMode) {
    if (!s_.corpseSleep || !active_) { if (!off_.empty() || !watch_.empty()) WakeAll(); counts_ = {}; return; }
    if (!Prepare()) return;
    allActors_.put<UObject*>(0, gameMode).put<UClass*>(1, willie_);
    if (!allActors_.invoke(statics_)) return;
    const TArrayRaw found = allActors_.take<TArrayRaw>(2);
    const std::vector<UObject*> fighters(found.data, found.data + (found.data && found.num > 0 ? found.num : 0));
    std::unordered_map<UObject*, Snapshot> watch;
    CorpseCounts n;
    for (UObject* f : fighters) {
        if (!Alive(f)) continue;
        bool ded = false;
        if (!GetBool(f, "DED", ded) || !ded) continue;
        ++n.dead;
        if (std::any_of(off_.begin(), off_.end(), [&](const Off& o) { return o.obj == f; })) continue;
        UClass* cls = ClassOf(f);
        if (FindFunction(cls, "ReceiveTick") != baseTick_) continue;   // a subclass with its own tick: left alone
        if (!CallBool(f, "IsActorTickEnabled")) continue;              // never re-enable a tick the game turned off
        auto it = watch_.find(f);
        Snapshot snap = it == watch_.end() ? Snapshot{} : it->second;
        if (!Settled(f, snap)) { watch[f] = snap; continue; }
        if (Call(f, "SetActorTickEnabled").set<bool>("bEnabled", false).invoke()) off_.push_back({f, ObjectIndex(f), cls});
    }
    watch_.swap(watch);
    n.tickOff = int(off_.size());
    counts_ = n;
}

}  // namespace hsp
