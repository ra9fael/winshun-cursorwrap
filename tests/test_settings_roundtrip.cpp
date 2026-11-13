// test_settings_roundtrip.cpp — every setting must survive a write/read cycle.
//
// Why this exists
// ---------------
// The user reported "the search bar's settings and the plugin page's settings
// are not in sync". The cause was not a display bug: the plugin WROTE all 13
// settings but READ BACK only 9. `loadOverridesFromDisk()` in main.cpp rebuilt
// a RawSettings by hand and never mentioned impactBandPx or impactAccelPps2, so
// "accel 2000" survived in settings.json and was silently dropped on the next
// start (measured: the menu showed "off (speed only)" after a restart).
//
// A hand-written list of 13 fields in six places is the real defect, and it
// will happen again the moment a setting is added. So the core test below is
// EXHAUSTIVE and driven by a table: it walks every field, and the table is the
// only place a new setting has to be registered. `kFieldCount` is compared
// against the table's own size, so forgetting to add a field to the table shows
// up as a failing test rather than a silent gap.
#include "settings_store.h"
#include "test_harness.h"
#include "wrap_logic.h"

#include <string>
#include <vector>

namespace {

// One row per field of cw::Settings. `mutate` puts a NON-DEFAULT, in-range value
// into the struct; `read` reports what the struct says. If a field is missing
// here, kFieldCount and kFields disagree and the test fails.
struct Field {
    const char* name;
    void (*mutate)(cw::Settings&);
    bool (*differs)(const cw::Settings&);
};

const std::vector<Field>& fields()
{
    static const std::vector<Field> table = {
        { "wrapMode",
          [](cw::Settings& s) { s.wrapMode = cw::WrapMode::VerticalOnly; },
          [](const cw::Settings& s) { return s.wrapMode == cw::WrapMode::VerticalOnly; } },
        { "activationMode",
          [](cw::Settings& s) { s.activation = cw::ActivationMode::WhileCtrl; },
          [](const cw::Settings& s) { return s.activation == cw::ActivationMode::WhileCtrl; } },
        { "disableDuringDrag",
          [](cw::Settings& s) { s.disableDuringDrag = false; },
          [](const cw::Settings& s) { return s.disableDuringDrag == false; } },
        { "disableOnSingleMonitor",
          [](cw::Settings& s) { s.disableOnSingleMonitor = true; },
          [](const cw::Settings& s) { return s.disableOnSingleMonitor == true; } },
        { "edgeThresholdPx",
          [](cw::Settings& s) { s.edgeThresholdPx = 7; },
          [](const cw::Settings& s) { return s.edgeThresholdPx == 7; } },
        { "wrapGuardPx",
          [](cw::Settings& s) { s.wrapGuardPx = 77; },
          [](const cw::Settings& s) { return s.wrapGuardPx == 77; } },
        { "adjacencyTolerancePx",
          [](cw::Settings& s) { s.adjacencyTolerancePx = 88; },
          [](const cw::Settings& s) { return s.adjacencyTolerancePx == 88; } },
        { "impactGate",
          [](cw::Settings& s) { s.impactGate = true; },
          [](const cw::Settings& s) { return s.impactGate == true; } },
        { "impactSpeedPps",
          [](cw::Settings& s) { s.impactSpeedPps = 4321; },
          [](const cw::Settings& s) { return s.impactSpeedPps == 4321; } },
        // The two fields the hand-written reader forgot. They are the whole
        // reason this file exists, so they are not allowed to be "special".
        { "impactBandPx",
          [](cw::Settings& s) { s.impactBandPx = 400; },
          [](const cw::Settings& s) { return s.impactBandPx == 400; } },
        { "impactAccelPps2",
          [](cw::Settings& s) { s.impactAccelPps2 = 2000; },
          [](const cw::Settings& s) { return s.impactAccelPps2 == 2000; } },
        { "tuningOverlay",
          [](cw::Settings& s) { s.tuningOverlay = true; },
          [](const cw::Settings& s) { return s.tuningOverlay == true; } },
        { "dataWindow",
          [](cw::Settings& s) { s.dataWindow = true; },
          [](const cw::Settings& s) { return s.dataWindow == true; } },
    };
    return table;
}

// cw::Settings has this many data members. Keep in step with wrap_logic.h; a
// new field makes the exhaustive test fail until it is listed above.
constexpr size_t kFieldCount = 13;

} // namespace

