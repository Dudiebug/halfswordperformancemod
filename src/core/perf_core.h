// Game-free logic (unit-tested in tests/test_core.cpp): settings, frame statistics, shadow distance decisions.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace hsp::core {

// HSPerf.ini files written before this version keep their own value for every key except the ones whose default
// changed with it (armor_bounds, shadow_trim and skip_empty_gamemode_tick in 2): those take the new default.
constexpr int kSettingsVersion = 2;

struct Settings {
    bool enabled = true;
    bool armorLeaderPose = true;     // armour pieces follow the body's bones instead of copying them with an anim blueprint
    bool armorBounds = true;         // leader-pose armour drops the game's 10x bounds
    bool shadowTrim = true;          // armour on fighters farther than shadowTrimMeters casts no shadow
    double shadowTrimMeters = 15.0;
    bool vertexPaint = true;         // false: the game's own "Force Disable Vertex Paint" on every fighter (no blood/dents)
    bool idleWeapons = true;         // weapons asleep on the floor with nothing pending stop ticking until they wake
    bool skipEmptyGameModeTick = true;    // skip the game mode's empty Blueprint tick body (saves about a microsecond)
    bool corpseSleep = true;         // dead fighters lying still stop ticking until something moves them
    bool bodyBounds = true;          // the body drops the game's 1000x bounds
    bool headLeaderPose = true;      // head and skeleton meshes follow the body's bones
    bool armorRayTracing = false;    // false: armour is left out of hardware ray tracing (Lumen reflections, GI)
    int overlayKey = 0x79;           // virtual key (VK_F10): stat fps / stat unit on and off
    int menuKey = 0x78;              // virtual key (VK_F9): the settings menu
    int logSeconds = 10;             // a perf line every N seconds (0 = never)
    int forceUnknownBuild = 0;
};

inline std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "F1".."F12", "0x7A", or a decimal virtual-key code; 0 = none. Anything else: fallback.
inline int ParseKey(const std::string& v, int fallback) {
    const std::string s = Lower(Trim(v));
    if (s.empty() || s == "none" || s == "0") return 0;
    if (s.size() >= 2 && s[0] == 'f' && std::all_of(s.begin() + 1, s.end(), ::isdigit)) {
        const int n = std::atoi(s.c_str() + 1);
        return n >= 1 && n <= 24 ? 0x70 + n - 1 : fallback;
    }
    char* end = nullptr;
    const long k = std::strtol(s.c_str(), &end, 0);
    return end && *end == 0 && k > 0 && k < 256 ? int(k) : fallback;
}

inline bool ParseBool(const std::string& v, bool fallback) {
    const std::string s = Lower(Trim(v));
    if (s == "1" || s == "true" || s == "on" || s == "yes") return true;
    if (s == "0" || s == "false" || s == "off" || s == "no") return false;
    return fallback;
}

