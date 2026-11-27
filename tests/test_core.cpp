// Unit tests for src/core/perf_core.h. Run: tests/run_tests.sh
#include "core/perf_core.h"
#include <cmath>
#include <cstdio>

using namespace hsp::core;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void TestDefaults() {
    const Settings s = ParseSettings("");
    CHECK(s.enabled && s.armorLeaderPose && s.armorBounds && s.shadowTrim && s.vertexPaint);
    CHECK(s.idleWeapons && s.skipEmptyGameModeTick && s.cheaperBlood);
    CHECK(s.corpseSleep && s.bodyBounds && s.headLeaderPose && !s.armorRayTracing);
    CHECK(s.physicsIslands && s.islandMinPerWorker == 8 && s.threadScheduling);
    CHECK(s.overlayKey == 0x79 && s.logSeconds == 10 && s.shadowTrimMeters == 15.0);
}

// A 0.2.0 file (no settings_version) says armor_bounds=0, shadow_trim=0 and skip_empty_gamemode_tick=0 because those
// were the defaults then: the new defaults win. A file written since keeps what it says.
static void TestSettingsVersion() {
    const Settings old = ParseSettings("[perf]\narmor_bounds=0\nshadow_trim=0\nskip_empty_gamemode_tick=0\nidle_weapons=0\n");
    CHECK(old.armorBounds && old.shadowTrim && old.skipEmptyGameModeTick && !old.idleWeapons);
    const Settings now = ParseSettings("[perf]\nsettings_version=2\narmor_bounds=0\nshadow_trim=0\nskip_empty_gamemode_tick=0\n");
    CHECK(!now.armorBounds && !now.shadowTrim && !now.skipEmptyGameModeTick);
    CHECK(SettingsIni(Settings{}).find("settings_version=3\n") != std::string::npos);
    // 16 was the default before 3: an older file with it takes the new 8; another value, or a version 3 file, keeps it.
    CHECK(ParseSettings("settings_version=2\nisland_min_per_worker=16\n").islandMinPerWorker == 8);
    CHECK(ParseSettings("settings_version=2\nisland_min_per_worker=24\n").islandMinPerWorker == 24);
    CHECK(ParseSettings("settings_version=3\nisland_min_per_worker=16\n").islandMinPerWorker == 16);
}

static void TestParse() {
    const Settings s = ParseSettings(
        "; comment\n[other]\nenabled=0\n[Perf]\r\nsettings_version=2\nshadow_trim = off ; trailing\nshadow_trim_meters=25\n"
        "vertex_paint=false\noverlay_key=F9\nlog_seconds=30\narmor_bounds=1\nforce_unknown_build=2\nbogus=1\nidle_weapons=0\nskip_empty_gamemode_tick=0\n"
        "corpse_sleep=0\nbody_bounds=0\nhead_leader_pose=no\narmor_ray_tracing=on\ncheaper_blood=off\n"
        "physics_islands=0\nisland_min_per_worker=12\nmarker_spheres=off\nthread_scheduling=0\n");
    CHECK(!s.physicsIslands && s.islandMinPerWorker == 12 && !s.threadScheduling);   // a 0.5.0 file's marker_spheres line is ignored
    CHECK(!s.cheaperBlood);
    CHECK(!s.corpseSleep && !s.bodyBounds && !s.headLeaderPose && s.armorRayTracing);
    CHECK(!s.idleWeapons && !s.skipEmptyGameModeTick);
    CHECK(s.enabled);               // [other] is not read
    CHECK(!s.shadowTrim && s.shadowTrimMeters == 25.0);
    CHECK(!s.vertexPaint && s.overlayKey == 0x78 && s.logSeconds == 30 && s.armorBounds && s.forceUnknownBuild == 2);
}

static void TestBadValues() {
    CHECK(ParseSettings("island_min_per_worker=0").islandMinPerWorker == 8);
    const Settings s = ParseSettings("shadow_trim_meters=0\nlog_seconds=-1\noverlay_key=banana\nenabled=maybe\nforce_unknown_build=9\n");
    CHECK(s.shadowTrimMeters == 15.0 && s.logSeconds == 10 && s.overlayKey == 0x79 && s.enabled && s.forceUnknownBuild == 0);
    CHECK(ParseSettings("overlay_key=none").overlayKey == 0);
    CHECK(ParseSettings("overlay_key=0x7A").overlayKey == 0x7A);
    CHECK(ParseSettings("overlay_key=F25").overlayKey == 0x79);
}

