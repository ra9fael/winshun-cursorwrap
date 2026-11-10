// test_settings_store.cpp — the search-bar override file: minimal diff dump,
// tolerant load, per-key merging, clamped values, version gate.
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
    s.impactGate = true;
    const std::string json = dumpOverrides(s);
    CW_CHECK(json.find("\"impactGate\":true") != std::string::npos);
    // Untouched keys are not written.
    CW_CHECK(json.find("wrapMode") == std::string::npos);
    CW_CHECK(json.find("wrapGuardPx") == std::string::npos);
}

CW_TEST(store_roundtrip_all_keys)
{
    Settings s;
    s.wrapMode = WrapMode::VerticalOnly;
    s.activation = ActivationMode::WhileShift;
    s.impactGate = true;
    s.impactSpeedPps = 3500;
    s.wrapGuardPx = 90;
    s.edgeThresholdPx = 4;
    s.adjacencyTolerancePx = 120;
    const std::string json = dumpOverrides(s);
    bool valid = false;
    std::string problem;
    const Settings back = loadOverrides(json, valid, problem);
    CW_CHECK(valid);
    CW_CHECK(problem.empty());
    CW_CHECK(back.wrapMode == WrapMode::VerticalOnly);
    CW_CHECK(back.activation == ActivationMode::WhileShift);
    CW_CHECK_EQ(back.impactGate, true);
    CW_CHECK_EQ(back.impactSpeedPps, 3500);
    CW_CHECK_EQ(back.wrapGuardPx, 90);
    CW_CHECK_EQ(back.edgeThresholdPx, 4);
    CW_CHECK_EQ(back.adjacencyTolerancePx, 120);
}

CW_TEST(store_load_partial_file_keeps_defaults)
{
    // A file with only one key: the rest stay at the defaults (the exe then
    // merges them over the host's values).
    bool valid = false;
    std::string problem;
    const Settings s = loadOverrides(
        R"({"version":1,"settings":{"impactGate":true}})", valid, problem);
    CW_CHECK(valid);
    CW_CHECK_EQ(s.impactGate, true);
    CW_CHECK(s.wrapMode == WrapMode::Both); // untouched
    CW_CHECK_EQ(s.wrapGuardPx, 50);         // untouched
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
    loadOverrides(R"({"version":1,"settings":{"impactGate":"yes"}})", valid, problem);
    CW_CHECK(valid); // a wrong-typed value is dropped, the file still loads
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
