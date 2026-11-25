// Game-free logic (unit-tested in tests/test_core.cpp): settings, frame statistics, shadow distance decisions.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace hsp::core {

struct Settings {
    bool enabled = true;
    bool armorLeaderPose = true;     // armour pieces follow the body's bones instead of copying them with an anim blueprint
    bool armorBounds = true;         // armour pieces use the body's bounds (off-screen armour and its shadows get culled)
    bool shadowTrim = false;         // armour on fighters farther than shadowTrimMeters casts no shadow
    double shadowTrimMeters = 15.0;
    bool vertexPaint = true;         // false: the game's own "Force Disable Vertex Paint" on every fighter (no blood/dents)
    int overlayKey = 0x79;           // virtual key (VK_F10): stat fps / stat unit on and off
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
        if (k == "enabled") s.enabled = ParseBool(v, s.enabled);
        else if (k == "armor_leader_pose") s.armorLeaderPose = ParseBool(v, s.armorLeaderPose);
        else if (k == "armor_bounds") s.armorBounds = ParseBool(v, s.armorBounds);
        else if (k == "shadow_trim") s.shadowTrim = ParseBool(v, s.shadowTrim);
        else if (k == "shadow_trim_meters") { const double m = std::atof(v.c_str()); if (m >= 1 && m <= 500) s.shadowTrimMeters = m; }
        else if (k == "vertex_paint") s.vertexPaint = ParseBool(v, s.vertexPaint);
        else if (k == "overlay_key") s.overlayKey = ParseKey(v, s.overlayKey);
        else if (k == "log_seconds") { const int n = std::atoi(v.c_str()); if (n >= 0 && n <= 3600) s.logSeconds = n; }
        else if (k == "force_unknown_build") { const int n = std::atoi(v.c_str()); if (n >= 0 && n <= 2) s.forceUnknownBuild = n; }
    }
    // Bounds from the body only exist for a piece that follows the body's pose.
    if (!s.armorLeaderPose) s.armorBounds = false;
    return s;
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