static void TestBoundsNeedLeaderPose() {
    const Settings s = ParseSettings("armor_leader_pose=0\narmor_bounds=1\n");
    CHECK(!s.armorLeaderPose && !s.armorBounds);
}

static bool Same(const Settings& a, const Settings& b) {
    return a.enabled == b.enabled && a.armorLeaderPose == b.armorLeaderPose && a.armorBounds == b.armorBounds && a.shadowTrim == b.shadowTrim &&
           a.shadowTrimMeters == b.shadowTrimMeters && a.vertexPaint == b.vertexPaint && a.idleWeapons == b.idleWeapons &&
           a.skipEmptyGameModeTick == b.skipEmptyGameModeTick && a.overlayKey == b.overlayKey && a.menuKey == b.menuKey &&
           a.logSeconds == b.logSeconds && a.forceUnknownBuild == b.forceUnknownBuild && a.corpseSleep == b.corpseSleep &&
           a.bodyBounds == b.bodyBounds && a.headLeaderPose == b.headLeaderPose && a.armorRayTracing == b.armorRayTracing && a.cheaperBlood == b.cheaperBlood &&
           a.physicsIslands == b.physicsIslands && a.islandMinPerWorker == b.islandMinPerWorker && a.threadScheduling == b.threadScheduling;
}

static void TestIniRoundTrip() {
    CHECK(Same(ParseSettings(SettingsIni(Settings{})), Settings{}));
    Settings s;
    s.enabled = false; s.armorBounds = false; s.shadowTrim = false; s.shadowTrimMeters = 22.5; s.vertexPaint = false;
    s.idleWeapons = false; s.skipEmptyGameModeTick = false; s.overlayKey = 0x7A; s.menuKey = 0; s.logSeconds = 0; s.forceUnknownBuild = 1;
    s.corpseSleep = false; s.bodyBounds = false; s.headLeaderPose = false; s.armorRayTracing = true; s.cheaperBlood = false;
    s.physicsIslands = false; s.islandMinPerWorker = 32; s.threadScheduling = false;
    CHECK(Same(ParseSettings(SettingsIni(s)), s));
    s.menuKey = 0xC0;   // not an F key
    CHECK(ParseSettings(SettingsIni(s)).menuKey == 0xC0);
    CHECK(ParseSettings("").menuKey == 0x78 && ParseSettings("menu_key=F7").menuKey == 0x76);
    CHECK(KeyName(0x78) == "F9" && KeyName(0x87) == "F24" && KeyName(0) == "none" && KeyName(0x41) == "0x41");
}

static void TestTrimSteps() {
    CHECK(NextTrimMeters(15, 1) == 20 && NextTrimMeters(15, -1) == 10);
    CHECK(NextTrimMeters(100, 1) == 5 && NextTrimMeters(5, -1) == 100);   // wraps
    CHECK(NextTrimMeters(17, 1) == 15);                                   // a hand-set value snaps to a step first
}

static void TestFrameStats() {
    FrameStats f;
    CHECK(f.Average() == 0 && f.P99() == 0 && f.Max() == 0);
    for (int i = 0; i < 99; ++i) f.Add(10);
    f.Add(100);
    f.Add(-5);      // ignored
    f.Add(20000);   // ignored (a pause, not a frame)
    CHECK(f.Count() == 100);
    CHECK(std::fabs(f.Average() - 10.9) < 1e-9);
    CHECK(f.P99() == 100 && f.Max() == 100);
    f.Reset();
    CHECK(f.Count() == 0);
}

static void TestShadowHysteresis() {
    CHECK(ShadowWanted(true, 16.5, 15));    // on stays on until 17 m
    CHECK(!ShadowWanted(true, 17.5, 15));
    CHECK(!ShadowWanted(false, 15.5, 15));  // off stays off until under 15 m
    CHECK(ShadowWanted(false, 14.5, 15));
}

int main() {
    TestDefaults();
    TestSettingsVersion();
    TestParse();
    TestBadValues();
    TestBoundsNeedLeaderPose();
    TestIniRoundTrip();
    TestTrimSteps();
    TestFrameStats();
    TestShadowHysteresis();
    std::printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
