// The armour fixes, run on the game thread a few times a second.
//
// The game gives each worn armour piece its own SkeletalMeshComponent running ABP_CopyPose, an anim blueprint whose
// only node copies the body's pose bone by bone every frame, and scales the piece's bounds by 10 (Willie_BP
// "Set Up Armor"). A fully armoured fighter has 10-20+ such pieces. Leader pose (SetLeaderPoseComponent) makes a piece
// reuse the body's bone transforms with no anim instance update or evaluation, and bounds taken from the body let
// the renderer cull armour off screen. Both are undone by clearing the leader again (the piece's own anim instance
// is never removed), which the A/B key does live.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <cstdint>
#include <unordered_map>

namespace hsp {

struct ArmorCounts { int fighters = 0, pieces = 0, leader = 0, shadowOff = 0; };

class ArmorFixes {
public:
    explicit ArmorFixes(const core::Settings& s) : s_(s) {}
    // gameMode: the ticking BP_HalfSwordGameMode_C (world context). Cheap when there is nothing new.
    void Pass(ue::UObject* gameMode);
    void SetActive(bool on) { active_ = on; }   // false: every fix undone on the next pass (the A/B key)
    bool Active() const { return active_; }
    ArmorCounts Counts() const { return counts_; }

private:
    struct Piece { bool leader = false; bool shadowOff = false; };
    bool Prepare();
    bool CameraLocation(ue::UObject* gameMode, ue::FVector& out);
    void FixPiece(ue::UObject* piece, ue::UObject* body, Piece& rec, bool wantShadow);

    const core::Settings s_;
    bool active_ = true;
    bool prepared_ = false, unusable_ = false;
    ue::UClass* willie_ = nullptr;
    ue::UObject* statics_ = nullptr;    // GameplayStatics CDO
    ue::PreparedCall allActors_;        // GetAllActorsOfClass, its output array reused (released by the engine)
    // Pieces seen on the last pass. Rebuilt every pass from the fighters' own Worn Armor arrays, so a destroyed piece
    // is dropped without ever being read.
    std::unordered_map<ue::UObject*, Piece> pieces_;
    std::unordered_map<ue::UObject*, bool> farFighters_;   // fighter -> its armour's shadows are off
    ArmorCounts counts_;
    int failures_ = 0;
};

}  // namespace hsp