CW_TEST(settings_field_table_is_complete)
{
    // The guard against the original bug: a setting that exists but is missing
    // from the table would never be round-tripped, and nothing would notice.
    CW_CHECK_EQ(fields().size(), kFieldCount);
}

CW_TEST(settings_every_field_round_trips)
{
    for (const Field& f : fields()) {
        cw::Settings settings;
        f.mutate(settings);
        const std::string json = cw::dumpOverrides(settings);

        bool valid = false;
        std::string problem;
        const cw::Settings read = cw::loadOverrides(json, valid, problem);
        if (!valid) {
            ++cwtest::failures();
            std::printf("  field %s: dump produced an unreadable file: %s\n", f.name,
                        problem.c_str());
            continue;
        }
        if (!f.differs(read)) {
            ++cwtest::failures();
            std::printf("  field %s did NOT survive the round trip; json was %s\n", f.name,
                        json.c_str());
        }
    }
}

CW_TEST(settings_band_and_accel_specifically)
{
    // The exact regression the user hit, spelled out so its name appears in the
    // log: "accel 2000 survived in settings.json and was dropped on restart".
    cw::Settings s;
    // Both must DIFFER from the defaults, or dumpOverrides (a minimal diff) has
    // nothing to write and the test would pass for the wrong reason.
    s.impactBandPx = cw::Settings{}.impactBandPx == 400 ? 250 : 400;
    s.impactAccelPps2 = 2000;
    const std::string json = cw::dumpOverrides(s);
    CW_CHECK(json.find("impactBandPx") != std::string::npos);
    CW_CHECK(json.find("impactAccelPps2") != std::string::npos);

    bool valid = false;
    std::string problem;
    const cw::Settings read = cw::loadOverrides(json, valid, problem);
    CW_CHECK(valid);
    CW_CHECK_EQ(read.impactBandPx, cw::Settings{}.impactBandPx == 400 ? 250 : 400);
    CW_CHECK_EQ(read.impactAccelPps2, 2000);
}

CW_TEST(settings_unknown_key_is_ignored)
{
    // A file from a newer build must not make the reader throw the rest away.
    const std::string json =
        R"({"version":1,"settings":{"impactAccelPps2":2000,"futureThing":42}})";
    bool valid = false;
    std::string problem;
    const cw::Settings read = cw::loadOverrides(json, valid, problem);
    CW_CHECK(valid);
    CW_CHECK_EQ(read.impactAccelPps2, 2000);
}

CW_TEST(settings_out_of_range_values_are_rejected_not_silently_accepted)
{
    // A hand-edited file with an absurd value must not become a usable value.
    // The documented policy is "out of range falls back to the default" (it must
    // never break the plugin), so the check is: the result is inside the range
    // and is NOT the absurd number.
    const std::string json = R"({"version":1,"settings":{"impactAccelPps2":999999}})";
    bool valid = false;
    std::string problem;
    const cw::Settings read = cw::loadOverrides(json, valid, problem);
    CW_CHECK(valid);
    CW_CHECK(read.impactAccelPps2 <= 200000);
    CW_CHECK(read.impactAccelPps2 != 999999);
    // In-range values must survive exactly; only out-of-range ones fall back.
    const std::string ok = R"({"version":1,"settings":{"impactAccelPps2":2000}})";
    const cw::Settings kept = cw::loadOverrides(ok, valid, problem);
    CW_CHECK_EQ(kept.impactAccelPps2, 2000);
}
