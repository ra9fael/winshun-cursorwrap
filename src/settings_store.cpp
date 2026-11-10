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

// One key: parse it out of `s` and apply it onto `out` through the same
// clamping path the host settings use.
void applyKey(const winshun::Json& s, const char* key, Settings& out,
              void (*fill)(RawSettings&, const winshun::Json&))
{
    if (!s.has(key))
        return;
    RawSettings raw;
    fill(raw, s[key]);
    applySettings(raw, out, true);
}

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

    // Only the keys present in the file become overrides; absent keys stay
    // in whatever the host card says. The clamps run through the same
    // applySettings path, so a hand-edited file cannot exceed the ranges.
    applyKey(s, "wrapMode", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isString()) {
            raw.wrapMode = v.toString();
            raw.hasWrapMode = true;
        }
    });
    applyKey(s, "activationMode", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isString()) {
            raw.activationMode = v.toString();
            raw.hasActivation = true;
        }
    });
    applyKey(s, "impactGate", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isBool()) {
            raw.impactGate = v.toBool();
            raw.hasImpactGate = true;
        }
    });
    applyKey(s, "impactSpeedPps", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isNumber()) {
            raw.impactSpeedPps = v.toNumber();
            raw.hasImpactSpeed = true;
        }
    });
    applyKey(s, "impactBandPx", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isNumber()) {
            raw.impactBandPx = v.toNumber();
            raw.hasImpactBand = true;
        }
    });
    applyKey(s, "wrapGuardPx", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isNumber()) {
            raw.wrapGuardPx = v.toNumber();
            raw.hasWrapGuard = true;
        }
    });
    applyKey(s, "edgeThresholdPx", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isNumber()) {
            raw.edgeThresholdPx = v.toNumber();
            raw.hasEdgeThreshold = true;
        }
    });
    applyKey(s, "adjacencyTolerancePx", out, [](RawSettings& raw, const winshun::Json& v) {
        if (v.isNumber()) {
            raw.adjacencyTolerancePx = v.toNumber();
            raw.hasAdjacency = true;
        }
    });
    return out;
}

std::string dumpOverrides(const Settings& settings)
{
    const Settings defaults;
    winshun::Json::Object inner;
    if (settings.wrapMode != defaults.wrapMode)
        inner["wrapMode"] = modeText(settings.wrapMode);
    if (settings.activation != defaults.activation)
        inner["activationMode"] = activationText(settings.activation);
    if (settings.impactGate != defaults.impactGate)
        inner["impactGate"] = settings.impactGate;
    if (settings.impactSpeedPps != defaults.impactSpeedPps)
        inner["impactSpeedPps"] = static_cast<long long>(settings.impactSpeedPps);
    if (settings.impactBandPx != defaults.impactBandPx)
        inner["impactBandPx"] = static_cast<long long>(settings.impactBandPx);
    if (settings.wrapGuardPx != defaults.wrapGuardPx)
        inner["wrapGuardPx"] = static_cast<long long>(settings.wrapGuardPx);
    if (settings.edgeThresholdPx != defaults.edgeThresholdPx)
        inner["edgeThresholdPx"] = static_cast<long long>(settings.edgeThresholdPx);
    if (settings.adjacencyTolerancePx != defaults.adjacencyTolerancePx)
        inner["adjacencyTolerancePx"] = static_cast<long long>(settings.adjacencyTolerancePx);

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
           || o.impactBandPx != defaults.impactBandPx;
}

} // namespace cw
