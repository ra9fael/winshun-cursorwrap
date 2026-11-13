// settings_store.cpp — see settings_store.h.
//
// This translation unit needs a JSON reader/writer. The plugin vendors the
// SDK's winshun.h (Json) — use it here rather than pulling in another
// dependency, and keep the disk calls in the exe.
#include "settings_store.h"

#include "winshun.h"

namespace cw {
namespace {

constexpr int kVersion = 1;

} // namespace

std::string modeText(WrapMode m)
{
    switch (m) {
    case WrapMode::Both: return "both";
    case WrapMode::VerticalOnly: return "vertical";
    case WrapMode::HorizontalOnly: return "horizontal";
    }
    return "both";
}

std::string activationText(ActivationMode a)
{
    switch (a) {
    case ActivationMode::Always: return "always";
    case ActivationMode::WhileCtrl: return "ctrl";
    case ActivationMode::WhileShift: return "shift";
    }
    return "always";
}

// The single table. Every entry must be a real member of Settings, and every
// member of Settings must be here — tests/test_settings_roundtrip.cpp walks all
// of them and names the field that failed, so the suite keeps this honest.
const std::vector<SettingsField>& settingsFields()
{
    static const std::vector<SettingsField> table = {
        { "wrapMode", FieldKind::Text,
          [](const Settings& s) { return s.wrapMode != Settings{}.wrapMode; },
          [](const Settings&) { return 0LL; }, [](const Settings&) { return false; },
          [](const Settings& s) { return modeText(s.wrapMode); } },

        { "activationMode", FieldKind::Text,
          [](const Settings& s) { return s.activation != Settings{}.activation; },
          [](const Settings&) { return 0LL; }, [](const Settings&) { return false; },
          [](const Settings& s) { return activationText(s.activation); } },

        // The two drag/single-monitor gates. The host card applied them but
        // dumpOverrides had no entry for them, so a change made from the search
        // bar vanished on the next start. The exhaustive round-trip test found
        // them; before this table existed they were simply missing.
        { "disableDuringDrag", FieldKind::Bool,
          [](const Settings& s) { return s.disableDuringDrag != Settings{}.disableDuringDrag; },
          [](const Settings&) { return 0LL; },
          [](const Settings& s) { return s.disableDuringDrag; },
          [](const Settings&) { return std::string(); } },

        { "disableOnSingleMonitor", FieldKind::Bool,
          [](const Settings& s) {
              return s.disableOnSingleMonitor != Settings{}.disableOnSingleMonitor;
          },
          [](const Settings&) { return 0LL; },
          [](const Settings& s) { return s.disableOnSingleMonitor; },
          [](const Settings&) { return std::string(); } },

        { "edgeThresholdPx", FieldKind::Number,
          [](const Settings& s) { return s.edgeThresholdPx != Settings{}.edgeThresholdPx; },
          [](const Settings& s) { return static_cast<long long>(s.edgeThresholdPx); },
          [](const Settings&) { return false; },
          [](const Settings&) { return std::string(); } },

        { "wrapGuardPx", FieldKind::Number,
          [](const Settings& s) { return s.wrapGuardPx != Settings{}.wrapGuardPx; },
          [](const Settings& s) { return static_cast<long long>(s.wrapGuardPx); },
          [](const Settings&) { return false; },
          [](const Settings&) { return std::string(); } },

        { "adjacencyTolerancePx", FieldKind::Number,
          [](const Settings& s) {
              return s.adjacencyTolerancePx != Settings{}.adjacencyTolerancePx;
          },
          [](const Settings& s) { return static_cast<long long>(s.adjacencyTolerancePx); },
          [](const Settings&) { return false; },
          [](const Settings&) { return std::string(); } },

        { "impactGate", FieldKind::Bool,
          [](const Settings& s) { return s.impactGate != Settings{}.impactGate; },
          [](const Settings&) { return 0LL; },
          [](const Settings& s) { return s.impactGate; },
          [](const Settings&) { return std::string(); } },

        { "impactSpeedPps", FieldKind::Number,
          [](const Settings& s) { return s.impactSpeedPps != Settings{}.impactSpeedPps; },
          [](const Settings& s) { return static_cast<long long>(s.impactSpeedPps); },
          [](const Settings&) { return false; },
          [](const Settings&) { return std::string(); } },

        // The two fields the hand-written reader forgot, which is why
        // "accel 2000" was silently dropped on the next start.
        { "impactBandPx", FieldKind::Number,
          [](const Settings& s) { return s.impactBandPx != Settings{}.impactBandPx; },
          [](const Settings& s) { return static_cast<long long>(s.impactBandPx); },
          [](const Settings&) { return false; },
          [](const Settings&) { return std::string(); } },

        { "impactAccelPps2", FieldKind::Number,
          [](const Settings& s) { return s.impactAccelPps2 != Settings{}.impactAccelPps2; },
          [](const Settings& s) { return static_cast<long long>(s.impactAccelPps2); },
          [](const Settings&) { return false; },
          [](const Settings&) { return std::string(); } },

        { "tuningOverlay", FieldKind::Bool,
          [](const Settings& s) { return s.tuningOverlay != Settings{}.tuningOverlay; },
          [](const Settings&) { return 0LL; },
          [](const Settings& s) { return s.tuningOverlay; },
          [](const Settings&) { return std::string(); } },

        { "dataWindow", FieldKind::Bool,
          [](const Settings& s) { return s.dataWindow != Settings{}.dataWindow; },
          [](const Settings&) { return 0LL; },
          [](const Settings& s) { return s.dataWindow; },
          [](const Settings&) { return std::string(); } },

        { "impactFlash", FieldKind::Bool,
          [](const Settings& s) { return s.impactFlash != Settings{}.impactFlash; },
          [](const Settings&) { return 0LL; },
          [](const Settings& s) { return s.impactFlash; },
          [](const Settings&) { return std::string(); } },
    };
    return table;
}

namespace {

// One key: parse it out of `s` and apply it onto `out` through the same
// clamping path the host settings use.
//
// RawSettings mirrors Settings field by field, so one switch over the key is
// unavoidable; the TABLE is what guarantees every key has an entry here,
// because the loop below only visits keys the table names. A key added to
// Settings but forgotten in this switch is caught by the round-trip test, which
// fails with that key's name.
void applyKey(const winshun::Json& s, const SettingsField& field, Settings& out)
{
    if (!s.has(field.key))
        return;
    const winshun::Json& v = s[field.key];
    switch (field.kind) {
    case FieldKind::Text:
        if (!v.isString())
            return;
        break;
    case FieldKind::Bool:
        if (!v.isBool())
            return;
        break;
    case FieldKind::Number:
        if (!v.isNumber())
            return;
        break;
    }

    RawSettings raw;
    const std::string key = field.key;
    if (key == "wrapMode") {
        raw.wrapMode = v.toString();
        raw.hasWrapMode = true;
    } else if (key == "activationMode") {
        raw.activationMode = v.toString();
        raw.hasActivation = true;
    } else if (key == "disableDuringDrag") {
        raw.disableDuringDrag = v.toBool();
        raw.hasDrag = true;
    } else if (key == "disableOnSingleMonitor") {
        raw.disableOnSingleMonitor = v.toBool();
        raw.hasSingle = true;
    } else if (key == "edgeThresholdPx") {
        raw.edgeThresholdPx = v.toNumber();
        raw.hasEdgeThreshold = true;
    } else if (key == "wrapGuardPx") {
        raw.wrapGuardPx = v.toNumber();
        raw.hasWrapGuard = true;
    } else if (key == "adjacencyTolerancePx") {
        raw.adjacencyTolerancePx = v.toNumber();
        raw.hasAdjacency = true;
    } else if (key == "impactGate") {
        raw.impactGate = v.toBool();
        raw.hasImpactGate = true;
    } else if (key == "impactSpeedPps") {
        raw.impactSpeedPps = v.toNumber();
        raw.hasImpactSpeed = true;
    } else if (key == "impactBandPx") {
        raw.impactBandPx = v.toNumber();
        raw.hasImpactBand = true;
    } else if (key == "impactAccelPps2") {
        raw.impactAccelPps2 = v.toNumber();
        raw.hasImpactAccel = true;
    } else if (key == "tuningOverlay") {
        raw.tuningOverlay = v.toBool();
        raw.hasTuningOverlay = true;
    } else if (key == "impactFlash") {
        raw.impactFlash = v.toBool();
        raw.hasImpactFlash = true;
    } else if (key == "dataWindow") {
        raw.dataWindow = v.toBool();
        raw.hasDataWindow = true;
    }
    applySettings(raw, out, true);
}

} // namespace

Settings loadOverrides(const std::string& jsonText, bool& valid, std::string& problem)
{
    valid = false;
    Settings out;
    const auto parsed = winshun::Json::parse(jsonText, &problem);
    if (!parsed) {
        if (problem.empty())
            problem = "not JSON";
        return out;
    }
    if (!parsed->isObject()) {
        problem = "not an object";
        return out;
    }
    if ((*parsed)["version"].toInt(0) != kVersion) {
        problem = "unknown version";
        return out;
    }
    const winshun::Json& s = (*parsed)["settings"];
    if (!s.isObject()) {
        problem = "missing settings object";
        return out;
    }
    valid = true;
    problem.clear();

    // Only the keys present in the file become overrides; absent keys stay in
    // whatever the host card says. The clamps run through the same
    // applySettings path, so a hand-edited file cannot exceed the ranges.
    // Unknown keys are ignored, so a file from a newer build still loads.
    for (const SettingsField& field : settingsFields())
        applyKey(s, field, out);
    return out;
}

std::string dumpOverrides(const Settings& settings)
{
    winshun::Json::Object inner;
    for (const SettingsField& field : settingsFields()) {
        if (!field.isSet(settings))
            continue;
        switch (field.kind) {
        case FieldKind::Text:
            inner[field.key] = field.text(settings);
            break;
        case FieldKind::Bool:
            inner[field.key] = field.boolean(settings);
            break;
        case FieldKind::Number:
            inner[field.key] = field.number(settings);
            break;
        }
    }

    winshun::Json::Object root;
    root["version"] = static_cast<long long>(kVersion);
    root["settings"] = winshun::Json(std::move(inner));
    return winshun::Json(std::move(root)).dump();
}

bool overridesWrapMode(const Settings& o)
{
    return o.wrapMode != Settings{}.wrapMode;
}

bool overridesActivation(const Settings& o)
{
    return o.activation != Settings{}.activation;
}

bool overridesImpact(const Settings& o)
{
    const Settings defaults;
    return o.impactGate != defaults.impactGate || o.impactSpeedPps != defaults.impactSpeedPps
           || o.impactBandPx != defaults.impactBandPx
           || o.impactAccelPps2 != defaults.impactAccelPps2;
}

} // namespace cw
