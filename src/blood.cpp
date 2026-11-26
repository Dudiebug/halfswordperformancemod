#include "blood.h"
#include "log.h"
#include <cstring>

namespace hsp {
using namespace ue;

namespace {
// Kismet opcodes (UE 5.4 EExprToken).
constexpr uint8_t kLocalVariable = 0x00, kLetBool = 0x14, kSelf = 0x17, kFinalFunction = 0x1C, kIntConst = 0x1D, kFloatConst = 0x1E,
                  kIntZero = 0x25, kIntOne = 0x26, kTrue = 0x27, kFalse = 0x28, kIntConstByte = 0x2C, kStructConst = 0x2F,
                  kSkipOffsetConst = 0x5B, kCallMath = 0x68;
constexpr float kSnapshotSeconds = 0.25f;
constexpr int32_t kPassportWindow = 2000;   // bytes before the copy's Delay that hold the passport writes (about 900 here)

template <class T> T At(const uint8_t* b, int32_t i) { T v; std::memcpy(&v, b + i, sizeof v); return v; }

// KismetSystemLibrary.Delay(self, <float literal>, LatentActionInfo{<skip offset>, ...}) starting at i.
struct DelayCall { int32_t at = 0; float seconds = 0; uint32_t target = 0; };
bool ReadDelay(const uint8_t* b, int32_t n, int32_t i, const void* delay, const void* latentInfo, DelayCall& out) {
    if (i + 33 > n || (b[i] != kFinalFunction && b[i] != kCallMath) || At<const void*>(b, i + 1) != delay) return false;
    if (b[i + 9] != kSelf || b[i + 10] != kFloatConst || b[i + 15] != kStructConst || At<const void*>(b, i + 16) != latentInfo) return false;
    if (b[i + 28] != kSkipOffsetConst) return false;
    out = {i, At<float>(b, i + 11), At<uint32_t>(b, i + 29)};
    return true;
}

// `<DoOnce IsClosed temp> = false` at i: where a DoOnce gate opens again.
bool IsGateReopen(const uint8_t* b, int32_t n, uint32_t i) {
    if (int64_t(i) + 11 > n || b[i] != kLetBool || b[i + 1] != kLocalVariable || b[i + 10] != kFalse) return false;
    const FProperty* p = At<const FProperty*>(b, int32_t(i) + 2);
    return p && PropertyName(p).rfind("Temp_bool_IsClosed_Variable", 0) == 0;
}

bool Contains(const uint8_t* b, int32_t from, int32_t to, const void* ptr) {
    for (int32_t i = from < 0 ? 0 : from; i + 8 <= to; ++i)
        if (At<const void*>(b, i) == ptr) return true;
    return false;
}
}  // namespace

bool BloodFixes::Prepare() {
    if (prepared_ && (ObjectAt(willieIndex_) != reinterpret_cast<UObject*>(willie_) ||
                      ObjectAt(graphIndex_) != reinterpret_cast<UObject*>(graph_))) {
        prepared_ = scanned_ = false;   // a new class (the old one's bytecode, patches included, is gone)
        patches_.clear();
        counts_ = {};
    }
    if (prepared_) return graph_ != nullptr;
    willie_ = FindClass("Willie_BP_C");
    if (!willie_) return false;
    prepared_ = true;
    willieIndex_ = ObjectIndex(reinterpret_cast<UObject*>(willie_));
    graph_ = FindFunction(willie_, "ExecuteUbergraph_Willie_BP");
    if (graph_ && OuterOf(reinterpret_cast<UObject*>(graph_)) != reinterpret_cast<UObject*>(willie_)) graph_ = nullptr;
    graphIndex_ = graph_ ? ObjectIndex(reinterpret_cast<UObject*>(graph_)) : -1;
    if (!graph_ || !ScriptBytes(graph_, nullptr)) {
        HSP_LOG("blood: Willie_BP's event graph bytecode not found; cheaper blood off");
        graph_ = nullptr;
    }
    return graph_ != nullptr;
}

void BloodFixes::Apply() {
    scanned_ = true;
    int32_t n = 0;
    uint8_t* b = ScriptBytes(graph_, &n);
    if (!b) return;
    std::vector<Patch> found;

    // 1. The Delay(0) that reopens the passport copy's DoOnce, reached from the Delay(0) right after the copy.
    UObject* ksl = FindObject("KismetSystemLibrary", "Class");
    UFunction* delay = ksl ? FindFunction(reinterpret_cast<UClass*>(ksl), "Delay") : nullptr;
    UObject* latentInfo = FindStruct("LatentActionInfo");
    FProperty* passport = FindProperty(willie_, "Character Passport");
    int snapshotSites = 0;
    if (delay && latentInfo && passport) {
        std::vector<DelayCall> zero;
        for (int32_t i = 0; i < n; ++i) {
            DelayCall d;
            if (ReadDelay(b, n, i, delay, latentInfo, d) && d.seconds == 0.0f) zero.push_back(d);
        }
        for (const DelayCall& reopen : zero) {
            if (!IsGateReopen(b, n, reopen.target)) continue;
            for (const DelayCall& first : zero)
                if (first.target == uint32_t(reopen.at) && Contains(b, first.at - kPassportWindow, first.at, passport)) {
                    ++snapshotSites;
                    if (snapshotSites == 1) {
                        Patch p{reopen.at + 11, std::vector<uint8_t>(b + reopen.at + 11, b + reopen.at + 15), std::vector<uint8_t>(4)};
                        std::memcpy(p.now.data(), &kSnapshotSeconds, 4);
                        found.push_back(p);
                    }
                }
        }
    }
    if (snapshotSites != 1) {
        HSP_LOG("blood: passport copy delay %s; snapshot throttle off", snapshotSites ? "found more than once" : "not found");
        found.clear();
    }

    // 2. RVPDPClosestVertexDataOptimizationSettings{<face index>, false, <range>}: the early-out flag.
    UObject* opt = FindStruct("RVPDPClosestVertexDataOptimizationSettings");
    int footSites = 0;
    for (int32_t i = 0; opt && i + 15 <= n; ++i) {
        if (b[i] != kStructConst || At<const void*>(b, i + 1) != opt) continue;
        int32_t j = i + 13;   // past the struct pointer and its size
        if (b[j] == kIntConst) j += 5;
        else if (b[j] == kIntConstByte) j += 2;
        else if (b[j] == kIntZero || b[j] == kIntOne) j += 1;
        else continue;
        if (j >= n || b[j] != kFalse) continue;
        found.push_back({j, {kFalse}, {kTrue}});
        ++footSites;
    }
    if (!footSites) HSP_LOG("blood: footstep floor lookup settings not found; footstep early-out off");

    for (Patch& p : found) {
        std::memcpy(b + p.at, p.now.data(), p.now.size());
        patches_.push_back(p);
    }
    counts_.snapshot = snapshotSites == 1;
    counts_.footsteps = footSites > 0;
    if (!patches_.empty())
        HSP_LOG("blood: cheaper blood on (passport copy every %.2f s instead of every frame: %s; footstep floor lookups with the 5 cm "
                "early-out: %d)", kSnapshotSeconds, counts_.snapshot ? "yes" : "no", footSites);
}

bool BloodFixes::Intact() const {
    int32_t n = 0;
    const uint8_t* b = ScriptBytes(graph_, &n);
    if (!b) return false;
    for (const Patch& p : patches_)
        if (p.at + int32_t(p.now.size()) > n || std::memcmp(b + p.at, p.now.data(), p.now.size()) != 0) return false;
    return true;
}

void BloodFixes::Restore() {
    if (!patches_.empty() && graph_ && ObjectAt(graphIndex_) == reinterpret_cast<UObject*>(graph_) && Intact()) {
        uint8_t* b = ScriptBytes(graph_, nullptr);
        for (const Patch& p : patches_) std::memcpy(b + p.at, p.was.data(), p.was.size());
        HSP_LOG("blood: cheaper blood off (the game's own bytecode again)");
    }
    patches_.clear();
    scanned_ = false;
    counts_ = {};
}

void BloodFixes::SetActive(bool on) {
    active_ = on;
    if (!on) Restore();
}

void BloodFixes::Pass() {
    if (!s_.cheaperBlood || !active_) { if (scanned_) Restore(); return; }
    if (!Prepare()) return;
    if (scanned_ && !patches_.empty() && !Intact()) {   // the bytecode changed under the patches (another mod): left alone
        HSP_LOG("blood: Willie_BP's bytecode changed after it was patched; cheaper blood leaves it alone");
        patches_.clear();
        counts_ = {};
    }
    if (!scanned_) Apply();
}

}  // namespace hsp
