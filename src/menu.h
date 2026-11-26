// The settings menu (F9): a panel the mod builds at runtime out of the engine's own UMG widgets (no assets of its own,
// no code patched), on top of the game's HUD (UI_HUD) while a fight is on.
//
// Each row is a UMG Button. A click is the left mouse button going down while a row's button is hovered (polled every
// frame, the button's own OnClicked is never bound); the arrow keys and Enter work too. While the menu is open the
// game gets no keys or mouse (UI-only input mode) and the cursor shows; closing gives both back. If the cursor was
// already up (the game's own pause menu), the input mode is left alone.
//
// Every change applies at once and is written to HSPerf.ini. The game keeps running behind the menu.
#pragma once
#include "core/perf_core.h"
#include "ue/engine.h"
#include <string>

namespace hsp {

// What the menu shows besides the settings, filled by the tick every frame the menu is open.
struct MenuInfo {
    bool overlay = false;      // the F10 counters
    bool fixesActive = true;   // the A/B switch (Ctrl + F10)
    bool recording = false;    // a profile capture is running
    std::string stats;         // one line: frame times and counts
};

enum class MenuAction { None, SettingsChanged, ToggleOverlay, ToggleFixes, ToggleProfile };

class Menu {
public:
    // Game thread, every game mode tick. Opens and closes on the menu key, handles the rows, and returns what the
    // caller has to do (settings are changed in place in `s` and already saved when it returns SettingsChanged).
    MenuAction Tick(ue::UObject* gameMode, core::Settings& s, const MenuInfo& info);
    bool IsOpen() const { return open_; }
    void Status(const std::string& line);   // the line under the rows (what the last action did)

private:
    enum Row { kLeader, kBounds, kShadow, kShadowMeters, kArmorRt, kPaint, kBlood, kWeapons, kCorpses, kBodyBounds, kHead, kSkipTick, kIslands, kOverlay,
               kFixes, kProfile, kClose, kRows };
    struct Widgets {
        ue::UObject* root = nullptr;        // the Border on the HUD's canvas
        ue::UObject* button[kRows] = {};
        ue::UObject* value[kRows] = {};
        ue::UObject* stats = nullptr;
        ue::UObject* status = nullptr;
        bool ready = false;                 // every widget was made
    };
    bool Attach(ue::UObject* gameMode);     // finds the HUD on screen, builds the panel on it when it is a new one
    bool Build();
    void Open(ue::UObject* gameMode);
    void Close(ue::UObject* gameMode);
    void Refresh(const core::Settings& s, const MenuInfo& info, bool statsDue);
    MenuAction Change(int row, int dir, core::Settings& s);
    ue::UObject* PlayerController(ue::UObject* gameMode);

    bool open_ = false;
    bool built_ = false;
    bool cursorWasUp_ = false;       // the game had the cursor up when the menu opened: its input mode is left alone
    uint8_t hudVis_ = 0, canvasVis_ = 0;
    ue::UObject* hud_ = nullptr;     // the UI_HUD the panel sits on
    int32_t hudIndex_ = -1;
    ue::UObject* canvas_ = nullptr;
    Widgets w_;
    int selected_ = 0;
    bool keys_[8] = {};              // last state of the polled keys (see Tick)
    double nextInfoMs_ = 0;
    std::string status_, shown_[kRows], shownStats_, shownStatus_;
    int shownSelected_ = -1;
    bool noHudLogged_ = false;
};

}  // namespace hsp
