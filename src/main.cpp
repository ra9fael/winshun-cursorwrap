// main.cpp — WinShunCursorWrap: a Win顺 resident plugin.
//
// Multi-monitor cursor wrapping: when the pointer reaches an outer edge of
// the monitor layout it re-enters from the nearest opposite edge (PowerToys
// CursorWrap's algorithm, re-implemented as a pure core so it unit-tests
// without a display). Inner edges — where two monitors touch — never wrap;
// Windows already handles monitor-to-monitor movement.
//
// Keyboard: the `cursorwrap` keyword shows the status row, and the text
// after the keyword changes settings without opening the host's settings
// card — `cursorwrap impact`, `cursorwrap speed 3000`, `cursorwrap ctrl`,
// `cursorwrap mode vertical`, `cursorwrap reset` (see query_parser.h for the
// full grammar). Changes apply immediately and persist in the plugin's data
// directory (WS_DATA_DIR/settings.json); the host's settings card remains
// authoritative for keys the search bar has not overridden.
#include "winshun.h"

#include "monitor_watcher.h"
#include "mouse_hook.h"
#include "overlay.h"
#include "query_parser.h"
#include "settings_store.h"
#include "wrap_logic.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

using winshun::ActionRequest;
using winshun::Effects;
using winshun::Item;
using winshun::Items;
using winshun::Json;
using winshun::Query;
namespace effect = winshun::effect;

