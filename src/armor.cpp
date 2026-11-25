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
}  // namespace

bool ArmorFixes::Prepare() {
    if (prepared_ && willie_ && ObjectAt(willieIndex_) != reinterpret_cast<UObject*>(willie_)) {
        prepared_ = unusable_ = false;
        pieces_.clear();
        farFighters_.clear();
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
            // The game gives each piece 10x bounds (its own box at the body's root would miss a fighter lying down).
            // Under leader pose the piece's bounds come from the body's physics bodies, so they need no margin.
            const float* scale = Field<float>(piece, "BoundsScale");
            if (s_.armorBounds && scale && *scale > kArmorBoundsScale) {
                const float was = *scale;
                if (SetBoundsScale(piece, kArmorBoundsScale)) rec.scale = was;
            }
        } else if (failures_ < 5) {
            ++failures_;
            HSP_LOG("armour: %s not on its fighter's body; left as the game set it up", ObjectName(piece).c_str());
        }
    } else if (!wantLeader && rec.leader) {
        if (rec.scale > 0 && SetBoundsScale(piece, rec.scale)) rec.scale = 0;
        SetLeader(piece, nullptr);
        rec.leader = false;
    }
    const bool wantOff = active_ && s_.shadowTrim && !wantShadow;
    if (wantOff != rec.shadowOff && SetCastShadow(piece, !wantOff)) rec.shadowOff = wantOff;
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
    HSP_LOG("bounds: %s body (%.0f %.0f %.0f) r %.0f scale %.0f; %s (%.0f %.0f %.0f) r %.0f scale %.1f",
            ObjectName(fighter).c_str(), bo.x, bo.y, bo.z, br, bs ? *bs : -1.0f, ObjectName(piece).c_str(), po.x, po.y, po.z, pr,
            ps ? *ps : -1.0f);
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
    ArmorCounts n;
    bool logged = false;
    for (UObject* w : fighters) {
        if (!Alive(w)) continue;
        ++n.fighters;
        if (!s_.vertexPaint) SetBool(w, "Force Disable Vertex Paint", true);   // the game's own switch, set again on every pass
        UObject* const* meshField = Field<UObject*>(w, "Mesh");
        UObject* body = meshField ? *meshField : nullptr;
        auto* worn = static_cast<TArrayRaw*>(FieldPtr(w, "Worn Armor"));
        if (!Alive(body) || !worn || !worn->data || worn->num <= 0 || worn->num > 256) continue;

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
        }
        if (!logged && passes_ % 40 == 0 && !list.empty()) {
            auto it = seen.find(list.front());
            if (it != seen.end() && it->second.leader) { LogBounds(w, body, list.front()); logged = true; }
        }
    }
    ++passes_;
    pieces_.swap(seen);
    farFighters_.swap(far);
    counts_ = n;
}

}  // namespace hsp
