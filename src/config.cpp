#include "config.h"
#include "log.h"
#include "paths.h"
#include <cstdio>
#include <string>

namespace hsp {
namespace {
constexpr const char* kDefaultIni =
    "; Half Sword Performance Mod settings. Restart the game after a change.\n"
    "[perf]\n"
    "; 0 turns the whole mod off.\n"
    "enabled=1\n"
    "; Armour follows the body's bones directly instead of running an animation per piece.\n"
    "armor_leader_pose=1\n"
    "; Armour drops the 10x bounds the game gives it, so armour (and its shadow) off screen can be skipped. Being checked.\n"
    "armor_bounds=0\n"
    "; Armour on fighters farther than shadow_trim_meters casts no shadow.\n"
    "shadow_trim=0\n"
    "shadow_trim_meters=15\n"
    "; 0 turns off blood and dents painted on bodies and armour (the game's own switch).\n"
    "vertex_paint=1\n"
    "; Weapons lying still on the floor stop ticking until something moves or picks them up.\n"
    "idle_weapons=1\n"
    "; Skips the game mode's empty per-frame Blueprint (saves about a microsecond a frame).\n"
    "skip_empty_gamemode_tick=0\n"
    "; Shows the frame time counters. Ctrl + this key turns the armour and weapon fixes off and on, to compare.\n"
    "overlay_key=F10\n"
    "; A performance line in HSPerf.log every N seconds (0 = never).\n"
    "log_seconds=10\n";

std::string ReadAll(const std::wstring& path) {
    std::string out;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}
}  // namespace

core::Settings LoadSettings() {
    const std::wstring path = ModFilePath(L"HSPerf.ini");
    std::string text = ReadAll(path);
    if (text.empty()) {
        if (FILE* f = _wfopen(path.c_str(), L"wb")) { std::fputs(kDefaultIni, f); std::fclose(f); }
        text = kDefaultIni;
    }
    const core::Settings s = core::ParseSettings(text);
    HSP_LOG("config: enabled %d, armor_leader_pose %d, armor_bounds %d, shadow_trim %d (%.0f m), vertex_paint %d, idle_weapons %d, skip_empty_gamemode_tick %d, overlay_key 0x%02X, log_seconds %d",
            s.enabled, s.armorLeaderPose, s.armorBounds, s.shadowTrim, s.shadowTrimMeters, s.vertexPaint, s.idleWeapons, s.skipEmptyGameModeTick, s.overlayKey, s.logSeconds);
    return s;
}

}  // namespace hsp
