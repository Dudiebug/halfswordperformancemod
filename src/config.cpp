#include "config.h"
#include "log.h"
#include "paths.h"
#include <cstdio>
#include <string>

namespace hsp {
namespace {
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

bool WriteAll(const std::wstring& path, const std::string& text) {
    // Written beside the file and moved over it, so a crash mid-write never leaves half a settings file.
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) { _wremove(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
}  // namespace

core::Settings LoadSettings() {
    const std::wstring path = ModFilePath(L"HSPerf.ini");
    std::string text = ReadAll(path);
    if (text.empty()) {
        text = core::SettingsIni(core::Settings{});
        WriteAll(path, text);
    }
    const core::Settings s = core::ParseSettings(text);
    HSP_LOG("config: enabled %d, armor_leader_pose %d, armor_bounds %d, shadow_trim %d (%.0f m), vertex_paint %d, idle_weapons %d, skip_empty_gamemode_tick %d, menu_key %s, overlay_key %s, log_seconds %d",
            s.enabled, s.armorLeaderPose, s.armorBounds, s.shadowTrim, s.shadowTrimMeters, s.vertexPaint, s.idleWeapons, s.skipEmptyGameModeTick,
            core::KeyName(s.menuKey).c_str(), core::KeyName(s.overlayKey).c_str(), s.logSeconds);
    return s;
}

bool SaveSettings(const core::Settings& s) {
    const bool ok = WriteAll(ModFilePath(L"HSPerf.ini"), core::SettingsIni(s));
    if (!ok) HSP_LOG("config: could not write HSPerf.ini (error %lu)", GetLastError());
    return ok;
}

}  // namespace hsp