// INI text ([perf] section; keys outside any section are read too). Unknown keys and bad values keep the default.
inline Settings ParseSettings(const std::string& text) {
    Settings s;
    const Settings defaults;
    int version = 1;
    std::string section;
    size_t at = 0;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = Trim(text.substr(at, nl - at));
        at = nl + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = Lower(Trim(line.substr(1, line.size() - 2))); continue; }
        if (!section.empty() && section != "perf") continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = Lower(Trim(line.substr(0, eq)));
        std::string v = Trim(line.substr(eq + 1));
        if (const size_t c = v.find_first_of(";#"); c != std::string::npos) v = Trim(v.substr(0, c));
        if (k == "settings_version") version = std::atoi(v.c_str());
        else if (k == "enabled") s.enabled = ParseBool(v, s.enabled);
        else if (k == "armor_leader_pose") s.armorLeaderPose = ParseBool(v, s.armorLeaderPose);
        else if (k == "armor_bounds") s.armorBounds = ParseBool(v, s.armorBounds);
        else if (k == "shadow_trim") s.shadowTrim = ParseBool(v, s.shadowTrim);
        else if (k == "shadow_trim_meters") { const double m = std::atof(v.c_str()); if (m >= 1 && m <= 500) s.shadowTrimMeters = m; }
        else if (k == "vertex_paint") s.vertexPaint = ParseBool(v, s.vertexPaint);
        else if (k == "idle_weapons") s.idleWeapons = ParseBool(v, s.idleWeapons);
        else if (k == "skip_empty_gamemode_tick") s.skipEmptyGameModeTick = ParseBool(v, s.skipEmptyGameModeTick);
        else if (k == "corpse_sleep") s.corpseSleep = ParseBool(v, s.corpseSleep);
        else if (k == "body_bounds") s.bodyBounds = ParseBool(v, s.bodyBounds);
        else if (k == "head_leader_pose") s.headLeaderPose = ParseBool(v, s.headLeaderPose);
        else if (k == "armor_ray_tracing") s.armorRayTracing = ParseBool(v, s.armorRayTracing);
        else if (k == "overlay_key") s.overlayKey = ParseKey(v, s.overlayKey);
        else if (k == "menu_key") s.menuKey = ParseKey(v, s.menuKey);
        else if (k == "log_seconds") { const int n = std::atoi(v.c_str()); if (n >= 0 && n <= 3600) s.logSeconds = n; }
        else if (k == "force_unknown_build") { const int n = std::atoi(v.c_str()); if (n >= 0 && n <= 2) s.forceUnknownBuild = n; }
    }
    if (version < 2) {
        s.armorBounds = defaults.armorBounds;
        s.shadowTrim = defaults.shadowTrim;
        s.skipEmptyGameModeTick = defaults.skipEmptyGameModeTick;
    }
    // Bounds from the body only exist for a piece that follows the body's pose.
    if (!s.armorLeaderPose) s.armorBounds = false;
    return s;
}

// A virtual key as ParseKey reads it back: "F1".."F24", "none", or "0x7A".
inline std::string KeyName(int vk) {
    char buf[8];
    if (vk == 0) return "none";
    if (vk >= 0x70 && vk <= 0x87) std::snprintf(buf, sizeof buf, "F%d", vk - 0x70 + 1);
    else std::snprintf(buf, sizeof buf, "0x%02X", vk & 0xFF);
    return buf;
}

