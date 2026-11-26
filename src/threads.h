// Where the frame's CPU time goes, per engine thread, for the perf line.
//
// Each perf line compares every thread's CPU time (GetThreadTimes) with the last line's: the game thread (the one the
// tick runs on), RenderThread, RHIThread and everything else. Threads are named by the engine (GetThreadDescription,
// which Proton has too). When even the busiest of the three was busy for less than 85% of the time, the frame is
// waiting on something else: the GPU or a frame cap.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace hsp {

class ThreadTimes {
public:
    void SetGameThread(uint32_t tid) { game_ = tid; }
    // " | cpu ms a frame: game 3.1, render 2.0, rhi 0.8, other 4.2 (most: Foreground Worker #1 0.9) ..." or "" the
    // first time (nothing to compare with yet). frameMs: the average frame time since the last call.
    std::string Line(double frameMs);

private:
    struct Thread { uint64_t cpu = 0; std::string name; bool named = false; };
    std::unordered_map<uint32_t, Thread> last_;
    uint64_t lastWall_ = 0;   // 100 ns units
    uint32_t game_ = 0;
};

}  // namespace hsp
