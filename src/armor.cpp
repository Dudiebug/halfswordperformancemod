#include "armor.h"
#include "log.h"
#include <cmath>

namespace hsp {
using namespace ue;

namespace {
constexpr float kArmorBoundsScale = 1.0f;
struct TArrayRaw { UObject** data; int32_t num, max; };
static_assert(sizeof(TArrayRaw) == 16, "TArray header");

bool Alive(UObject* o) { return o && !IsGarbage(o); }

UObject* AttachParent(UObject* comp) {
    Call c(comp, "GetAttachParent");
    return c.valid() && c.invoke() ? c.get<UObject*>("ReturnValue") : nullptr;
}

bool SetLeader(UObject* piece, UObject* leader) {
    Call c(piece, "SetLeaderPoseComponent");
    if (!c.valid()) return false;
    c.set<UObject*>("NewLeaderBoneComponent", leader);
    c.set<bool>("bForceUpdate", leader != nullptr);
    c.set<bool>("bInFollowerShouldTickPose", false);
    return c.invoke();
}

bool SetBoundsScale(UObject* comp, float scale) {
    Call c(comp, "SetBoundsScale");
    return c.valid() && c.set<float>("NewBoundsScale", scale).invoke();
}

bool SetCastShadow(UObject* comp, bool on) {
    Call c(comp, "SetCastShadow");
    return c.valid() && c.set<bool>("NewCastShadow", on).invoke();
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-3; }

// The game's bounds for a body are 1000x (radius ~940 m: never culled, its shadow always drawn). Its physics bodies
// give bounds that follow the ragdoll; 2x leaves room for a limb that is between two bone updates.
constexpr float kBodyBoundsScale = 2.0f;
constexpr const char* kFollowers[2] = {"CharacterMesh_Head", "SK_Skeleton"};
}  // namespace

bool ArmorFixes::Prepare() {
    if (prepared_ && willie_ && ObjectAt(willieIndex_) != reinterpret_cast<UObject*>(willie_)) {
        prepared_ = unusable_ = false;
        pieces_.clear();
        farFighters_.clear();
        bodies_.clear();
    }
    if (prepared_) return !unusable_;
    willie_ = FindClass("Willie_BP_C");
    UObject* gs = FindObject("GameplayStatics", "Class");
    if (!willie_ || !gs) return false;   // not loaded yet: asked again next pass
    prepared_ = true;
    willieIndex_ = ObjectIndex(reinterpret_cast<UObject*>(willie_));
    statics_ = DefaultObject(reinterpret_cast<UClass*>(gs));
    allActors_ = PreparedCall(reinterpret_cast<UClass*>(gs), "GetAllActorsOfClass", {"WorldContextObject", "ActorClass", "OutActors"});
    // Every name this mod passes, checked once against the running build (a missing one = that fix stays off).
    UClass* skel = FindClass("SkeletalMeshComponent");
    UFunction* leader = skel ? FindFunction(skel, "SetLeaderPoseComponent") : nullptr;
    const bool leaderOk = leader && FindProperty(leader, "NewLeaderBoneComponent") && FindProperty(leader, "bForceUpdate") &&
                          FindProperty(leader, "bInFollowerShouldTickPose");
    UClass* prim = FindClass("PrimitiveComponent");
    UFunction* scale = prim ? FindFunction(prim, "SetBoundsScale") : nullptr;
    const bool boundsOk = scale && FindProperty(scale, "NewBoundsScale") && FindProperty(prim, "BoundsScale");
    if (UObject* ks = FindObject("KismetSystemLibrary", "Class")) kismet_ = DefaultObject(reinterpret_cast<UClass*>(ks));
    const bool armorOk = FindProperty(willie_, "Worn Armor") && FindProperty(willie_, "Mesh");
    HSP_LOG("armour: GetAllActorsOfClass %s, SetLeaderPoseComponent %s, SetBoundsScale %s, Worn Armor %s",
            allActors_.valid() ? "ok" : "MISSING", leaderOk ? "ok" : "MISSING", boundsOk ? "ok" : "MISSING", armorOk ? "ok" : "MISSING");
    unusable_ = !allActors_.valid() || !armorOk || !leaderOk || !boundsOk;
    if (unusable_) HSP_LOG("armour: this game build differs from the one the mod knows; armour fixes off");
    // The optional parts: each one missing only turns its own setting off.
    rtParam_.clear();
    if (UFunction* rt = prim ? FindFunction(prim, "SetVisibleInRayTracing") : nullptr) {
        const std::vector<FProperty*> ps = Params(rt);
        if (ps.size() == 1 && FindProperty(prim, "bVisibleInRayTracing")) rtParam_ = PropertyName(ps[0]);
    }
    FProperty* tickOpt = skel ? FindProperty(skel, "VisibilityBasedAnimTickOption") : nullptr;
    tickOptOk_ = tickOpt && PropertySize(tickOpt) == 1;
    const bool followersOk = skel && FindProperty(skel, "AnimClass") && FindProperty(prim, "AttachSocketName") &&
                             FindProperty(willie_, kFollowers[0]) && FindProperty(willie_, kFollowers[1]);
    HSP_LOG("armour: SetVisibleInRayTracing %s, VisibilityBasedAnimTickOption %s, head and skeleton meshes %s",
            rtParam_.empty() ? "MISSING" : rtParam_.c_str(), tickOptOk_ ? "ok" : "MISSING", followersOk ? "ok" : "MISSING");
    followersOk_ = followersOk;
    return !unusable_;
}

bool ArmorFixes::CameraLocation(UObject* gameMode, FVector& out) {
    Call cm(statics_, "GetPlayerCameraManager");
    if (!cm.valid() || !cm.set<UObject*>("WorldContextObject", gameMode).set<int32_t>("PlayerIndex", 0).invoke()) return false;
    UObject* cam = cm.get<UObject*>("ReturnValue");
    if (!Alive(cam)) return false;
    Call loc(cam, "GetCameraLocation");
    if (!loc.valid() || !loc.invoke()) return false;
    out = loc.get<FVector>("ReturnValue");
    return true;
}

void ArmorFixes::FixPiece(UObject* piece, UObject* body, Piece& rec, bool wantShadow) {
    const bool wantLeader = active_ && s_.armorLeaderPose;
    if (wantLeader && !rec.leader) {
        // Only a piece the game attached to the body itself (a cut-off limb's or a dropped piece's is left alone).
        if (AttachParent(piece) == body && SetLeader(piece, body)) {
            rec.leader = true;
        } else if (failures_ < 5) {
            ++failures_;
            HSP_LOG("armour: %s not on its fighter's body; left as the game set it up", ObjectName(piece).c_str());
        }
    } else if (!wantLeader && rec.leader) {
        if (rec.scale > 0 && SetBoundsScale(piece, rec.scale)) rec.scale = 0;
        SetLeader(piece, nullptr);
        rec.leader = false;
    }
    // The game gives each piece 10x bounds (its own box at the body's root would miss a fighter lying down). Under
    // leader pose the piece's bounds come from the body's physics bodies, so they need no margin.
    const bool wantBounds = rec.leader && s_.armorBounds;
    if (wantBounds && rec.scale == 0) {
        const float* scale = Field<float>(piece, "BoundsScale");
        if (scale && *scale > kArmorBoundsScale) {
            const float was = *scale;
            if (SetBoundsScale(piece, kArmorBoundsScale)) rec.scale = was;
        }
    } else if (!wantBounds && rec.scale > 0 && SetBoundsScale(piece, rec.scale)) {
        rec.scale = 0;
    }
    const bool wantOff = active_ && s_.shadowTrim && !wantShadow;
    if (wantOff != rec.shadowOff && SetCastShadow(piece, !wantOff)) rec.shadowOff = wantOff;
    // Out of hardware ray tracing: Lumen's reflections and GI skip the piece (its ray tracing geometry is rebuilt every
    // frame it moves). A piece the game already keeps out is left alone, so turning this on again restores only ours.
    if (rtParam_.empty()) return;
    const bool wantRtOff = active_ && !s_.armorRayTracing;
    if (wantRtOff && !rec.rtOff) {
        bool visible = false;
        if (GetBool(piece, "bVisibleInRayTracing", visible) && visible) {
            Call c(piece, "SetVisibleInRayTracing");
            if (c.valid() && c.set<bool>(rtParam_, false).invoke()) rec.rtOff = true;
        }
    } else if (!wantRtOff && rec.rtOff) {
        Call c(piece, "SetVisibleInRayTracing");
        if (c.valid() && c.set<bool>(rtParam_, true).invoke()) rec.rtOff = false;
    }
}

// The body's bounds. VisibilityBasedAnimTickOption goes to AlwaysTickPoseAndRefreshBones first: with bounds that can
// leave the screen, the default (AlwaysTickPose) would stop refreshing the bones of a body nobody sees, and the
// armour following it would freeze. Undone in the other order.
void ArmorFixes::FixBody(UObject* body, Body& rec) {
    const bool want = active_ && s_.bodyBounds && tickOptOk_;
    uint8_t* tick = tickOptOk_ ? Field<uint8_t>(body, "VisibilityBasedAnimTickOption") : nullptr;
    const float* scale = Field<float>(body, "BoundsScale");
    if (want && tick && scale && *scale > kBodyBoundsScale) {   // also when the game set its 1000x again
        const float was = *scale;
        if (rec.tick < 0) { rec.tick = *tick; *tick = 0; }
        if (SetBoundsScale(body, kBodyBoundsScale) && rec.scale == 0) rec.scale = was;
    } else if (!want && (rec.scale > 0 || rec.tick >= 0)) {
        if (rec.scale > 0 && SetBoundsScale(body, rec.scale)) rec.scale = 0;
        if (rec.scale == 0 && rec.tick >= 0 && tick) { *tick = uint8_t(rec.tick); rec.tick = -1; }
    }
}

// CharacterMesh_Head (which = 0) or SK_Skeleton (1). Only when the mesh copies the body's pose the way armour does:
// a CopyPose anim blueprint, attached to the body at its root with no offset. Anything else is logged and left alone.
void ArmorFixes::FixFollower(UObject* fighter, UObject* body, int which, Body& rec) {
    UObject* const* field = Field<UObject*>(fighter, kFollowers[which]);
    UObject* m = field ? *field : nullptr;
    UObject*& done = rec.follower[which];
    if (done && done != m) done = nullptr;   // the game replaced the mesh (a new head): the old one is gone
    const bool want = active_ && s_.headLeaderPose && followersOk_;
    if (!Alive(m) || !followersOk_) return;
    if (!want && done && SetLeader(m, nullptr)) done = nullptr;
    if (done || (!want && followerLogged_[which])) return;
    UClass* const* anim = Field<UClass*>(m, "AnimClass");
    const std::string animName = anim && *anim ? ObjectName(reinterpret_cast<UObject*>(*anim)) : "none";
    const FName* socket = Field<FName>(m, "AttachSocketName");
    const FVector* loc = Field<FVector>(m, "RelativeLocation");
    const FRotator* rot = Field<FRotator>(m, "RelativeRotation");
    const FVector* sc = Field<FVector>(m, "RelativeScale3D");
    const bool parentOk = AttachParent(m) == body;
    const bool socketOk = socket && socket->index == 0;
    const bool offsetOk = loc && rot && sc && Near(loc->x, 0) && Near(loc->y, 0) && Near(loc->z, 0) && Near(rot->pitch, 0) &&
                          Near(rot->yaw, 0) && Near(rot->roll, 0) && Near(sc->x, 1) && Near(sc->y, 1) && Near(sc->z, 1);
    const bool copyPose = animName.find("CopyPose") != std::string::npos;
    if (!followerLogged_[which]) {
        followerLogged_[which] = true;
        HSP_LOG("follower: %s anim %s, on the body %s, socket %s, offset %s", kFollowers[which], animName.c_str(), parentOk ? "yes" : "no",
                socketOk ? "none" : socket ? NameToString(*socket).c_str() : "?", offsetOk ? "none" : "yes");
    }
    if (want && copyPose && parentOk && socketOk && offsetOk && SetLeader(m, body)) done = m;
}

// Every 10 s, one fighter: where the game's bounds for the body and for one leader-pose piece sit. With armor_bounds=1
// the piece's centre should follow the body's when the fighter is down; if it stays at the capsule, the 10x was needed.
// The first time, also what the body's 1000x bounds (Willie_BP Set Up) may be covering for.
void ArmorFixes::LogBounds(UObject* fighter, UObject* body, UObject* piece) {
    if (!kismet_) return;
    auto bounds = [&](UObject* c, FVector& o, double& r) {
        Call b(kismet_, "GetComponentBounds");
        if (!b.valid() || !b.set<UObject*>("Component", c).invoke()) return false;
        o = b.get<FVector>("Origin");
        r = b.get<float>("SphereRadius");
        return true;
    };
    FVector bo{}, po{};
    double br = 0, pr = 0;
    if (!bounds(body, bo, br) || !bounds(piece, po, pr)) return;
    const float* bs = Field<float>(body, "BoundsScale");
    const float* ps = Field<float>(piece, "BoundsScale");
    const uint8_t* bt = Field<uint8_t>(body, "VisibilityBasedAnimTickOption");
    HSP_LOG("bounds: %s body (%.0f %.0f %.0f) r %.0f scale %.1f tick %d; %s (%.0f %.0f %.0f) r %.0f scale %.1f",
            ObjectName(fighter).c_str(), bo.x, bo.y, bo.z, br, bs ? *bs : -1.0f, bt ? *bt : -1, ObjectName(piece).c_str(), po.x, po.y,
            po.z, pr, ps ? *ps : -1.0f);
    if (loggedBody_) return;
    loggedBody_ = true;
    UObject* const* driverField = Field<UObject*>(fighter, "DriverSkeleton");
    for (UObject* m : {body, driverField ? *driverField : nullptr}) {
        if (!Alive(m)) continue;
        const uint8_t* tick = Field<uint8_t>(m, "VisibilityBasedAnimTickOption");
        const int32_t* lod = Field<int32_t>(m, "ForcedLodModel");
        bool uro = false;
        const bool uroOk = GetBool(m, "bEnableUpdateRateOptimizations", uro);
        HSP_LOG("bounds: %s VisibilityBasedAnimTickOption %d, bEnableUpdateRateOptimizations %d, ForcedLodModel %d",
                ObjectName(m).c_str(), tick ? *tick : -1, uroOk ? uro : -1, lod ? *lod : -1);
    }
}

void ArmorFixes::Pass(UObject* gameMode) {
    if (!Prepare()) return;
    allActors_.put<UObject*>(0, gameMode).put<UClass*>(1, willie_);
    if (!allActors_.invoke(statics_)) return;
    const TArrayRaw found = allActors_.take<TArrayRaw>(2);
    std::vector<UObject*> fighters(found.data, found.data + (found.data && found.num > 0 ? found.num : 0));

    FVector cam{};
    const bool haveCam = s_.shadowTrim && active_ && CameraLocation(gameMode, cam);
    std::unordered_map<UObject*, Piece> seen;
    std::unordered_map<UObject*, bool> far;
    std::unordered_map<UObject*, Body> bodies;
    ArmorCounts n;
    bool logged = false;
    for (UObject* w : fighters) {
        if (!Alive(w)) continue;
        ++n.fighters;
        // The game's own switch, set again on every pass; turned on again in the menu, set back to the game's default.
        if (!s_.vertexPaint) paintTouched_ = SetBool(w, "Force Disable Vertex Paint", true) || paintTouched_;
        else if (paintTouched_) SetBool(w, "Force Disable Vertex Paint", false);
        UObject* const* meshField = Field<UObject*>(w, "Mesh");
        UObject* body = meshField ? *meshField : nullptr;
        auto* worn = static_cast<TArrayRaw*>(FieldPtr(w, "Worn Armor"));
        if (!Alive(body)) continue;
        {
            auto bt = bodies_.find(w);
            Body brec = bt == bodies_.end() || bt->second.mesh != body ? Body{} : bt->second;
            brec.mesh = body;
            FixBody(body, brec);
            for (int i = 0; i < 2; ++i) FixFollower(w, body, i, brec);
            n.bodyBounds += brec.scale > 0;
            n.headLeader += (brec.follower[0] != nullptr) + (brec.follower[1] != nullptr);
            bodies[w] = brec;
        }
        if (!worn || !worn->data || worn->num <= 0 || worn->num > 256) continue;

        bool shadow = true;
        if (haveCam) {
            Call at(w, "K2_GetActorLocation");
            if (at.valid() && at.invoke()) {
                const FVector p = at.get<FVector>("ReturnValue");
                const double m = std::sqrt((p.x - cam.x) * (p.x - cam.x) + (p.y - cam.y) * (p.y - cam.y) + (p.z - cam.z) * (p.z - cam.z)) / 100.0;
                auto it = farFighters_.find(w);
                shadow = core::ShadowWanted(it == farFighters_.end() || !it->second, m, s_.shadowTrimMeters);
            }
        }
        far[w] = !shadow;
        // Copied first: a call below could run game code that changes the array.
        const std::vector<UObject*> list(worn->data, worn->data + worn->num);
        for (UObject* piece : list) {
            if (!Alive(piece) || seen.count(piece)) continue;
            auto it = pieces_.find(piece);
            Piece rec = it == pieces_.end() ? Piece{} : it->second;
            FixPiece(piece, body, rec, shadow);
            seen[piece] = rec;
            ++n.pieces;
            n.leader += rec.leader;
            n.shadowOff += rec.shadowOff;
            n.rtOff += rec.rtOff;
        }
        if (!logged && passes_ % 40 == 0 && !list.empty()) {
            auto it = seen.find(list.front());
            if (it != seen.end() && it->second.leader) { LogBounds(w, body, list.front()); logged = true; }
        }
    }
    ++passes_;
    pieces_.swap(seen);
    farFighters_.swap(far);
    bodies_.swap(bodies);
    counts_ = n;
}

}  // namespace hsp
