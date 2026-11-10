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
#include "query_parser.h"
#include "settings_store.h"
#include "wrap_logic.h"

#include <cstdio>
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
            }
        });

        log(hookOk && m_watcherOk ? "info" : "warn",
            std::string("cursorwrap: hook ") + (hookOk ? "installed" : "FAILED")
                + ", watcher " + (m_watcherOk ? "started" : "FAILED")
                + ", " + intText(static_cast<long long>(m_engine->monitorCount()))
                + " monitor(s)"
                + (m_overrides ? ", overrides loaded" : ""));
    }

    Items query(const Query& q) override
    {
        const cw::ParsedCommand cmd = cw::parseCommand(q.text);
        switch (cmd.kind) {
        case cw::CommandKind::Show:
            return { statusRow() };
        case cw::CommandKind::Unknown:
            return { unknownRow(q.text) };
        default: {
            // A setting change: apply it, persist it, show the new state.
            applyCommand(cmd);
            return { statusRow() };
        }
        }
    }

    Effects action(const ActionRequest& a) override
    {
        const bool chinese = winshun::Plugin::chinese();
        if (a.action == "toggleEnabled") {
            const bool on = !m_hookEnabled;
            setEnabled(on);
            return { effect::replace({ statusRow() }), effect::keep(),
                     effect::notify(on ? (chinese ? "光标回绕已开启" : "Cursor wrap on")
                                       : (chinese ? "光标回绕已关闭" : "Cursor wrap off")) };
        }
        if (a.action == "cycleMode") {
            cycleMode();
            persistOverrides();
            pushSettings();
            return { effect::replace({ statusRow() }), effect::keep(),
                     effect::notify(std::string(chinese ? "回绕模式：" : "Wrap mode: ")
                                    + modeLabel(m_settings.wrapMode, chinese)) };
        }
        if (a.action == "toggleImpact") {
            m_settings.impactGate = !m_settings.impactGate;
            persistOverrides();
            pushSettings();
            return { effect::replace({ statusRow() }), effect::keep(),
                     effect::notify(impactNotifyText()) };
        }
        if (a.action == "copyDiagnostics") {
            return { effect::copy(diagnosticsText()), effect::keep(),
                     effect::notify(chinese ? "诊断已复制" : "Diagnostics copied") };
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
        case cw::CommandKind::ToggleImpact:
            m_settings.impactGate = !m_settings.impactGate;
            break;
        case cw::CommandKind::SetImpact:
            m_settings.impactGate = cmd.value != 0;
            break;
        case cw::CommandKind::SetImpactSpeed:
            m_settings.impactGate = true; // setting a speed implies wanting the gate
            m_settings.impactSpeedPps = static_cast<int>(cmd.value);
            break;
        case cw::CommandKind::CycleMode:
            cycleMode();
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
        case cw::CommandKind::ResetToDefaults: {
            const cw::Settings defaults;
            m_settings.wrapMode = defaults.wrapMode;
            m_settings.activation = defaults.activation;
            m_settings.impactGate = defaults.impactGate;
            m_settings.impactSpeedPps = defaults.impactSpeedPps;
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
                 chinese ? "试试：impact 撞击 · speed 3000 速度 · mode 模式 · ctrl/shift/always · "
                         "guard 防震 · edge 边缘 · tol 容差 · reset 恢复"
                         : "try: impact · speed 3000 · mode · ctrl/shift/always · guard · "
                         "edge · tol · reset");
        row.invalid();
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
                     chinese ? "按 Enter 开启" : "Press Enter to enable");
            row.withActions({ {"toggleEnabled", chinese ? "开启" : "Enable", "Enter"},
                              {"cycleMode", chinese ? "切换模式" : "Cycle mode", "Ctrl+Enter"},
                              {"toggleImpact", chinese ? "切换撞击" : "Toggle impact", "Shift+Enter"},
                              {"copyDiagnostics", chinese ? "复制诊断" : "Copy diagnostics", "Ctrl+Enter"} });
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
        row.withActions({ {"toggleEnabled", chinese ? "关闭" : "Disable", "Enter"},
                          {"cycleMode", chinese ? "切换模式" : "Cycle mode", "Ctrl+Enter"},
                          {"toggleImpact", chinese ? "切换撞击" : "Toggle impact", "Shift+Enter"},
                          {"copyDiagnostics", chinese ? "复制诊断" : "Copy diagnostics", "Ctrl+Enter"} });
        return row;
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
        text += "outer edges: " + intText(d.outerEdges) + "\n";
        text += "moves: " + intText(d.stats.moves) + "\n";
        text += "wraps: " + intText(d.stats.wraps) + "\n";
        text += "suppressed: " + intText(d.stats.suppressed) + "\n";
        text += "rebuilds: " + intText(d.stats.rebuilt) + "\n";
        text += "edge threshold: " + intText(d.settings.edgeThresholdPx) + " px\n";
        text += "wrap guard: " + intText(d.settings.wrapGuardPx) + " px\n";
        text += "adjacency tolerance: " + intText(d.settings.adjacencyTolerancePx) + " px\n";
        text += std::string("impact gate: ") + (d.settings.impactGate ? "on" : "off") + "\n";
        if (d.settings.impactGate)
            text += "impact speed: " + intText(d.settings.impactSpeedPps) + " px/s\n";
        for (const std::string& gap : d.gaps)
            text += "gap: " + gap + "\n";
        return text;
    }

    cw::Settings m_settings;
    std::unique_ptr<cw::WrapEngine> m_engine;
    std::unique_ptr<cw::MouseHook> m_hook;
    std::unique_ptr<cw::MonitorWatcher> m_watcher;
    bool m_hookOk = false;
    bool m_watcherOk = false;
    bool m_hookEnabled = true;
    bool m_overrides = false;
};

int main()
{
    CursorWrapPlugin plugin;
    return plugin.serve();
}
