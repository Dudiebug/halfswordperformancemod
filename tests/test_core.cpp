// Unit tests for src/core/perf_core.h. Run: tests/run_tests.sh
#include "core/perf_core.h"
#include <cmath>
#include <cstdio>

using namespace hsp::core;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void TestDefaults() {
    const Settings s = ParseSettings("");
    CHECK(s.enabled && s.armorLeaderPose && s.armorBounds && !s.shadowTrim && s.vertexPaint);
    CHECK(s.overlayKey == 0x79 && s.logSeconds == 10 && s.shadowTrimMeters == 15.0);
}

static void TestParse() {
    const Settings s = ParseSettings(
        "; comment\n[other]\nenabled=0\n[Perf]\r\nshadow_trim = on ; trailing\nshadow_trim_meters=25\n"
        "vertex_paint=false\noverlay_key=F9\nlog_seconds=30\narmor_bounds=0\nforce_unknown_build=2\nbogus=1\n");
    CHECK(s.enabled);               // [other] is not read
    CHECK(s.shadowTrim && s.shadowTrimMeters == 25.0);
    CHECK(!s.vertexPaint && s.overlayKey == 0x78 && s.logSeconds == 30 && !s.armorBounds && s.forceUnknownBuild == 2);
}

static void TestBadValues() {
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
    TestParse();
    TestBadValues();
    TestBoundsNeedLeaderPose();
    TestFrameStats();
    TestShadowHysteresis();
    std::printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
