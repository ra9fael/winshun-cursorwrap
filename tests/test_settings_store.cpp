// test_settings_store.cpp — the plugin's tuning file: minimal diff dump,
// tolerant load, per-key merging, clamped values, ownership split.
#include "test_harness.h"
#include "settings_store.h"

using namespace cw;

CW_TEST(store_dump_defaults_is_empty)
{
    // All defaults: the dump carries an empty settings object, which the exe
    // treats as "delete the file".
    const std::string json = dumpOverrides(Settings{});
    CW_CHECK(json.find("\"settings\":{}") != std::string::npos);
    CW_CHECK(json.find("\"version\":1") != std::string::npos);
}

CW_TEST(store_dump_is_minimal_diff)
{
    Settings s;
    s.impactSpeedPps = 3500;
    const std::string json = dumpOverrides(s);
    CW_CHECK(json.find("\"impactSpeedPps\":3500") != std::string::npos);
    // Untouched keys are not written — including the host card's keys: those
    // belong to the host, and writing them here would let the plugin's stale
    // copy win over a newer change made on the card.
    CW_CHECK(json.find("wrapMode") == std::string::npos);
    CW_CHECK(json.find("impactGate") == std::string::npos);
    CW_CHECK(json.find("wrapGuardPx") == std::string::npos);
}

CW_TEST(store_roundtrip_plugin_keys)
{
    // The plugin owns the tuning keys; they must survive the file round trip.
    Settings s;
    s.impactSpeedPps = 3500;
    s.wrapGuardPx = 90;
    s.edgeThresholdPx = 4;
    s.adjacencyTolerancePx = 120;
    s.impactBandPx = 250;
    s.impactAccelPps2 = 2000;
    s.tuningOverlay = true;
    s.dataWindow = true;
    s.impactFlash = true;
    const std::string json = dumpOverrides(s);
    bool valid = false;
    std::string problem;
    const Settings back = loadOverrides(json, valid, problem);
    CW_CHECK(valid);
    CW_CHECK(problem.empty());
    CW_CHECK_EQ(back.impactSpeedPps, 3500);
    CW_CHECK_EQ(back.wrapGuardPx, 90);
    CW_CHECK_EQ(back.edgeThresholdPx, 4);
    CW_CHECK_EQ(back.adjacencyTolerancePx, 120);
    CW_CHECK_EQ(back.impactBandPx, 250);
    CW_CHECK_EQ(back.impactAccelPps2, 2000);
    CW_CHECK(back.tuningOverlay);
    CW_CHECK(back.dataWindow);
    CW_CHECK(back.impactFlash);
    // The host card's keys are untouched by the file.
    CW_CHECK(back.wrapMode == Settings{}.wrapMode);
    CW_CHECK(back.activation == Settings{}.activation);
    CW_CHECK_EQ(back.impactGate, Settings{}.impactGate);
}

CW_TEST(store_host_keys_are_host_business)
{
    // A pre-split file may still carry host-card keys: they are IGNORED, so an
    // old override cannot win over a newer change the user made on the card.
    bool valid = false;
    std::string problem;
    const Settings s = loadOverrides(
        R"({"version":1,"settings":{"wrapMode":"vertical","impactGate":true,
            "disableDuringDrag":false,"impactSpeedPps":3500}})",
        valid, problem);
    CW_CHECK(valid);
    CW_CHECK(s.wrapMode == Settings{}.wrapMode);      // host's key: ignored
    CW_CHECK_EQ(s.impactGate, Settings{}.impactGate); // host's key: ignored
    CW_CHECK_EQ(s.disableDuringDrag, Settings{}.disableDuringDrag);
    CW_CHECK_EQ(s.impactSpeedPps, 3500);              // plugin's key: applied
}

CW_TEST(store_load_partial_file_keeps_defaults)
{
    // A file with only one key: the rest stay at the defaults (the exe then
    // merges them over the host's values).
    bool valid = false;
    std::string problem;
    const Settings s = loadOverrides(
        R"({"version":1,"settings":{"impactSpeedPps":3500}})", valid, problem);
    CW_CHECK(valid);
    CW_CHECK_EQ(s.impactSpeedPps, 3500);
    CW_CHECK_EQ(s.wrapGuardPx, 50); // untouched
}

CW_TEST(store_load_clamps_out_of_range)
{
    // applySettings fails to the default for out-of-range values (it never
    // invents a number); the parser clamps typed input before it gets here.
    bool valid = false;
    std::string problem;
    const Settings s = loadOverrides(
        R"({"version":1,"settings":{"impactSpeedPps":999999,"wrapGuardPx":1}})",
        valid, problem);
    CW_CHECK(valid);
    CW_CHECK_EQ(s.impactSpeedPps, 2000); // out of range -> the default
    CW_CHECK_EQ(s.wrapGuardPx, 50);      // out of range -> the default
}

CW_TEST(store_load_rejects_bad_documents)
{
    bool valid = false;
    std::string problem;
    loadOverrides("not json at all", valid, problem);
    CW_CHECK(!valid);
    loadOverrides("[1,2,3]", valid, problem);
    CW_CHECK(!valid);
    loadOverrides(R"({"version":2,"settings":{}})", valid, problem);
    CW_CHECK(!valid); // unknown version
    loadOverrides(R"({"version":1})", valid, problem);
    CW_CHECK(!valid); // missing settings object
    loadOverrides(R"({"version":1,"settings":{"impactSpeedPps":"fast"}})", valid, problem);
    CW_CHECK(valid); // a wrong-typed value is dropped, the file still loads
}

CW_TEST(store_raw_from_json_ownership)
{
    // initialize may still carry pre-split values, so the full table is read.
    const auto full = winshun::Json::parse(
        R"({"wrapMode":"vertical","impactSpeedPps":3500,"unknown":1})", nullptr);
    const cw::RawSettings all = rawOverridesFromJson(*full, false);
    CW_CHECK(all.hasWrapMode);
    CW_CHECK(all.hasImpactSpeed);
    // settingsChanged is filtered to the host card's declared keys, so a stale
    // tuning value left in WinShun.ini from an older version cannot restore
    // itself over the user's newer tuning.
    const cw::RawSettings host = rawOverridesFromJson(*full, true);
    CW_CHECK(host.hasWrapMode);
    CW_CHECK(!host.hasImpactSpeed);
}

CW_TEST(store_override_predicates)
{
    CW_CHECK(!overridesImpact(Settings{}));
    CW_CHECK(!overridesWrapMode(Settings{}));
    CW_CHECK(!overridesActivation(Settings{}));
    Settings s;
    s.impactGate = true;
    CW_CHECK(overridesImpact(s));
    CW_CHECK(!overridesWrapMode(s));
    s.wrapMode = WrapMode::HorizontalOnly;
    CW_CHECK(overridesWrapMode(s));
    s.activation = ActivationMode::WhileCtrl;
    CW_CHECK(overridesActivation(s));
}