namespace {

std::string modeLabel(cw::WrapMode mode, bool chinese)
{
    switch (mode) {
    case cw::WrapMode::Both:
        return chinese ? "双向" : "both";
    case cw::WrapMode::VerticalOnly:
        return chinese ? "仅上下" : "vertical only";
    case cw::WrapMode::HorizontalOnly:
        return chinese ? "仅左右" : "horizontal only";
    }
    return "both";
}

std::string activationLabel(cw::ActivationMode mode, bool chinese)
{
    switch (mode) {
    case cw::ActivationMode::Always:
        return chinese ? "总是" : "always";
    case cw::ActivationMode::WhileCtrl:
        return chinese ? "按住 Ctrl" : "while Ctrl is held";
    case cw::ActivationMode::WhileShift:
        return chinese ? "按住 Shift" : "while Shift is held";
    }
    return "always";
}

std::string intText(long long v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", v);
    return buf;
}

cw::RawSettings rawFromJson(const Json& s)
{
    cw::RawSettings raw;
    if (!s.isObject())
        return raw;
    auto stringField = [&s](const char* key, std::string& out, bool& has) {
        const Json& v = s[key];
        if (v.isString()) {
            out = v.toString();
            has = true;
        }
    };
    auto boolField = [&s](const char* key, bool& out, bool& has) {
        const Json& v = s[key];
        if (v.isBool()) {
            out = v.toBool();
            has = true;
        }
    };
    auto numberField = [&s](const char* key, double& out, bool& has) {
        const Json& v = s[key];
        if (v.isNumber()) {
            out = v.toNumber();
            has = true;
        }
    };
    stringField("wrapMode", raw.wrapMode, raw.hasWrapMode);
    stringField("activationMode", raw.activationMode, raw.hasActivation);
    boolField("disableDuringDrag", raw.disableDuringDrag, raw.hasDrag);
    boolField("disableOnSingleMonitor", raw.disableOnSingleMonitor, raw.hasSingle);
    numberField("edgeThresholdPx", raw.edgeThresholdPx, raw.hasEdgeThreshold);
    numberField("wrapGuardPx", raw.wrapGuardPx, raw.hasWrapGuard);
    numberField("adjacencyTolerancePx", raw.adjacencyTolerancePx, raw.hasAdjacency);
    boolField("impactGate", raw.impactGate, raw.hasImpactGate);
    numberField("impactSpeedPps", raw.impactSpeedPps, raw.hasImpactSpeed);
    numberField("impactBandPx", raw.impactBandPx, raw.hasImpactBand);
    return raw;
}

std::vector<cw::MonitorBox> enumerateMonitors()
{
    std::vector<cw::MonitorBox> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::vector<cw::MonitorBox>*>(lp);
        MONITORINFO mi {};
        mi.cbSize = sizeof mi;
        if (GetMonitorInfo(hMonitor, &mi)) {
            cw::MonitorBox box;
            box.left = mi.rcMonitor.left;
            box.top = mi.rcMonitor.top;
            box.right = mi.rcMonitor.right;
            box.bottom = mi.rcMonitor.bottom;
            box.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            box.dpi = 96;
            out->push_back(box);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

std::wstring overridePath(const std::string& dataDir)
{
    if (dataDir.empty())
        return {};
    return winshun::wide(dataDir) + L"\\settings.json";
}

std::string readFileText(const std::wstring& path)
{
    if (path.empty())
        return {};
    FILE* f = nullptr;
    if (::_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return {};
    std::string text;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0)
        text.append(buffer, got);
    std::fclose(f);
    return text;
}

bool writeFileText(const std::wstring& path, const std::string& text)
{
    if (path.empty())
        return false;
    FILE* f = nullptr;
    if (::_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
        return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

bool deleteFile(const std::wstring& path)
{
    if (path.empty())
        return true;
    return ::DeleteFileW(path.c_str()) != 0 || ::GetLastError() == ERROR_FILE_NOT_FOUND;
}

} // namespace

class CursorWrapPlugin final : public winshun::Plugin {
public:
    std::string name() const override { return "ra9fael.cursorwrap"; }

protected:
    void initialized() override
    {
        // Host card first (defaults + the user's values), then the search
        // bar's overrides on top.
        applySettings(rawFromJson(settings()), m_settings, false);
        loadOverridesFromDisk();
        m_engine = std::make_unique<cw::WrapEngine>(m_settings);
        m_engine->rebuild(enumerateMonitors());

        m_hook = std::make_unique<cw::MouseHook>();
        const bool hookOk = m_hook->start(m_engine.get(), [this](int64_t, int64_t) {
            // A wrap happened; a status row left open should update.
            refresh();
        });
        m_hookOk = hookOk;

        m_watcher = std::make_unique<cw::MonitorWatcher>();
        m_watcherOk = m_watcher->start([this] {
            if (m_engine) {
                m_engine->rebuild(enumerateMonitors());
                if (m_hook)
                    m_hook->updateSettings(m_engine->settings());
                // The overlay windows must follow the new layout.
                if (m_tunerOn)
                    m_overlay.start(m_engine->monitorsSnapshot());
            }
        });
        m_overlay.setEngine(m_engine.get());

        log(hookOk && m_watcherOk ? "info" : "warn",
            std::string("cursorwrap: hook ") + (hookOk ? "installed" : "FAILED")
                + ", watcher " + (m_watcherOk ? "started" : "FAILED")
                + ", " + intText(static_cast<long long>(m_engine->monitorCount()))
                + " monitor(s)"
                + (m_overrides ? ", overrides loaded" : ""));
    }

    Items query(const Query& q) override
    {
        const bool chinese = winshun::Plugin::chinese();
        // Some host builds expose only the full raw query. In keyword mode,
        // remove the matched alias before parsing the command tail.
        std::string input = q.text;
        if (input.empty()) {
            input = q.raw;
            if (q.trigger == "keyword" && !q.keyword.empty()
                && cw::asciiLower(input).compare(0, cw::asciiLower(q.keyword).size(),
                                                  cw::asciiLower(q.keyword)) == 0) {
                input.erase(0, q.keyword.size());
            }
        }
        const cw::ParsedCommand cmd = cw::parseCommand(input);
        switch (cmd.kind) {
        case cw::CommandKind::Show:
            return settingsList();
        case cw::CommandKind::Unknown:
            return { unknownRow(q.text) };
        case cw::CommandKind::CopyDiagnostics:
            // The copy effect only runs on action responses, so this query
            // returns a diagnostics row: its copy field carries the text,
            // and Enter on it runs the plugin's copyDiag action.
            return { diagnosticsRow() };
        case cw::CommandKind::ToggleTuner: {
            // Toggle the overlay and refresh the list (the row shows on/off).
            m_tunerOn = !m_tunerOn;
            if (m_tunerOn && m_engine)
                m_overlay.start(m_engine->monitorsSnapshot());
            else
                m_overlay.stop();
            return settingsList();
        }
        case cw::CommandKind::MenuImpact:
            return impactMenu();
        case cw::CommandKind::MenuSpeed:
            return speedMenu();
        case cw::CommandKind::MenuBand:
            return bandMenu();
        case cw::CommandKind::MenuMode:
            return modeMenu();
        case cw::CommandKind::MenuActivation:
            return activationMenu();
        case cw::CommandKind::MenuGuard:
            return guardMenu();
        case cw::CommandKind::MenuEdge:
            return edgeMenu();
        case cw::CommandKind::MenuTolerance:
            return toleranceMenu();
        case cw::CommandKind::MenuDrag:
            return boolMenu("cursorwrap.row.drag", chinese ? "拖拽时不回绕" : "No wrap while dragging",
                            chinese ? "按住左键拖动窗口时禁用回绕，防止把窗口甩到对角"
                                    : "disables wrapping while the left button is held",
                            m_settings.disableDuringDrag, "drag");
        case cw::CommandKind::MenuSingle:
            return boolMenu("cursorwrap.row.single",
                            chinese ? "单屏时禁用" : "Disable on a single monitor",
                            chinese ? "只接一块屏时也回绕到本屏对边；不想这样可打开此项"
                                    : "on one display the cursor still wraps to the same display's opposite edge",
                            m_settings.disableOnSingleMonitor, "single");
        case cw::CommandKind::MenuTuner:
            return tunerMenu();
        case cw::CommandKind::MenuBest:
            return bestMenu();
        case cw::CommandKind::CopyTrace:
            // Like the diagnostics row: the text rides in the copy field, and
            // Enter runs the plugin action that flashes "copied".
            return { traceRow() };
        default: {
            // A setting change: apply it, persist it, show the new state.
            applyCommand(cmd);
            // For numeric settings, show the submenu with the new value
            // marked current — the user just picked it, so the submenu is
            // the natural next view.
            switch (cmd.kind) {
            case cw::CommandKind::SetImpactSpeed: return speedMenu();
            case cw::CommandKind::SetBand: return bandMenu();
            case cw::CommandKind::SetGuard: return guardMenu();
            case cw::CommandKind::SetEdge: return edgeMenu();
            case cw::CommandKind::SetTolerance: return toleranceMenu();
            default: break;
            }
            return settingsList();
        }
        }
    }

    Effects action(const ActionRequest& a) override
    {
        const bool chinese = winshun::Plugin::chinese();
        if (a.action == "toggleEnabled") {
            const bool on = !m_hookEnabled;
            setEnabled(on);
            return listEffects(effect::notify(on ? (chinese ? "光标回绕已开启" : "Cursor wrap on")
                                                 : (chinese ? "光标回绕已关闭" : "Cursor wrap off")));
        }
        if (a.action == "cycleMode") {
            cycleMode();
            persistOverrides();
            pushSettings();
            return listEffects(effect::notify(std::string(chinese ? "回绕模式：" : "Wrap mode: ")
                                              + modeLabel(m_settings.wrapMode, chinese)));
        }
        if (a.action == "toggleImpact") {
            m_settings.impactGate = !m_settings.impactGate;
            persistOverrides();
            pushSettings();
            return listEffects(effect::notify(impactNotifyText()));
        }
        if (a.action == "setActivationCtrl" || a.action == "setActivationShift"
            || a.action == "setActivationAlways") {
            m_settings.activation = a.action == "setActivationCtrl"   ? cw::ActivationMode::WhileCtrl
                : a.action == "setActivationShift"                    ? cw::ActivationMode::WhileShift
                                                                      : cw::ActivationMode::Always;
            persistOverrides();
            pushSettings();
            return listEffects(effect::notify(std::string(chinese ? "激活方式：" : "Activation: ")
                                              + activationLabel(m_settings.activation, chinese)));
        }
        if (a.action == "resetDefaults") {
            const cw::Settings defaults;
            m_settings.wrapMode = defaults.wrapMode;
            m_settings.activation = defaults.activation;
            m_settings.impactGate = defaults.impactGate;
            m_settings.impactSpeedPps = defaults.impactSpeedPps;
            m_settings.impactBandPx = defaults.impactBandPx;
            m_settings.wrapGuardPx = defaults.wrapGuardPx;
            m_settings.edgeThresholdPx = defaults.edgeThresholdPx;
            m_settings.adjacencyTolerancePx = defaults.adjacencyTolerancePx;
            persistOverrides();
            pushSettings();
            return listEffects(effect::notify(chinese ? "已恢复宿主设置卡" : "Host settings restored"));
        }
        // Numeric value rows: "set:<field>:<value>".
        if (a.action.rfind("set:", 0) == 0) {
            const size_t f1 = a.action.find(':', 4);
            if (f1 != std::string::npos) {
                const std::string field = a.action.substr(4, f1 - 4);
                const long long value = std::atoll(a.action.c_str() + f1 + 1);
                Items after;
                if (field == "speed") {
                    m_settings.impactGate = true;
                    m_settings.impactSpeedPps = static_cast<int>(value);
                    after = speedMenu();
                } else if (field == "band") {
                    m_settings.impactBandPx = static_cast<int>(value);
                    after = bandMenu();
                } else if (field == "guard") {
                    m_settings.wrapGuardPx = static_cast<int>(value);
                    after = guardMenu();
                } else if (field == "edge") {
                    m_settings.edgeThresholdPx = static_cast<int>(value);
                    after = edgeMenu();
                } else if (field == "tol") {
                    m_settings.adjacencyTolerancePx = static_cast<int>(value);
                    after = toleranceMenu();
                } else if (field == "drag") {
                    m_settings.disableDuringDrag = value != 0;
                    after = boolMenu("cursorwrap.row.drag",
                                     chinese ? "拖拽时不回绕" : "No wrap while dragging",
                                     chinese ? "按住左键拖动窗口时禁用回绕，防止把窗口甩到对角"
                                             : "disables wrapping while the left button is held",
                                     m_settings.disableDuringDrag, "drag");
                } else if (field == "single") {
                    m_settings.disableOnSingleMonitor = value != 0;
                    after = boolMenu("cursorwrap.row.single",
                                     chinese ? "单屏时禁用" : "Disable on a single monitor",
                                     chinese ? "只接一块屏时也回绕到本屏对边；不想这样可打开此项"
                                             : "on one display the cursor still wraps to the same display's opposite edge",
                                     m_settings.disableOnSingleMonitor, "single");
                } else if (field == "mode") {
                    m_settings.wrapMode = static_cast<cw::WrapMode>(value);
                    after = modeMenu();
                }
                if (!after.empty()) {
                    persistOverrides();
                    pushSettings();
                    return { effect::replace(after), effect::keep() };
                }
            }
        }
        if (a.action == "toggleTuner") {
            m_tunerOn = !m_tunerOn;
            if (m_tunerOn && m_engine)
                m_overlay.start(m_engine->monitorsSnapshot());
            else
                m_overlay.stop();
            const bool chinese = winshun::Plugin::chinese();
            return listEffects(effect::notify(
                m_tunerOn ? (chinese ? "调参可视化已开启：甩到边缘看速度读数" : "Tuning overlay on: push to an edge and watch the readout")
                          : (chinese ? "调参可视化已关闭" : "Tuning overlay off")));
        }
        if (a.action == "copyDiagnostics") {
            return { effect::copy(diagnosticsText()), effect::keep(),
                     effect::notify(chinese ? "诊断已复制" : "Diagnostics copied") };
        }
        if (a.action == "copyTrace") {
            return { effect::copy(traceCsv()), effect::hide() };
        }
        if (a.action == "copyDiagRow") {
            // Enter on the diagnostics row (its copy field already carries
            // the text, but the plugin action makes the "已复制" flash).
            return { effect::copy(diagnosticsText()), effect::hide() };
        }
        Item row("cursorwrap.error", chinese ? "未知动作" : "Unknown action", a.action);
        row.invalid();
        return { effect::replace({ row }), effect::notify("unknown action: " + a.action) };
    }

    void settingsChanged(const Json& settings) override
    {
        // The host card is authoritative for keys the search bar has not
        // overridden: start from the host's values, then re-apply the file.
        applySettings(rawFromJson(settings), m_settings, true);
        loadOverridesFromDisk();
        if (m_engine) {
            m_engine->setSettings(m_settings);
            // The adjacency tolerance changes the topology itself.
            m_engine->rebuild(enumerateMonitors());
        }
        if (m_hook)
            m_hook->updateSettings(m_settings);
    }

    void shuttingDown() override
    {
        m_overlay.stop();
        if (m_hook) {
            m_hook->stop();
            m_hook.reset();
        }
        if (m_watcher) {
            m_watcher->stop();
            m_watcher.reset();
        }
        m_engine.reset();
    }

    void appearanceChanged() override
    {
        // The overlay paints in the host's accent/text colours.
        const Json look = appearance()["nativeLook"];
        if (!look.isObject())
            return;
        auto parseARGB = [&look](const char* key, COLORREF fallback) -> COLORREF {
            const Json v = look[key];
            if (!v.isString())
                return fallback;
            const std::string s = v.toString();
            if (s.size() != 9 || s[0] != '#')
                return fallback;
            const auto hex = [&s](size_t i) -> int {
                const char c = s[i];
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            // #AARRGGBB: skip alpha, take RRGGBB.
            return RGB(hex(3) * 16 + hex(4), hex(5) * 16 + hex(6), hex(7) * 16 + hex(8));
        };
        cw::Overlay::Colors colors;
        colors.accent = parseARGB("accent", colors.accent);
        colors.text = parseARGB("text", colors.text);
        colors.background = parseARGB("background", colors.background);
        colors.dark = theme() == "dark";
        m_overlay.setColors(colors);
    }

private:
    // --- persistence ---------------------------------------------------------

    void loadOverridesFromDisk()
    {
        const std::string text = readFileText(overridePath(dataDir()));
        if (text.empty()) {
            m_overrides = false;
            return;
        }
        bool valid = false;
        std::string problem;
        const cw::Settings overrides = cw::loadOverrides(text, valid, problem);
        if (!valid) {
            log("warn", "cursorwrap: ignoring settings.json: " + problem);
            m_overrides = false;
            return;
        }
        // Merge: only the keys the file carries were clamped into `overrides`;
        // the rest keep the host's values.
        cw::RawSettings raw;
        raw.wrapMode = modeText(overrides.wrapMode);
        raw.hasWrapMode = cw::overridesWrapMode(overrides);
        raw.activationMode = activationText(overrides.activation);
        raw.hasActivation = cw::overridesActivation(overrides);
        raw.impactGate = overrides.impactGate;
        raw.hasImpactGate = cw::overridesImpact(overrides);
        raw.impactSpeedPps = overrides.impactSpeedPps;
        raw.hasImpactSpeed = cw::overridesImpact(overrides);
        raw.wrapGuardPx = overrides.wrapGuardPx;
        raw.hasWrapGuard = overrides.wrapGuardPx != cw::Settings{}.wrapGuardPx;
        raw.edgeThresholdPx = overrides.edgeThresholdPx;
        raw.hasEdgeThreshold = overrides.edgeThresholdPx != cw::Settings{}.edgeThresholdPx;
        raw.adjacencyTolerancePx = overrides.adjacencyTolerancePx;
        raw.hasAdjacency = overrides.adjacencyTolerancePx != cw::Settings{}.adjacencyTolerancePx;
        applySettings(raw, m_settings, true);
        m_overrides = raw.hasWrapMode || raw.hasActivation || raw.hasImpactGate
            || raw.hasImpactSpeed || raw.hasWrapGuard || raw.hasEdgeThreshold
            || raw.hasAdjacency;
    }

    void persistOverrides()
    {
        const std::string json = cw::dumpOverrides(m_settings);
        if (json.find("\"settings\":{}") != std::string::npos) {
            // Everything back at the defaults: the host card is authoritative.
            deleteFile(overridePath(dataDir()));
            m_overrides = false;
            return;
        }
        m_overrides = writeFileText(overridePath(dataDir()), json);
        if (!m_overrides)
            log("warn", "cursorwrap: could not write settings.json");
    }

    // --- search-bar commands --------------------------------------------------

    void applyCommand(const cw::ParsedCommand& cmd)
    {
        switch (cmd.kind) {
        case cw::CommandKind::SetImpact:
            m_settings.impactGate = cmd.value != 0;
            break;
        case cw::CommandKind::SetImpactSpeed:
            m_settings.impactGate = true; // setting a speed implies wanting the gate
            m_settings.impactSpeedPps = static_cast<int>(cmd.value);
            break;
        case cw::CommandKind::SetBand:
            m_settings.impactBandPx = static_cast<int>(cmd.value);
            break;
        case cw::CommandKind::SetMode:
            m_settings.wrapMode = static_cast<cw::WrapMode>(cmd.value);
            break;
        case cw::CommandKind::SetActivation:
            m_settings.activation = static_cast<cw::ActivationMode>(cmd.value);
            break;
        case cw::CommandKind::SetGuard:
            m_settings.wrapGuardPx = static_cast<int>(cmd.value);
            break;
        case cw::CommandKind::SetEdge:
            m_settings.edgeThresholdPx = static_cast<int>(cmd.value);
            break;
        case cw::CommandKind::SetTolerance:
            m_settings.adjacencyTolerancePx = static_cast<int>(cmd.value);
            break;
        case cw::CommandKind::SetDrag:
            m_settings.disableDuringDrag = cmd.value != 0;
            break;
        case cw::CommandKind::SetSingle:
            m_settings.disableOnSingleMonitor = cmd.value != 0;
            break;
        case cw::CommandKind::SetOverlay:
            m_tunerOn = cmd.value != 0;
            if (m_tunerOn && m_engine)
                m_overlay.start(m_engine->monitorsSnapshot());
            else
                m_overlay.stop();
            break;
        case cw::CommandKind::ClearTrace:
            if (m_engine)
                m_engine->clearArrivals();
            break;
        case cw::CommandKind::CopyDiagnostics:
            // The copy effect only runs on action responses, so this query
            // returns a diagnostics row: its copy field carries the text,
            // and Enter on it runs the plugin's copyDiag action.
            break;
        case cw::CommandKind::ResetToDefaults: {
            const cw::Settings defaults;
            m_settings.wrapMode = defaults.wrapMode;
            m_settings.activation = defaults.activation;
            m_settings.impactGate = defaults.impactGate;
            m_settings.impactSpeedPps = defaults.impactSpeedPps;
            m_settings.impactBandPx = defaults.impactBandPx;
            m_settings.wrapGuardPx = defaults.wrapGuardPx;
            m_settings.edgeThresholdPx = defaults.edgeThresholdPx;
            m_settings.adjacencyTolerancePx = defaults.adjacencyTolerancePx;
            break;
        }
        default:
            break;
        }
        persistOverrides();
        pushSettings();
    }

    void cycleMode()
    {
        switch (m_settings.wrapMode) {
        case cw::WrapMode::Both:
            m_settings.wrapMode = cw::WrapMode::VerticalOnly;
            break;
        case cw::WrapMode::VerticalOnly:
            m_settings.wrapMode = cw::WrapMode::HorizontalOnly;
            break;
        case cw::WrapMode::HorizontalOnly:
            m_settings.wrapMode = cw::WrapMode::Both;
            break;
        }
    }

    std::string impactNotifyText() const
    {
        const bool chinese = winshun::Plugin::chinese();
        if (!m_settings.impactGate)
            return chinese ? "撞击模式：关" : "Impact mode: off";
        return std::string(chinese ? "撞击模式：开，门槛 " : "Impact mode: on, gate ")
            + intText(m_settings.impactSpeedPps) + " px/s";
    }

    Item unknownRow(const std::string& text)
    {
        const bool chinese = winshun::Plugin::chinese();
        Item row("cursorwrap.unknown",
                 chinese ? "不认识的词：" + text : "Unknown words: " + text,
                 chinese ? "试试：best 最优 · trace 轨迹 · tuner 调参 · impact 撞击 · "
                         "speed 速度 · band 区间 · mode 模式 · drag 拖拽 · single 单屏 · "
                         "activation 激活 · guard 防震 · edge 边缘 · tol 容差 · diag 诊断"
                         : "try: best · trace · tuner · impact · speed · band · mode · "
                           "drag · single · activation · guard · edge · tol · diag");
        row.invalid();
        return row;
    }

    // The `cursorwrap diag` row: the diagnostics text rides in the copy
    // field, Enter runs the plugin's copyDiag action (the copy effect only
    // applies on action responses, never on query responses).
    Item diagnosticsRow()
    {
        const bool chinese = winshun::Plugin::chinese();
        Item row("cursorwrap.diag",
                 chinese ? "光标回绕：诊断信息" : "Cursor wrap: diagnostics",
                 chinese ? "Enter 复制到剪贴板" : "Press Enter to copy");
        row.withCopy(diagnosticsText());
        row.withActions({ {"copyDiagRow", chinese ? "复制诊断" : "Copy diagnostics", "Enter"} });
        return row;
    }

    // --- engine plumbing -------------------------------------------------------

    void setEnabled(bool on)
    {
        m_hookEnabled = on;
        if (m_hook) {
            m_hook->setEnabled(on);
            m_engine->resetState();
        }
    }

    void pushSettings()
    {
        if (m_engine)
            m_engine->setSettings(m_settings);
        if (m_hook)
            m_hook->updateSettings(m_settings);
    }

    std::string sourceNote(bool chinese) const
    {
        if (!m_overrides)
            return {};
        return chinese ? " · 搜索栏覆盖中（reset 恢复宿主设置）"
                       : " · search-bar overrides active (reset to fall back)";
    }

    Item statusRow()
    {
        const bool chinese = winshun::Plugin::chinese();
        const cw::Diagnostics d = m_engine ? m_engine->diagnostics(m_hookOk)
                                           : cw::Diagnostics {};
        if (!m_hookEnabled) {
            Item row("cursorwrap.status",
                     chinese ? "光标回绕：已关闭" : "Cursor wrap: off",
                     chinese ? "Enter 开启 · 下方逐项设置" : "Enter to enable · per-setting rows below");
            row.withActions({ {"toggleEnabled", chinese ? "开启" : "Enable", "Enter"} });
            return row;
        }
        std::string subtitle = std::string(chinese ? "模式 " : "mode ")
            + modeLabel(m_settings.wrapMode, chinese)
            + std::string(chinese ? " · 激活 " : " · activation ")
            + activationLabel(m_settings.activation, chinese)
            + std::string(chinese ? " · 撞击" : " · impact")
            + (m_settings.impactGate
                   ? std::string(chinese ? "开 " : " on ") + intText(m_settings.impactSpeedPps)
                   : std::string(chinese ? "关" : " off"))
            + std::string(chinese ? " · " : " · ") + intText(d.monitors)
            + std::string(chinese ? " 块屏" : " display(s)")
            + sourceNote(chinese);
        if (!d.hookOk)
            subtitle += chinese ? " · 钩子未安装，仅显示状态" : " · hook failed, status only";
        Item row("cursorwrap.status", chinese ? "光标回绕：已开启" : "Cursor wrap: on",
                 subtitle);
        row.withActions({ {"toggleEnabled", chinese ? "关闭" : "Disable", "Enter"} });
        return row;
    }

    // --- the settings list and its submenus ---------------------------------
    //
    // One row per setting: Enter acts on the row itself (a toggle, a value
    // pick or a drill-down via setQuery), which keeps every key binding
    // within the host's three-keys-per-row limit. A submenu's first row goes
    // back to the list (setQuery "cursorwrap "), then every choice is its
    // own row.

    Effects listEffects(Json extra)
    {
        // Refresh the list after an action; `extra` (a notify) rides along.
        return { effect::replace(settingsList()), effect::keep(), std::move(extra) };
    }

    // A drill-down row: Enter fills the search box, which re-queries and
    // shows the submenu. No action needed — the host treats setQuery as the
    // row's default Enter.
    Item drillRow(const char* id, std::string title, std::string subtitle, const char* query)
    {
        Item row(id, std::move(title), std::move(subtitle));
        row.withSetQuery(query);
        return row;
    }

    Item backRow()
    {
        return drillRow("cursorwrap.back",
                        winshun::Plugin::chinese() ? "← 返回" : "← Back",
                        {}, "cursorwrap ");
    }

    // A value row inside a numeric submenu: Enter applies set:<field>:<v>.
    Item valueRow(const char* id, std::string title, const char* field, long long value,
                  bool current)
    {
        const bool chinese = winshun::Plugin::chinese();
        Item row(id, std::move(title),
                 current ? std::string(chinese ? "当前" : "current") : std::string());
        row.withActions({ { "set:" + std::string(field) + ":" + intText(value),
                            chinese ? "设为该值" : "Set", "Enter" } });
        return row;
    }

    Items settingsList()
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(statusRow());

        rows.push_back(Item("cursorwrap.row.impact",
                            std::string(chinese ? "撞击模式：" : "Impact mode: ")
                                + (m_settings.impactGate
                                       ? (chinese ? "开" : "on")
                                       : (chinese ? "关" : "off")),
                            chinese ? "Enter 切换 · 慢慢挪到边缘不回绕"
                                    : "Enter to toggle · drifting to the edge does not wrap"));
        rows.back().withActions({ {"toggleImpact", chinese ? "切换" : "Toggle", "Enter"} });

        rows.push_back(drillRow("cursorwrap.row.speed",
                                std::string(chinese ? "撞击速度：" : "Impact speed: ")
                                    + intText(m_settings.impactSpeedPps) + " px/s",
                                chinese ? "Enter 选择门槛值" : "Enter to pick a threshold",
                                "cursorwrap speed "));

        rows.push_back(drillRow("cursorwrap.row.band",
                                std::string(chinese ? "撞击测速区间：" : "Impact band: ")
                                    + intText(m_settings.impactBandPx) + " px",
                                chinese ? "Enter 选择区间" : "Enter to pick a band",
                                "cursorwrap band "));

        rows.push_back(Item("cursorwrap.row.mode",
                            std::string(chinese ? "回绕模式：" : "Wrap mode: ")
                                + modeLabel(m_settings.wrapMode, chinese),
                            chinese ? "Enter 切换 双向→仅上下→仅左右"
                                    : "Enter to cycle both→vertical→horizontal"));
        rows.back().withActions({ {"cycleMode", chinese ? "切换" : "Cycle", "Enter"} });

        rows.push_back(drillRow("cursorwrap.row.activation",
                                std::string(chinese ? "激活方式：" : "Activation: ")
                                    + activationLabel(m_settings.activation, chinese),
                                chinese ? "Enter 选择激活方式" : "Enter to pick one",
                                "cursorwrap activation "));

        rows.push_back(drillRow("cursorwrap.row.guard",
                                std::string(chinese ? "防震半径：" : "Wrap guard: ")
                                    + intText(m_settings.wrapGuardPx) + " px",
                                chinese ? "Enter 选择半径" : "Enter to pick a radius",
                                "cursorwrap guard "));

        rows.push_back(drillRow("cursorwrap.row.edge",
                                std::string(chinese ? "边缘阈值：" : "Edge threshold: ")
                                    + intText(m_settings.edgeThresholdPx) + " px",
                                chinese ? "Enter 选择阈值" : "Enter to pick one",
                                "cursorwrap edge "));

        rows.push_back(drillRow("cursorwrap.row.tol",
                                std::string(chinese ? "内边容差：" : "Adjacency tolerance: ")
                                    + intText(m_settings.adjacencyTolerancePx) + " px",
                                chinese ? "Enter 选择容差" : "Enter to pick one",
                                "cursorwrap tol "));

        rows.push_back(drillRow("cursorwrap.row.drag",
                                std::string(chinese ? "拖拽时不回绕：" : "No wrap while dragging: ")
                                    + (m_settings.disableDuringDrag ? (chinese ? "开" : "on")
                                                                    : (chinese ? "关" : "off")),
                                chinese ? "Enter 选择开关" : "Enter to pick on or off",
                                "cursorwrap drag "));

        rows.push_back(drillRow("cursorwrap.row.single",
                                std::string(chinese ? "单屏时禁用：" : "Disable on one monitor: ")
                                    + (m_settings.disableOnSingleMonitor ? (chinese ? "开" : "on")
                                                                         : (chinese ? "关" : "off")),
                                chinese ? "Enter 选择开关" : "Enter to pick on or off",
                                "cursorwrap single "));

        rows.push_back(drillRow("cursorwrap.row.tuner",
                                std::string(chinese ? "调参可视化：" : "Tuning overlay: ")
                                    + (m_tunerOn ? (chinese ? "开" : "on") : (chinese ? "关" : "off")),
                                chinese ? "Enter 打开子菜单（开关 / 区间 / 门槛 / 防震 / 阈值）"
                                        : "Enter for the submenu (switch, band, gate, guard, threshold)",
                                "cursorwrap tune "));

        rows.push_back(drillRow("cursorwrap.row.best",
                                std::string(chinese ? "建议门槛：" : "Suggested gate: ")
                                    + bestSummary(chinese),
                                chinese ? "Enter 看建议（由到达速度分布算出）"
                                        : "Enter to see the suggestion (derived from the arrival speeds)",
                                "cursorwrap best "));

        rows.push_back(traceRow());

        rows.push_back(diagnosticsRow());

        Item reset("cursorwrap.row.reset",
                   chinese ? "恢复宿主设置卡" : "Restore host settings",
                   chinese ? "Enter 清除搜索栏覆盖" : "Enter to clear search-bar overrides");
        reset.withActions({ {"resetDefaults", chinese ? "恢复" : "Reset", "Enter"} });
        rows.push_back(std::move(reset));
        return rows;
    }

    Items impactMenu()
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(backRow());
        rows.push_back(Item("cursorwrap.impact.state",
                            std::string(chinese ? "撞击模式：" : "Impact mode: ")
                                + (m_settings.impactGate
                                       ? (chinese ? "开" : "on")
                                       : (chinese ? "关" : "off")),
                            chinese ? "Enter 切换" : "Enter to toggle"));
        rows.back().withActions({ {"toggleImpact", chinese ? "切换" : "Toggle", "Enter"} });
        rows.push_back(drillRow("cursorwrap.impact.speed",
                                std::string(chinese ? "撞击速度：" : "Impact speed: ")
                                    + intText(m_settings.impactSpeedPps) + " px/s",
                                chinese ? "Enter 选择门槛值" : "Enter to pick a threshold",
                                "cursorwrap speed "));
        rows.push_back(drillRow("cursorwrap.impact.band",
                                std::string(chinese ? "撞击测速区间：" : "Impact band: ")
                                    + intText(m_settings.impactBandPx) + " px",
                                chinese ? "Enter 选择区间" : "Enter to pick a band",
                                "cursorwrap band "));
        return rows;
    }

    Items speedMenu()
    {
        Items rows;
        rows.push_back(backRow());
        for (const long long v : { 500, 1000, 2000, 3000, 5000, 8000, 12000, 20000 }) {
            std::string title = intText(v) + " px/s";
            rows.push_back(valueRow("cursorwrap.speed", std::move(title), "speed", v,
                                    m_settings.impactSpeedPps == v));
        }
        return rows;
    }

    Items bandMenu()
    {
        Items rows;
        rows.push_back(backRow());
        for (const long long v : { 50, 80, 120, 150, 200, 300, 500 }) {
            std::string title = intText(v) + " px";
            rows.push_back(valueRow("cursorwrap.band", std::move(title), "band", v,
                                    m_settings.impactBandPx == v));
        }
        return rows;
    }

    Items modeMenu()
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(backRow());
        const std::pair<cw::WrapMode, const char*> modes[] = {
            { cw::WrapMode::Both, chinese ? "双向" : "both" },
            { cw::WrapMode::VerticalOnly, chinese ? "仅上下" : "vertical only" },
            { cw::WrapMode::HorizontalOnly, chinese ? "仅左右" : "horizontal only" },
        };
        for (const auto& [mode, label] : modes) {
            const bool current = m_settings.wrapMode == mode;
            Item row("cursorwrap.mode", label,
                     current ? std::string(chinese ? "当前" : "current")
                             : std::string(chinese ? "Enter 切换到该模式" : "Enter to switch"));
            if (!current) {
                row.withActions({ { "set:mode:" + intText(static_cast<long long>(mode)),
                                    chinese ? "切换" : "Switch", "Enter" } });
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    Items activationMenu()
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(backRow());
        const cw::ActivationMode kinds[] = { cw::ActivationMode::Always,
            cw::ActivationMode::WhileCtrl, cw::ActivationMode::WhileShift };
        const char* actions[] = { "setActivationAlways", "setActivationCtrl", "setActivationShift" };
        for (size_t i = 0; i < 3; ++i) {
            Item row("cursorwrap.activation", activationLabel(kinds[i], chinese),
                     m_settings.activation == kinds[i]
                         ? std::string(chinese ? "当前" : "current")
                         : std::string(chinese ? "Enter 切换" : "Enter to switch"));
            if (m_settings.activation != kinds[i])
                row.withActions({ { actions[i], chinese ? "选择" : "Pick", "Enter" } });
            rows.push_back(std::move(row));
        }
        return rows;
    }

    Items guardMenu()
    {
        Items rows;
        rows.push_back(backRow());
        for (const long long v : { 10, 30, 50, 80, 120, 200 }) {
            std::string title = intText(v) + " px";
            rows.push_back(valueRow("cursorwrap.guard", std::move(title), "guard", v,
                                    m_settings.wrapGuardPx == v));
        }
        return rows;
    }

    Items edgeMenu()
    {
        Items rows;
        rows.push_back(backRow());
        for (const long long v : { 1, 2, 5, 10, 20 }) {
            std::string title = intText(v) + " px";
            rows.push_back(valueRow("cursorwrap.edge", std::move(title), "edge", v,
                                    m_settings.edgeThresholdPx == v));
        }
        return rows;
    }

    Items toleranceMenu()
    {
        Items rows;
        rows.push_back(backRow());
        for (const long long v : { 10, 30, 50, 80, 120, 200 }) {
            std::string title = intText(v) + " px";
            rows.push_back(valueRow("cursorwrap.tol", std::move(title), "tol", v,
                                    m_settings.adjacencyTolerancePx == v));
        }
        return rows;
    }

    // A two-way row (on/off) for a boolean setting that has no dedicated
    // action: Enter applies set:<field>:<0|1>.
    Items boolMenu(const char* id, std::string title, std::string hint, bool current,
                   const char* field)
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(backRow());
        const std::pair<bool, std::string> choices[] = {
            { true, chinese ? "开" : "on" },
            { false, chinese ? "关" : "off" },
        };
        for (const auto& [value, label] : choices) {
            Item row(id, label, current == value
                                    ? std::string(chinese ? "当前" : "current")
                                    : std::string(chinese ? "Enter 切换" : "Enter to switch"));
            if (current != value)
                row.withActions({ { "set:" + std::string(field) + ":" + intText(value ? 1 : 0),
                                    chinese ? "切换" : "Switch", "Enter" } });
            rows.push_back(std::move(row));
        }
        (void)title;
        (void)hint;
        return rows;
    }

    // The tuning overlay's own submenu: its switch plus the numbers the
    // overlay draws, so the user can set what they are looking at without
    // leaving the tuning screen.
    Items tunerMenu()
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(backRow());

        Item state("cursorwrap.tuner.state",
                   std::string(chinese ? "调参可视化：" : "Tuning overlay: ")
                       + (m_tunerOn ? (chinese ? "开" : "on") : (chinese ? "关" : "off")),
                   chinese ? "Enter 开关 · 只在能回绕的外边缘画线和测速区间"
                           : "Enter to toggle · draws the line and measuring band on outer edges only");
        state.withActions({ {"toggleTuner", chinese ? "切换" : "Toggle", "Enter"} });
        rows.push_back(std::move(state));

        rows.push_back(drillRow("cursorwrap.tuner.band",
                                std::string(chinese ? "测速区间：" : "Measuring band: ")
                                    + intText(m_settings.impactBandPx) + " px",
                                chinese ? "Enter 选择区间宽度（画出来的带宽一致）"
                                        : "Enter to pick the band width (drawn at the same size)",
                                "cursorwrap band "));
        rows.push_back(drillRow("cursorwrap.tuner.speed",
                                std::string(chinese ? "速度门槛：" : "Speed gate: ")
                                    + intText(m_settings.impactSpeedPps) + " px/s",
                                chinese ? "Enter 选择门槛值（读数里的分母）"
                                        : "Enter to pick the gate (the readout's denominator)",
                                "cursorwrap speed "));
        rows.push_back(drillRow("cursorwrap.tuner.guard",
                                std::string(chinese ? "防震半径：" : "Guard radius: ")
                                    + intText(m_settings.wrapGuardPx) + " px",
                                chinese ? "Enter 选择半径（回绕后这段时间不重复触发）"
                                        : "Enter to pick the radius (no re-wrap inside it)",
                                "cursorwrap guard "));
        rows.push_back(drillRow("cursorwrap.tuner.edge",
                                std::string(chinese ? "边缘阈值：" : "Edge threshold: ")
                                    + intText(m_settings.edgeThresholdPx) + " px",
                                chinese ? "Enter 选择阈值（离边缘多近开始判定）"
                                        : "Enter to pick how close counts as on the edge",
                                "cursorwrap edge "));
        return rows;
    }

    // The `cursorwrap trace` row: the arrival trace as CSV rides in the copy
    // field, Enter runs the copyTrace action for the flash.
    Item traceRow()
    {
        const bool chinese = winshun::Plugin::chinese();
        const size_t count = m_engine ? m_engine->arrivals().size() : 0;
        Item row("cursorwrap.trace",
                 chinese ? "光标回绕：到达轨迹" : "Cursor wrap: arrival trace",
                 chinese ? "Enter 复制 CSV（" + intText(static_cast<long long>(count))
                         + " 条）· 可离线扫出最优门槛"
                         : "Press Enter to copy CSV (" + intText(static_cast<long long>(count))
                               + " rows) · scan for the optimal gate offline");
        row.withCopy(traceCsv());
        row.withActions({ { "copyTrace", chinese ? "复制轨迹" : "Copy trace", "Enter" } });
        return row;
    }

    std::string bestSummary(bool chinese) const
    {
        const std::vector<cw::Arrival> trace = m_engine ? m_engine->arrivals()
                                                        : std::vector<cw::Arrival> {};
        const long long suggested = cw::suggestGate(trace);
        if (suggested <= 0)
            return chinese ? "数据不足" : "not enough data";
        return intText(suggested) + " px/s";
    }

    std::string traceCsv() const
    {
        std::string csv = "seq,t_us,monitor,edge,measured_pps,gate_pps,wrapped,gate_off\n";
        if (!m_engine)
            return csv;
        for (const cw::Arrival& a : m_engine->arrivals()) {
            const char* edge = a.edge == cw::EdgeType::Left    ? "left"
                : a.edge == cw::EdgeType::Right                ? "right"
                : a.edge == cw::EdgeType::Top                  ? "top"
                                                               : "bottom";
            csv += intText(a.seq) + "," + intText(a.tUs) + "," + intText(a.monitorIndex) + ","
                + edge + "," + intText(a.measuredPps) + "," + intText(a.gatePps) + ","
                + (a.wrapped ? "1" : "0") + "," + (a.gateOff ? "1" : "0") + "\n";
        }
        return csv;
    }

    // The `cursorwrap best` submenu: the gate the trace suggests. When the
    // trace has no clear gap it says so instead of inventing a number.
    Items bestMenu()
    {
        const bool chinese = winshun::Plugin::chinese();
        Items rows;
        rows.push_back(backRow());
        const std::vector<cw::Arrival> trace = m_engine ? m_engine->arrivals() : std::vector<cw::Arrival> {};
        const long long suggested = cw::suggestGate(trace);        if (suggested <= 0) {
            Item row("cursorwrap.best.none",
                     chinese ? "还没有足够数据" : "Not enough data yet",
                     chinese ? "先甩几次边缘、再慢慢挪几次，然后回来看"
                             : "push an edge a few times and drift to it a few times, then come back");
            row.invalid();
            rows.push_back(std::move(row));
            return rows;
        }
        Item head("cursorwrap.best.value",
                  std::string(chinese ? "建议门槛：" : "Suggested gate: ")
                      + intText(suggested) + " px/s",
                  std::string(chinese ? "当前 " : "current ") + intText(m_settings.impactSpeedPps)
                      + std::string(chinese ? " px/s · 取自到达速度分布里的空档" : " px/s · the gap in the arrival-speed distribution"));
        rows.push_back(std::move(head));
        if (suggested != m_settings.impactSpeedPps) {
            Item apply("cursorwrap.best.apply", chinese ? "采用这个门槛" : "Use this gate",
                       chinese ? "Enter 写入并立即生效" : "Enter to apply immediately");
            apply.withActions({ { "set:speed:" + intText(suggested),
                                  chinese ? "采用" : "Apply", "Enter" } });
            rows.push_back(std::move(apply));
        }
        rows.push_back(drillRow("cursorwrap.best.trace",
                                chinese ? "查看数据" : "See the data",
                                chinese ? "Enter 复制 CSV 离线分析" : "Enter to copy the CSV",
                                "cursorwrap trace "));
        return rows;
    }

    std::string diagnosticsText() const
    {
        const cw::Diagnostics d = m_engine ? m_engine->diagnostics(m_hookOk)
                                           : cw::Diagnostics {};
        std::string text = "ra9fael.cursorwrap diagnostics\n";
        text += "enabled: " + std::string(m_hookEnabled ? "true" : "false") + "\n";
        text += "hook installed: " + std::string(d.hookOk ? "true" : "false") + "\n";
        text += "watcher started: " + std::string(m_watcherOk ? "true" : "false") + "\n";
        text += "search-bar overrides: " + std::string(m_overrides ? "true" : "false") + "\n";
        text += "wrap mode: " + modeLabel(m_settings.wrapMode, false) + "\n";
        text += "activation: " + activationLabel(m_settings.activation, false) + "\n";
        text += "monitors: " + intText(d.monitors) + "\n";
        if (m_engine) {
            const auto monitors = m_engine->monitorsSnapshot();
            for (size_t i = 0; i < monitors.size(); ++i) {
                const auto& monitor = monitors[i];
                text += "display " + intText(static_cast<long long>(i)) + ": ["
                    + intText(monitor.left) + "," + intText(monitor.top) + ".."
                    + intText(monitor.right) + "," + intText(monitor.bottom) + "]\n";
            }
        }
        text += "outer edges: " + intText(d.outerEdges) + "\n";
        text += "moves: " + intText(d.stats.moves) + "\n";
        text += "wraps: " + intText(d.stats.wraps) + "\n";
        text += "suppressed: " + intText(d.stats.suppressed) + "\n";
        text += "rebuilds: " + intText(d.stats.rebuilt) + "\n";
        text += "edge threshold: " + intText(d.settings.edgeThresholdPx) + " px\n";
        text += "wrap guard: " + intText(d.settings.wrapGuardPx) + " px\n";
        text += "adjacency tolerance: " + intText(d.settings.adjacencyTolerancePx) + " px\n";
        text += std::string("impact gate: ") + (d.settings.impactGate ? "on" : "off") + "\n";
        if (d.settings.impactGate) {
            text += "impact speed: " + intText(d.settings.impactSpeedPps) + " px/s\n";
            text += "impact band: " + intText(d.settings.impactBandPx) + " px\n";
        }
        for (const std::string& gap : d.gaps)
            text += "gap: " + gap + "\n";
        return text;
    }

    cw::Settings m_settings;
    std::unique_ptr<cw::WrapEngine> m_engine;
    std::unique_ptr<cw::MouseHook> m_hook;
    std::unique_ptr<cw::MonitorWatcher> m_watcher;
    cw::Overlay m_overlay; // the tuning overlay (off until the user turns it on)
    bool m_tunerOn = false;
    bool m_hookOk = false;
    bool m_watcherOk = false;
    bool m_hookEnabled = true;
    bool m_overrides = false;
};

int main()
{
    // Keep EnumDisplayMonitors, low-level hook coordinates, and SetCursorPos
    // in the same physical-pixel coordinate space on mixed-DPI desktops.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CursorWrapPlugin plugin;
    return plugin.serve();
}
