// The armour fixes, run on the game thread a few times a second.
//
// The game gives each worn armour piece its own SkeletalMeshComponent running ABP_CopyPose, an anim blueprint whose
// only node copies the body's pose bone by bone every frame, and scales the piece's bounds by 10 (Willie_BP
// "Set Up Armor"). A fully armoured fighter has 10-20+ such pieces. Leader pose (SetLeaderPoseComponent) makes a piece
// reuse the body's bone transforms with no anim instance update or evaluation, and bounds taken from the body let
// the renderer cull armour off screen. Both are undone by clearing the leader again (the piece's own anim instance
// is never removed), which the A/B key does live.
//
// The same pass handles the fighter's own meshes when their settings are on: the body's 1000x bounds (Willie_BP
// "Set Up"), the head and skeleton meshes (leader pose, as for armour), and armour in hardware ray tracing.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace hsp {

struct ArmorCounts { int fighters = 0, pieces = 0, leader = 0, shadowOff = 0, rtOff = 0, bodyBounds = 0, headLeader = 0; };

class ArmorFixes {
public:
    explicit ArmorFixes(const core::Settings& s) : s_(s) {}   // s is read on every pass: the menu changes it live
    // gameMode: the ticking BP_HalfSwordGameMode_C (world context). Cheap when there is nothing new.
    void Pass(ue::UObject* gameMode);
    void SetActive(bool on) { active_ = on; }   // false: every fix undone on the next pass (the A/B key)
    bool Active() const { return active_; }
    ArmorCounts Counts() const { return counts_; }

private:
    // scale: the game's BoundsScale, 0 = untouched. rtOff: this mod took the piece out of ray tracing.
    struct Piece { bool leader = false; bool shadowOff = false; bool rtOff = false; float scale = 0; };
    // A fighter's own meshes. scale/tick: the body's BoundsScale and VisibilityBasedAnimTickOption before this mod
    // changed them (0 / -1 = untouched). head/skeleton: CharacterMesh_Head / SK_Skeleton follow the body.
    struct Body { ue::UObject* mesh = nullptr; float scale = 0; int tick = -1; ue::UObject* follower[2] = {}; };
    bool Prepare();
    bool CameraLocation(ue::UObject* gameMode, ue::FVector& out);
    void FixPiece(ue::UObject* piece, ue::UObject* body, Piece& rec, bool wantShadow);
    void LogBounds(ue::UObject* fighter, ue::UObject* body, ue::UObject* piece);   // for the in-game check of armor_bounds
    void FixBody(ue::UObject* body, Body& rec);
    void FixFollower(ue::UObject* fighter, ue::UObject* body, int which, Body& rec);

    const core::Settings& s_;
    bool active_ = true;
    bool prepared_ = false, unusable_ = false;
    ue::UClass* willie_ = nullptr;
    int32_t willieIndex_ = -1;          // its object array slot: a level change that unloads the class prepares again
    ue::UObject* statics_ = nullptr;    // GameplayStatics CDO
    ue::PreparedCall allActors_;        // GetAllActorsOfClass, its output array reused (released by the engine)
    // Pieces seen on the last pass. Rebuilt every pass from the fighters' own Worn Armor arrays, so a destroyed piece
    // is dropped without ever being read.
    std::unordered_map<ue::UObject*, Piece> pieces_;
    std::unordered_map<ue::UObject*, bool> farFighters_;   // fighter -> its armour's shadows are off
    std::unordered_map<ue::UObject*, Body> bodies_;        // fighter -> what was changed on its own meshes
    std::string rtParam_;                 // SetVisibleInRayTracing's bool parameter ("" = not in this build)
    bool tickOptOk_ = false;              // VisibilityBasedAnimTickOption is the 1-byte field this mod expects
    bool followersOk_ = false;            // the head / skeleton fields and AnimClass were found
    bool followerLogged_[2] = {};
    ArmorCounts counts_;
    int failures_ = 0;
    int passes_ = 0;
    bool loggedBody_ = false;
    bool paintTouched_ = false;   // this mod turned vertex paint off on some fighter (so turning it on again undoes that)
    ue::UObject* kismet_ = nullptr;
};

}  // namespace hsp
