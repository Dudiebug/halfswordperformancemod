#include "weapons.h"
#include "log.h"
#include <algorithm>

namespace hsp {
using namespace ue;

namespace {
struct TArrayRaw { UObject** data; int32_t num, max; };

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

bool WeaponFixes::Prepare() {
    if (prepared_ && weapon_ && ObjectAt(weaponIndex_) != reinterpret_cast<UObject*>(weapon_)) prepared_ = unusable_ = false;
    if (prepared_) return !unusable_;
    weapon_ = FindClass("ModularWeaponBP_C");
    UObject* gs = FindObject("GameplayStatics", "Class");
    if (!weapon_ || !gs) return false;   // not loaded yet: asked again next pass
    prepared_ = true;
    weaponIndex_ = ObjectIndex(reinterpret_cast<UObject*>(weapon_));
    statics_ = DefaultObject(reinterpret_cast<UClass*>(gs));
    allActors_ = PreparedCall(reinterpret_cast<UClass*>(gs), "GetAllActorsOfClass", {"WorldContextObject", "ActorClass", "OutActors"});
    baseTick_ = FindFunction(weapon_, "ReceiveTick");
    if (baseTick_ && OuterOf(reinterpret_cast<UObject*>(baseTick_)) != reinterpret_cast<UObject*>(weapon_)) baseTick_ = nullptr;
    const char* props[] = {"Parent Actor", "Last Parent", "Weapons With Disabled Collision", "Due Weapon Skill", "Just Parried Rate",
                           "Just Parried", "Sliding", "BaseMesh", "Sound Slide", "Weapon Velocity_Old"};
    bool propsOk = true;
    for (const char* p : props)
        if (!FindProperty(weapon_, p)) { HSP_LOG("weapons: %s MISSING", p); propsOk = false; }
    UClass* audio = FindClass("AudioComponent");
    UClass* prim = FindClass("PrimitiveComponent");
    UClass* actor = FindClass("Actor");
    const bool fnOk = audio && FindProperty(audio, "VolumeMultiplier") && prim && FindFunction(prim, "IsAnyRigidBodyAwake") &&
                      FindFunction(prim, "GetPhysicsLinearVelocity") && actor && FindFunction(actor, "SetActorTickEnabled") &&
                      FindProperty(FindFunction(actor, "SetActorTickEnabled"), "bEnabled") &&
                      FindFunction(actor, "IsActorTickEnabled");
    unusable_ = !allActors_.valid() || !baseTick_ || !propsOk || !fnOk;
    HSP_LOG("weapons: %s", unusable_ ? "this game build differs from the one the mod knows; idle weapon fix off" : "idle weapon fix ready");
    return !unusable_;
}

// Every value the weapon's tick reads or advances is at rest, so a tick would change nothing.
bool WeaponFixes::Idle(UObject* w) {
    if (Alive(Obj(w, "Parent Actor")) || Obj(w, "Last Parent")) return false;
    const auto* disabled = static_cast<TArrayRaw*>(FieldPtr(w, "Weapons With Disabled Collision"));
    if (!disabled || disabled->num != 0) return false;
    const double* skill = Field<double>(w, "Due Weapon Skill");
    const double* parried = Field<double>(w, "Just Parried Rate");
    if (!skill || *skill != 0 || !parried || *parried != 0) return false;
    bool b = true;
    if (!GetBool(w, "Just Parried", b) || b || !GetBool(w, "Sliding", b) || b) return false;
    UObject* sound = Obj(w, "Sound Slide");
    const float* vol = Alive(sound) ? Field<float>(sound, "VolumeMultiplier") : nullptr;
    if (!vol || *vol != 0) return false;
    UObject* mesh = Obj(w, "BaseMesh");
    return Alive(mesh) && !CallBool(mesh, "IsAnyRigidBodyAwake");
}

void WeaponFixes::Wake(UObject* w) {
    // The tick's woosh test compares this frame's velocity with Weapon Velocity_Old; start it from now, not from rest.
    if (UObject* mesh = Obj(w, "BaseMesh"); Alive(mesh)) {
        Call v(mesh, "GetPhysicsLinearVelocity");
        if (v.valid() && v.invoke())
            if (FVector* old = Field<FVector>(w, "Weapon Velocity_Old")) *old = v.get<FVector>("ReturnValue");
    }
    Call(w, "SetActorTickEnabled").set<bool>("bEnabled", true).invoke();
}

UObject* WeaponFixes::Resolve(const Off& o) const {
    UObject* now = ObjectAt(o.index);
    return now == o.obj && ClassOf(now) == o.cls && !IsGarbage(now) ? now : nullptr;
}

void WeaponFixes::Frame() {
    if (off_.empty()) return;
    off_.erase(std::remove_if(off_.begin(), off_.end(), [&](const Off& o) {
        UObject* w = Resolve(o);
        if (!w) return true;   // destroyed: nothing to undo
        UObject* mesh = Obj(w, "BaseMesh");
        if (!Alive(Obj(w, "Parent Actor")) && Alive(mesh) && !CallBool(mesh, "IsAnyRigidBodyAwake")) return false;
        Wake(w);
        return true;
    }), off_.end());
}

void WeaponFixes::SetActive(bool on) {
    active_ = on;
    if (on) return;
    for (const Off& o : off_)
        if (UObject* w = Resolve(o)) Wake(w);
    off_.clear();
}

void WeaponFixes::Pass(UObject* gameMode) {
    if (!s_.idleWeapons || !active_ || !Prepare()) return;
    allActors_.put<UObject*>(0, gameMode).put<UClass*>(1, weapon_);
    if (!allActors_.invoke(statics_)) return;
    const TArrayRaw found = allActors_.take<TArrayRaw>(2);
    const std::vector<UObject*> weapons(found.data, found.data + (found.data && found.num > 0 ? found.num : 0));
    WeaponCounts n;
    for (UObject* w : weapons) {
        if (!Alive(w)) continue;
        ++n.weapons;
        if (std::any_of(off_.begin(), off_.end(), [&](const Off& o) { return o.obj == w; })) continue;
        // A weapon class with its own tick (candlestick flame, crossbow) does more than this one: left alone.
        UClass* cls = ClassOf(w);
        if (FindFunction(cls, "ReceiveTick") != baseTick_) continue;
        if (!CallBool(w, "IsActorTickEnabled") || !Idle(w)) continue;   // never re-enable a tick the game turned off
        if (Call(w, "SetActorTickEnabled").set<bool>("bEnabled", false).invoke()) off_.push_back({w, ObjectIndex(w), cls});
    }
    n.tickOff = int(off_.size());
    counts_ = n;
}

}  // namespace hsp
