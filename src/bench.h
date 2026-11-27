// Scheduling benchmark (Ctrl + Alt + the menu key): tries engine settings that only change which threads run the work
// and in what order, never what is computed, against the settings in use, during a real fight.
//
// Each setting gets 3 s windows that alternate with 3 s windows of the settings in use (the first 0.5 s of a window is
// not counted, while the change settles), twice over in opposite order, so a fight getting busier or calmer weighs on
// both sides alike. Windows with fewer than 2 fighters, or with a level load in them, are thrown away and run again;
// the bench simply waits for the next fight. At the end (or when the keys are pressed again) every setting is put back
// as it was and each one's average frame time is logged next to the paired average of the settings in use.
#pragma once
#include "ue/engine.h"
#include <string>
#include <vector>

namespace hsp {

class Bench {
public:
    using Console = void (*)(ue::UObject* worldContext, const char* command);
    bool Running() const { return running_; }
    // Start or stop. Returns a line for the log.
    std::string Toggle(ue::UObject* gameMode, Console console);
    // Every frame while running: frameMs is this frame's time, fighters the live fighter count.
    void Frame(ue::UObject* gameMode, Console console, double nowMs, double frameMs, int fighters);

private:
    struct Setting { std::string cvar; int value = 0, original = 0; };
    struct Variant {
        std::string name;
        std::vector<Setting> set;
        double ms[2] = {}, baseMs[2] = {};   // per round: the variant's window and its paired window of the settings in use
        bool done[2] = {};
    };
    void Apply(ue::UObject* gameMode, Console console, const Variant* v);
    std::string Finish(ue::UObject* gameMode, Console console, const char* why);
    void StartWindow(double nowMs);

    bool running_ = false;
    std::vector<Variant> variants_;
    size_t step_ = 0;            // index into the schedule: (round, variant, which half)
    double windowStart_ = 0, sum_ = 0;
    int frames_ = 0;
    bool spoiled_ = false;
    double pendingBase_ = 0;     // the first half's average, when the settings in use ran first
    double pendingVar_ = 0;
};

}  // namespace hsp