// HSPerf.ini with these values (the file the mod writes the first time, and again when the menu changes a setting).
inline std::string SettingsIni(const Settings& s) {
    auto b = [](bool v) { return v ? "1" : "0"; };
    char meters[16];
    std::snprintf(meters, sizeof meters, "%g", s.shadowTrimMeters);
    std::string t;
    t += "; Half Sword Performance Mod settings. The F9 menu changes them in game and writes this file again (comments\n";
    t += "; you add are not kept). enabled, the keys and log_seconds take effect on the next start.\n";
    t += "[perf]\n";
    t += "; Which defaults this file was written with (leave it as it is).\n";
    t += "settings_version=" + std::to_string(kSettingsVersion) + "\n";
    t += "; 0 turns the whole mod off.\n";
    t += std::string("enabled=") + b(s.enabled) + "\n";
    t += "; Armour follows the body's bones directly instead of running an animation per piece.\n";
    t += std::string("armor_leader_pose=") + b(s.armorLeaderPose) + "\n";
    t += "; Armour drops the 10x bounds the game gives it, so armour (and its shadow) off screen can be skipped.\n";
    t += std::string("armor_bounds=") + b(s.armorBounds) + "\n";
    t += "; Armour on fighters farther than shadow_trim_meters casts no shadow.\n";
    t += std::string("shadow_trim=") + b(s.shadowTrim) + "\n";
    t += std::string("shadow_trim_meters=") + meters + "\n";
    t += "; 0 turns off blood and dents painted on bodies and armour (the game's own switch).\n";
    t += std::string("vertex_paint=") + b(s.vertexPaint) + "\n";
    t += "; Weapons lying still on the floor stop ticking until something moves or picks them up.\n";
    t += std::string("idle_weapons=") + b(s.idleWeapons) + "\n";
    t += "; Skips the game mode's empty per-frame Blueprint (saves about a microsecond a frame).\n";
    t += std::string("skip_empty_gamemode_tick=") + b(s.skipEmptyGameModeTick) + "\n";
    t += "; Dead fighters lying still stop ticking until something moves them.\n";
    t += std::string("corpse_sleep=") + b(s.corpseSleep) + "\n";
    t += "; The body drops the 1000x bounds the game gives it (its animation then runs even off screen).\n";
    t += std::string("body_bounds=") + b(s.bodyBounds) + "\n";
    t += "; The head and skeleton meshes follow the body's bones instead of copying them.\n";
    t += std::string("head_leader_pose=") + b(s.headLeaderPose) + "\n";
    t += "; 0 leaves armour out of ray traced reflections and lighting (saves GPU time; armour then shows in no reflection);\n";
    t += "; 1 keeps it in, as the game ships.\n";
    t += std::string("armor_ray_tracing=") + b(s.armorRayTracing) + "\n";
    t += "; Opens the settings menu.\n";
    t += "menu_key=" + KeyName(s.menuKey) + "\n";
    t += "; Shows the frame time counters. Ctrl + this key turns the armour and weapon fixes off and on, to compare;\n";
    t += "; Ctrl + Shift + this key records a 30 s profile.\n";
    t += "overlay_key=" + KeyName(s.overlayKey) + "\n";
    t += "; A performance line in HSPerf.log every N seconds (0 = never).\n";
    t += "log_seconds=" + std::to_string(s.logSeconds) + "\n";
    if (s.forceUnknownBuild) t += "force_unknown_build=" + std::to_string(s.forceUnknownBuild) + "\n";
    return t;
}

// The menu's shadow distance steps, in metres: the next one up (dir > 0) or down, wrapping around.
inline double NextTrimMeters(double now, int dir) {
    constexpr double kSteps[] = {5, 10, 15, 20, 30, 50, 100};
    constexpr int n = int(sizeof kSteps / sizeof kSteps[0]);
    int at = 0;   // the step nearest to `now`
    for (int i = 1; i < n; ++i) if (std::abs(kSteps[i] - now) < std::abs(kSteps[at] - now)) at = i;
    if (kSteps[at] != now) return kSteps[at];   // a value set by hand: snap to a step first
    return kSteps[((at + (dir > 0 ? 1 : -1)) % n + n) % n];
}

// Frame times between two game-mode ticks.
class FrameStats {
public:
    void Add(double ms) { if (ms > 0 && ms < 10000) ms_.push_back(ms); }
    size_t Count() const { return ms_.size(); }
    double Average() const {
        if (ms_.empty()) return 0;
        double sum = 0;
        for (double m : ms_) sum += m;
        return sum / double(ms_.size());
    }
    // The frame time 99% of frames stayed under (nearest rank).
    double P99() const {
        if (ms_.empty()) return 0;
        std::vector<double> v = ms_;
        const size_t k = std::min(v.size() - 1, size_t(double(v.size()) * 0.99));
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
        return v[k];
    }
    double Max() const { return ms_.empty() ? 0 : *std::max_element(ms_.begin(), ms_.end()); }
    void Reset() { ms_.clear(); }
private:
    std::vector<double> ms_;
};

// Whether a fighter's armour should cast shadows, with 2 m of hysteresis so a fighter at the edge does not flicker.
inline bool ShadowWanted(bool castsNow, double distanceMeters, double trimMeters) {
    constexpr double kHysteresis = 2.0;
    return castsNow ? distanceMeters <= trimMeters + kHysteresis : distanceMeters < trimMeters;
}

}  // namespace hsp::core
