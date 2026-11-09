// main.cpp — WinShunCursorWrap: a Win顺 resident plugin.
//
// Multi-monitor cursor wrapping: when the pointer reaches an outer edge of
// the monitor layout it re-enters from the nearest opposite edge (PowerToys
// CursorWrap's algorithm, re-implemented as a pure core so it unit-tests
// without a display). Inner edges — where two monitors touch — never wrap;
// Windows already handles monitor-to-monitor movement.
//
// Keyboard: the `cursorwrap` keyword shows the status row. Enter toggles the
// plugin, Ctrl+Enter cycles the wrap mode, Shift+Enter copies diagnostics.
// There are no windows, no tray and no hotkeys: the gesture is moving the
// mouse to a screen edge.
#include "winshun.h"

#include "monitor_watcher.h"
#include "mouse_hook.h"
#include "wrap_logic.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

} // namespace

class CursorWrapPlugin final : public winshun::Plugin {
public:
    std::string name() const override { return "ra9fael.cursorwrap"; }

protected:
    void initialized() override
    {
        applySettings(rawFromJson(settings()), m_settings, false);
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
                + ", " + intText(static_cast<long long>(m_engine->topology().monitorCount()))
                + " monitor(s)");
    }

    Items query(const Query& q) override
    {
        (void)q;
        return { statusRow() };
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
            pushSettings();
            return { effect::replace({ statusRow() }), effect::keep(),
                     effect::notify(std::string(chinese ? "回绕模式：" : "Wrap mode: ")
                                    + modeLabel(m_settings.wrapMode, chinese)) };
        }
        if (a.action == "copyDiagnostics") {
            return { effect::copy(diagnosticsText()), effect::keep(),
                     effect::notify(chinese ? "诊断已复制" : "Diagnostics copied") };
        }
        Item row("cursorwrap.error", chinese ? "未知动作" : "Unknown action",
                 a.action);
        row.invalid();
        return { effect::replace({ row }), effect::notify("unknown action: " + a.action) };
    }

    void settingsChanged(const Json& settings) override
    {
        applySettings(rawFromJson(settings), m_settings, true);
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
                              {"copyDiagnostics", chinese ? "复制诊断" : "Copy diagnostics", "Shift+Enter"} });
            return row;
        }
        std::string subtitle = std::string(chinese ? "模式 " : "mode ")
            + modeLabel(m_settings.wrapMode, chinese)
            + std::string(chinese ? " · 激活 " : " · activation ")
            + activationLabel(m_settings.activation, chinese)
            + std::string(chinese ? " · " : " · ") + intText(d.monitors)
            + std::string(chinese ? " 块屏 · " : " display(s) · ") + intText(d.outerEdges)
            + std::string(chinese ? " 条外边" : " outer edge(s)");
        if (!d.hookOk)
            subtitle += chinese ? " · 钩子未安装，仅显示状态" : " · hook failed, status only";
        Item row("cursorwrap.status", chinese ? "光标回绕：已开启" : "Cursor wrap: on",
                 subtitle);
        row.withActions({ {"toggleEnabled", chinese ? "关闭" : "Disable", "Enter"},
                          {"cycleMode", chinese ? "切换模式" : "Cycle mode", "Ctrl+Enter"},
                          {"copyDiagnostics", chinese ? "复制诊断" : "Copy diagnostics", "Shift+Enter"} });
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
};

int main()
{
    CursorWrapPlugin plugin;
    return plugin.serve();
}
