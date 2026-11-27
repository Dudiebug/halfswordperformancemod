#include "bench.h"
#include "log.h"
#include "physics.h"
#include <cstdio>

namespace hsp {
using namespace ue;

namespace {
constexpr int kRounds = 2;
constexpr double kWindowMs = 3000, kSettleMs = 500;

// Only settings that move work between threads or reorder it; none changes a result. Islands are solved one by one
// whichever worker takes them, animation update and evaluation give the same pose on a worker as on the game thread
// (the engine keeps an Anim Blueprint that is not thread safe on the game thread), and priorities only reorder tasks.
// The mod sets most of these by default (physics_islands, thread_scheduling): one already in use is measured the other
// way round, against the game's own value.
struct Candidate { const char* name; const char* cvars[2]; int value; };
constexpr Candidate kCandidates[] = {
    {"physics islands from 8 bodies per worker", {"p.Chaos.Solver.IslandGroups.MinBodiesPerWorker", "p.Chaos.Solver.IslandGroups.MinConstraintsPerWorker"}, 8},
    {"physics islands from 16 bodies per worker", {"p.Chaos.Solver.IslandGroups.MinBodiesPerWorker", "p.Chaos.Solver.IslandGroups.MinConstraintsPerWorker"}, 16},
    {"two physics island groups per worker", {"p.Chaos.Solver.IslandGroups.WorkerMultiplier", nullptr}, 2},
    {"task priority raised per task (dynamic prioritization)", {"TaskGraph.UseDynamicPrioritization", nullptr}, 1},
    {"animation update on worker threads", {"a.ParallelAnimUpdate", nullptr}, 1},
    {"animation evaluation on worker threads", {"a.ParallelAnimEvaluation", nullptr}, 1},
    {"animation interpolation on worker threads", {"a.ParallelAnimInterpolation", nullptr}, 1},
    {"physics blending on worker threads", {"a.ParallelBlendPhysics", nullptr}, 1},
    {"skinned meshes tick at high priority", {"tick.HiPriSkinnedMeshes", nullptr}, 1},
};
}  // namespace

void Bench::Apply(UObject* gameMode, Console console, const Variant* v) {
    for (const Variant& x : variants_)
        for (const Setting& s : x.set) {
            const Setting* want = nullptr;
            if (v) for (const Setting& t : v->set) if (t.cvar == s.cvar) want = &t;
            char cmd[160];
            std::snprintf(cmd, sizeof cmd, "%s %d", s.cvar.c_str(), want ? want->value : s.original);
            console(gameMode, cmd);
        }
}

void Bench::StartWindow(double nowMs) {
    windowStart_ = nowMs;
    sum_ = 0;
    frames_ = 0;
    spoiled_ = false;
}

std::string Bench::Toggle(UObject* gameMode, Console console) {
    if (running_) return Finish(gameMode, console, "stopped early");
    variants_.clear();
    std::string skipped;
    for (const Candidate& c : kCandidates) {
        Variant v;
        v.name = c.name;
        bool same = true, ok = true;
        for (const char* cv : c.cvars) {
            if (!cv) continue;
            Setting s;
            s.cvar = cv;
            s.value = c.value;
            if (!ReadConsoleInt(cv, s.original)) { ok = false; break; }
            same = same && s.original == s.value;
            v.set.push_back(s);
        }
        if (!ok) { skipped += std::string(skipped.empty() ? "" : ", ") + c.name + " (not readable)"; continue; }
        if (same) {   // already in use: try the game's own value instead
            bool back = true;
            for (Setting& s : v.set) back = GameCvarValue(s.cvar, s.value) && s.value != s.original && back;
            if (back) {
                v.name += " (in use; measured: the game's own " + std::to_string(v.set[0].value) + ")";
                same = false;
            }
        }
        if (same) { skipped += std::string(skipped.empty() ? "" : ", ") + c.name + " (already so)"; continue; }
        variants_.push_back(v);
    }
    std::string list;
    for (const Variant& v : variants_)
        for (const Setting& s : v.set) list += (list.empty() ? "" : ", ") + s.cvar + " " + std::to_string(s.original) + "->" + std::to_string(s.value);
    if (variants_.empty()) return "bench: nothing to try (" + skipped + ")";
    running_ = true;
    step_ = 0;
    windowStart_ = 0;   // the first frame starts the first window
    char b[160];
    std::snprintf(b, sizeof b, "bench: started, %zu settings, about %.0f s of fighting with 2 or more fighters (Ctrl+Alt+F9 stops); ",
                  variants_.size(), variants_.size() * 2 * kRounds * kWindowMs / 1000);
    return b + list + (skipped.empty() ? "" : "; skipped: " + skipped);
}

void Bench::Frame(UObject* gameMode, Console console, double nowMs, double frameMs, int fighters) {
    if (!running_) return;
    const size_t n = variants_.size();
    const size_t round = step_ / (2 * n), v = (step_ / 2) % n, half = step_ % 2;
    const bool variantFirst = round % 2 == 1;
    const bool isVariant = (half == 0) == variantFirst;
    if (windowStart_ == 0) {
        Apply(gameMode, console, isVariant ? &variants_[v] : nullptr);
        StartWindow(nowMs);
        return;
    }
    if (frameMs > 1000 || fighters < 2) spoiled_ = true;
    if (nowMs - windowStart_ >= kSettleMs) { sum_ += frameMs; ++frames_; }
    if (nowMs - windowStart_ < kWindowMs) return;
    if (spoiled_ || frames_ < 10) {
        if (half == 1) --step_;   // run the pair again from its first half
        windowStart_ = 0;
        return;
    }
    const double avg = sum_ / frames_;
    if (half == 0) {
        (isVariant ? pendingVar_ : pendingBase_) = avg;
    } else {
        Variant& x = variants_[v];
        x.ms[round] = isVariant ? avg : pendingVar_;
        x.baseMs[round] = isVariant ? pendingBase_ : avg;
        x.done[round] = true;
    }
    ++step_;
    windowStart_ = 0;
    if (step_ >= kRounds * 2 * n) {
        const std::string msg = Finish(gameMode, console, "done");
        HSP_LOG("%s", msg.c_str());
    }
}

std::string Bench::Finish(UObject* gameMode, Console console, const char* why) {
    Apply(gameMode, console, nullptr);
    running_ = false;
    HSP_LOG("bench: %s; every setting put back as it was", why);
    std::string out = "bench: results (lower ms is faster; each against the settings in use in the windows next to it)";
    for (const Variant& v : variants_) {
        double ms = 0, base = 0;
        int k = 0;
        for (int r = 0; r < kRounds; ++r)
            if (v.done[r]) { ms += v.ms[r]; base += v.baseMs[r]; ++k; }
        char b[256];
        if (!k) std::snprintf(b, sizeof b, "\n  %s: not measured", v.name.c_str());
        else std::snprintf(b, sizeof b, "\n  %s: %.2f ms vs %.2f ms (%+.1f%%, %d of %d pairs)", v.name.c_str(), ms / k, base / k,
                           100.0 * (ms - base) / base, k, kRounds);
        out += b;
    }
    variants_.clear();
    return out;
}

}  // namespace hsp
