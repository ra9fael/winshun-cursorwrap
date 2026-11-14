// devshell.cpp — run the real engine, overlay and data window without Win顺.
//
// Why this exists
// ---------------
// Everything the plugin actually does at runtime can only be judged by running
// it: does the overlay draw on the right edges, does the histogram show what was
// measured, does a braking flick get refused, does the data window's X turn its
// own setting off. Inside Win顺 that needs the host, and driving it with a real
// mouse needs a desktop that accepts SetCursorPos — which a Remote Desktop
// session often refuses (`SetCursorPos` returns FALSE) and which the user's own
// hand invalidates.
//
// This shell links the SAME translation units the plugin ships:
//
//     wrap_topology.cpp  wrap_logic.cpp     the decision
//     overlay.cpp        data_window.cpp    the GUI
//     settings_store.cpp                    the persisted overrides
//
// and replaces only the OS input with a scripted sample stream. It is not a
// reimplementation and not a mock: a wrong pixel here is a wrong pixel in the
// shipped plugin.
//
// Usage
// -----
//   cw_devshell diag                  print the layout and the engine's edges
//   cw_devshell gui                   show both windows, drive by keyboard
//   cw_devshell run <scenario.txt>    replay a scenario and assert on it
//
// Scenario file: one command per line, `#` starts a comment.
//   monitor 0,0,1920,1080[;2560,0,4480,1080]   declare the layout
//   set <key> <value>                          a setting, through the real clamp
//   move <x> <y> [dtMs]                        deliver one sample
//   stroke <x0> <y0> <x1> <y1> <steps> <dtMs>  a straight stroke
//   sweep <x> <y> <dx> <steps> <dtMs>          a stroke that feeds its own result
//                                              back, so a wrap continues the motion
//   brake <x0> <v0> <v1> <right|left> <dtMs>   decelerating approach
//   hand <x> <v0> <v1> <rateHz> <maxMs>       a real mouse: linear speed ramp at
//                                              a real report rate, integer pixels
//   modifier ctrl|shift|none                   the activation key
//   drag on|off                                the left button
//   wait <ms>                                  let the windows repaint
//   show overlay|data|both|none                toggle the windows live
//   dump                                       print the engine state
//   expect wrap|no-wrap|speed>=N|speed<N       assert about the last move
//   expect at <x> <y>                          assert the exact pointer position
//
// The exit code is 0 when every expectation held.
#include "data_window.h"
#include "overlay.h"
#include "qpc_time.h"
#include "settings_store.h"
#include "wrap_logic.h"
#include "wrap_topology.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

using namespace cw;

namespace {

// --- the synthetic pointer --------------------------------------------------
//
// Drives the engine exactly as the low-level hook does: one sample in, the
// engine's decision out, and when it says "wrap" the pointer is moved there.
// Nothing moves on the real screen, so this works over RDP and never fights the
// user's own mouse.
class ScriptedPointer {
public:
    explicit ScriptedPointer(WrapEngine* engine) : m_engine(engine) {}

    std::pair<int64_t, int64_t> deliver(const Sample& sample)
    {
        ++m_delivered;
        const auto [nx, ny] = m_engine->onMouseMove(sample, m_modifier, m_leftButton);
        const bool wrapped = nx != sample.x || ny != sample.y;
        m_x = wrapped ? nx : sample.x;
        m_y = wrapped ? ny : sample.y;
        if (wrapped)
            ++m_wraps;
        m_trail.emplace_back(m_x, m_y);
        if (m_trail.size() > 512)
            m_trail.erase(m_trail.begin());
        return { m_x, m_y };
    }

    const std::vector<std::pair<int64_t, int64_t>>& trail() const { return m_trail; }
    void clearTrail() { m_trail.clear(); }

    int64_t x() const { return m_x; }
    int64_t y() const { return m_y; }
    long long delivered() const { return m_delivered; }
    long long wraps() const { return m_wraps; }
    void setModifier(bool on) { m_modifier = on; }
    void setLeftButton(bool on) { m_leftButton = on; }

private:
    WrapEngine* m_engine;
    int64_t m_x = 0;
    int64_t m_y = 0;
    long long m_delivered = 0;
    long long m_wraps = 0;
    bool m_modifier = false;
    bool m_leftButton = false;
    std::vector<std::pair<int64_t, int64_t>> m_trail;
};

// --- helpers ----------------------------------------------------------------

std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            out.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    out.push_back(trim(current));
    return out;
}

bool parseInt(const std::string& text, long long& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const long long v = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str())
        return false;
    while (end && *end == ' ')
        ++end;
    if (end && *end != '\0')
        return false;
    out = v;
    return true;
}

bool parseDouble(const std::string& text, double& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str())
        return false;
    out = v;
    return true;
}

// A monitor is "left,top,right,bottom" and may carry a WORK AREA as four more
// numbers: "left,top,right,bottom,workLeft,workTop,workRight,workBottom". The
// work rect is what the taskbar leaves over, and a scenario needs it to prove
// that a wrap never lands the cursor under the taskbar.
std::vector<MonitorBox> parseMonitors(const std::string& text)
{
    std::vector<MonitorBox> monitors;
    bool primarySet = false;
    for (const std::string& rect : split(text, ';')) {
        if (rect.empty())
            continue;
        const auto parts = split(rect, ',');
        if (parts.size() != 4 && parts.size() != 8)
            continue;
        long long v[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        bool ok = true;
        for (size_t i = 0; i < parts.size(); ++i)
            ok = ok && parseInt(parts[i], v[i]);
        if (!ok)
            continue;
        MonitorBox box;
        box.left = v[0];
        box.top = v[1];
        box.right = v[2];
        box.bottom = v[3];
        if (parts.size() == 8) {
            box.hasWorkArea = true;
            box.workLeft = v[4];
            box.workTop = v[5];
            box.workRight = v[6];
            box.workBottom = v[7];
        }
        box.dpi = 96;
        box.primary = !primarySet; // the first described is the primary
        primarySet = true;
        monitors.push_back(box);
    }
    return monitors;
}

// The real desktop, enumerated the way the plugin does it (with the RDP
// fallback for an empty enumeration).
std::vector<MonitorBox> realMonitors()
{
    std::vector<MonitorBox> monitors;
    ::EnumDisplayMonitors(nullptr, nullptr,
                          [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::vector<MonitorBox>*>(lp);
        MONITORINFO mi {};
        mi.cbSize = sizeof mi;
        if (::GetMonitorInfo(hMonitor, &mi)) {
            MonitorBox box;
            box.left = mi.rcMonitor.left;
            box.top = mi.rcMonitor.top;
            box.right = mi.rcMonitor.right;
            box.bottom = mi.rcMonitor.bottom;
            box.hasWorkArea = true;
            box.workLeft = mi.rcWork.left;
            box.workTop = mi.rcWork.top;
            box.workRight = mi.rcWork.right;
            box.workBottom = mi.rcWork.bottom;
            box.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            box.dpi = 96;
            out->push_back(box);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty()) {
        MonitorBox box;
        box.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
        box.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
        box.right = box.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
        box.bottom = box.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
        box.primary = true;
        box.dpi = 96;
        if (box.width() > 0 && box.height() > 0)
            monitors.push_back(box);
    }
    return monitors;
}

// The x of the nearest outer RIGHT edge, or -1 when the layout has none. `hand`
// drives toward it so a scenario does not have to hard-code a pixel that moves
// whenever the layout does.
int64_t edgeTargetX(const WrapEngine& engine)
{
    int64_t target = -1;
    for (const MonitorEdge& e : engine.outerEdgesSnapshot()) {
        if (e.type != EdgeType::Right)
            continue;
        if (target < 0 || e.position > target)
            target = e.position;
    }
    return target;
}

// The scenario clock's base: real QPC microseconds, the same clock the
// overlay's fade and freshness run on (see m_clockUs).
long long scenarioClockUs()
{
    LARGE_INTEGER now {}, freq {};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0
        || !QueryPerformanceCounter(&now))
        return 1'000'000;
    return cw::counterToUs(now.QuadPart, freq.QuadPart);
}

const char* edgeName(EdgeType edge)
{
    switch (edge) {
    case EdgeType::Left: return "left";
    case EdgeType::Right: return "right";
    case EdgeType::Top: return "top";
    case EdgeType::Bottom: return "bottom";
    }
    return "?";
}

// One "set <key> <value>" through the real clamp, so the shell cannot accept a
// value the plugin would reject.
bool applySetting(Settings& settings, const std::string& key, const std::string& value,
                  std::string& error)
{
    RawSettings raw;
    long long n = 0;
    const bool numeric = parseInt(value, n);
    const bool on = value == "on" || value == "true" || value == "1";
    if (key == "wrapMode" || key == "mode") {
        raw.wrapMode = value;
        raw.hasWrapMode = true;
    } else if (key == "activationMode" || key == "activation") {
        raw.activationMode = value;
        raw.hasActivation = true;
    } else if (key == "impactGate") {
        raw.impactGate = on;
        raw.hasImpactGate = true;
    } else if (key == "tuningOverlay") {
        raw.tuningOverlay = on;
        raw.hasTuningOverlay = true;
    } else if (key == "impactFlash") {
        raw.impactFlash = on;
        raw.hasImpactFlash = true;
    } else if (key == "dataWindow") {
        raw.dataWindow = on;
        raw.hasDataWindow = true;
    } else if (key == "disableDuringDrag") {
        raw.disableDuringDrag = on;
        raw.hasDrag = true;
    } else if (key == "disableOnSingleMonitor") {
        raw.disableOnSingleMonitor = on;
        raw.hasSingle = true;
    } else if (key == "impactSpeedPps" || key == "speed") {
        if (!numeric) { error = "not a number: " + value; return false; }
        raw.impactSpeedPps = static_cast<double>(n);
        raw.hasImpactSpeed = true;
    } else if (key == "impactBandPx" || key == "band") {
        if (!numeric) { error = "not a number: " + value; return false; }
        raw.impactBandPx = static_cast<double>(n);
        raw.hasImpactBand = true;
    } else if (key == "impactAccelPps2" || key == "accel") {
        if (!numeric) { error = "not a number: " + value; return false; }
        raw.impactAccelPps2 = static_cast<double>(n);
        raw.hasImpactAccel = true;
    } else if (key == "wrapGuardPx" || key == "guard") {
        if (!numeric) { error = "not a number: " + value; return false; }
        raw.wrapGuardPx = static_cast<double>(n);
        raw.hasWrapGuard = true;
    } else if (key == "edgeThresholdPx" || key == "edge") {
        if (!numeric) { error = "not a number: " + value; return false; }
        raw.edgeThresholdPx = static_cast<double>(n);
        raw.hasEdgeThreshold = true;
    } else if (key == "adjacencyTolerancePx" || key == "tol") {
        if (!numeric) { error = "not a number: " + value; return false; }
        raw.adjacencyTolerancePx = static_cast<double>(n);
        raw.hasAdjacency = true;
    } else {
        error = "unknown setting: " + key;
        return false;
    }
    applySettings(raw, settings, true);
    return true;
}

// --- the scenario runner ----------------------------------------------------

struct Expectation {
    std::string kind; // wrap | no-wrap | speed>=N | accel>=N | at X Y | x< | y< | flash
    long long value = 0;
    long long value2 = 0;
};

class Scenario {
public:
    Scenario(WrapEngine* engine, ScriptedPointer* pointer, Overlay* overlay,
             DataWindow* dataWindow)
        : m_engine(engine), m_pointer(pointer), m_overlay(overlay),
          m_dataWindow(dataWindow)
    {
    }

    bool run(const std::string& line);

    // Settings drive the windows, exactly as applyWindowSettings() does in the
    // plugin. Without this a scenario could set tuningOverlay=off and still see
    // the overlay, which would make any visual check meaningless.
    void syncWindows();

    int failures() const { return m_failures; }
    int checks() const { return m_checks; }
    const std::vector<std::string>& log() const { return m_log; }

private:
    void note(const std::string& text) { m_log.push_back(text); }
    void check(bool ok, const std::string& text);
    void settle(const std::string& line);
    void dumpState();
    DataSnapshot snapshot();

    WrapEngine* m_engine;
    ScriptedPointer* m_pointer;
    Overlay* m_overlay;
    DataWindow* m_dataWindow;
    std::vector<Expectation> m_expect;
    // A `flash` expectation can only be judged after the overlay repaints.
    bool m_flashWait = false;
    std::vector<std::string> m_log;
    int m_failures = 0;
    int m_checks = 0;
    long long m_lastSpeed = 0;
    long long m_lastAccel = 0;
    bool m_lastWrapped = false;
    // The scenario clock. It starts at the REAL QueryPerformanceCounter, not
    // at a small fictional base: the overlay's readout freshness compares the
    // verdict's tUs against the real clock, and with a fictional base every
    // verdict read "two seconds old" the moment it was made — the shell could
    // never see the readout even though the real machine does. Timestamps land
    // slightly in the future (a scripted hand compresses 400 ms of samples into
    // ~5 ms of real time), which the freshness check reads as fresh; `wait`
    // advances this clock so long scenarios stay roughly in sync.
    long long m_clockUs = scenarioClockUs();
};

void Scenario::check(bool ok, const std::string& text)
{
    ++m_checks;
    if (!ok) {
        ++m_failures;
        note("FAIL " + text);
    }
}

void Scenario::syncWindows()
{
    const Settings current = m_engine->settings();
    const bool overlayOn = current.tuningOverlay || (current.impactFlash && current.impactGate);
    if (overlayOn && !m_overlay->active())
        m_overlay->start(m_engine->monitorsSnapshot());
    else if (!overlayOn && m_overlay->active())
        m_overlay->stop();
    if (current.dataWindow && !m_dataWindow->active())
        m_dataWindow->start([this] { return snapshot(); });
    else if (!current.dataWindow && m_dataWindow->active())
        m_dataWindow->stop();
}

void Scenario::settle(const std::string& line)
{
    std::vector<Expectation> next;
    for (const Expectation& e : m_expect) {
        bool keep = false;
        if (e.kind == "wrap")
            check(m_lastWrapped, line + " -> expected a wrap, got none");
        else if (e.kind == "no-wrap")
            check(!m_lastWrapped, line + " -> expected no wrap, got one at " +
                                      std::to_string(m_pointer->x()) + "," +
                                      std::to_string(m_pointer->y()));
        else if (e.kind == "speed>=")
            check(m_lastSpeed >= e.value, line + " -> expected speed >= " +
                                              std::to_string(e.value) + ", measured " +
                                              std::to_string(m_lastSpeed));
        else if (e.kind == "speed<")
            check(m_lastSpeed < e.value, line + " -> expected speed < " +
                                             std::to_string(e.value) + ", measured " +
                                             std::to_string(m_lastSpeed));
        else if (e.kind == "accel>=")
            check(m_lastAccel >= e.value, line + " -> expected accel >= " +
                                              std::to_string(e.value) + ", measured " +
                                              std::to_string(m_lastAccel));
        else if (e.kind == "accel<")
            check(m_lastAccel < e.value, line + " -> expected accel < " +
                                             std::to_string(e.value) + ", measured " +
                                             std::to_string(m_lastAccel));
        else if (e.kind == "flash") {
            // The glow is driven by the overlay's 60 ms repaint, so it cannot be
            // observed at the instant the move is delivered: leave it queued and
            // judge it on the next settle (the scenario puts a `wait` after it).
            if (m_flashWait) {
                m_flashWait = false;
                const bool on = m_overlay && m_overlay->impactFlashVisible();
                check(on == (e.value != 0), line + " -> expected the impact glow " +
                                               (e.value ? "on" : "off") + ", it is " +
                                               (on ? "on" : "off"));
            } else
                keep = true;
        }
        else if (e.kind == "at")
            check(m_pointer->x() == e.value && m_pointer->y() == e.value2,
                  line + " -> expected the pointer at " + std::to_string(e.value) + "," +
                      std::to_string(e.value2) + ", it is at " +
                      std::to_string(m_pointer->x()) + "," +
                      std::to_string(m_pointer->y()));
        if (keep)
            next.push_back(e);
    }
    m_expect = std::move(next);
}

void Scenario::dumpState()
{
    const Diagnostics d = m_engine->diagnostics(true);
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "state: monitors=%d outerEdges=%d moves=%lld wraps=%lld suppressed=%lld",
                  d.monitors, d.outerEdges, d.stats.moves, d.stats.wraps, d.stats.suppressed);
    note(buf);
    for (int i = 1; i < kSkipReasonCount; ++i) {
        if (d.stats.skipped[i] <= 0)
            continue;
        std::snprintf(buf, sizeof buf, "  skipped %s: %lld",
                      skipReasonName(static_cast<SkipReason>(i)), d.stats.skipped[i]);
        note(buf);
    }
    const EdgeVerdict v = m_engine->lastVerdict();
    std::snprintf(buf, sizeof buf,
                  "  last verdict: edge=%s measured=%lld gate=%lld accel=%lld gate=%lld wrapped=%d",
                  edgeName(v.edge), v.measuredPps, v.thresholdPps, v.measuredAccelPps2,
                  v.thresholdAccelPps2, v.wrapped ? 1 : 0);
    note(buf);
    if (m_overlay)
        note("  overlay: active=" + std::string(m_overlay->active() ? "1" : "0")
             + " repaints=" + std::to_string(m_overlay->debugRepaints())
             + " timers=" + std::to_string(m_overlay->debugTimers())
             + " messages=" + std::to_string(m_overlay->debugMessages()));
}

DataSnapshot Scenario::snapshot()
{
    DataSnapshot snap;
    snap.enabled = true;
    snap.hookOk = true;
    snap.hookResponsive = true;
    const Diagnostics d = m_engine->diagnostics(true);
    snap.monitors = d.monitors;
    snap.outerEdges = d.outerEdges;
    snap.settings = d.settings;
    snap.stats = d.stats;
    snap.verdict = m_engine->lastVerdict();
    snap.pointer.x = m_pointer->x();
    snap.pointer.y = m_pointer->y();
    snap.hasPointer = true;
    if (m_engine->lastSample(snap.pointer))
        snap.hasPointer = true;
    snap.arrivals = m_engine->arrivals();
    snap.decisions = m_engine->decisions();
    snap.boxes = m_engine->monitorsSnapshot();
    return snap;
}

// A wrap is supposed to look like the pointer carried on moving, so the trail
// must not step BACKWARDS along the direction of travel, and one arrival must
// not produce two wraps. This is the "smooth as Windows" requirement in its
// most literal form: count the visible discontinuities.
bool trailIsMonotonic(const std::vector<std::pair<int64_t, int64_t>>& trail,
                      const std::string& axis)
{
    if (trail.size() < 2)
        return true;
    bool ok = true;
    for (size_t i = 1; i < trail.size(); ++i) {
        const long long prev = axis == "x" ? trail[i - 1].first : trail[i - 1].second;
        const long long curr = axis == "x" ? trail[i].first : trail[i].second;
        // A wrap teleports; a backwards step along the axis of travel is what
        // the user sees as a glitch. Allow equality (a held pointer).
        if (curr < prev)
            ok = false;
    }
    return ok;
}

bool Scenario::run(const std::string& rawLine)
{
    const std::string line = trim(rawLine);
    if (line.empty() || line[0] == '#')
        return true;
    const auto parts = split(line, ' ');
    if (parts.empty())
        return true;
    const std::string& cmd = parts[0];

    if (cmd == "expect") {
        if (parts.size() < 2) {
            note("FAIL expect without a condition: " + line);
            ++m_failures;
            ++m_checks;
            return false;
        }
        Expectation e;
        const std::string& what = parts[1];
        if (what == "wrap") {
            e.kind = "wrap";
        } else if (what == "no-wrap") {
            e.kind = "no-wrap";
        } else if (what.rfind("speed>=", 0) == 0) {
            e.kind = "speed>=";
            e.value = std::atoll(what.c_str() + 7);
        } else if (what.rfind("speed<", 0) == 0) {
            e.kind = "speed<";
            e.value = std::atoll(what.c_str() + 6);
        } else if (what.rfind("accel>=", 0) == 0) {
            e.kind = "accel>=";
            e.value = std::atoll(what.c_str() + 8);
        } else if (what.rfind("accel<", 0) == 0) {
            e.kind = "accel<";
            e.value = std::atoll(what.c_str() + 6);
        } else if (what == "flash") {
            // expect flash on|off -- the impact glow is showing right now.
            if (parts.size() < 3 || (parts[2] != "on" && parts[2] != "off")) {
                note("FAIL flash expectation needs on|off: " + line);
                ++m_failures;
                ++m_checks;
                return false;
            }
            e.kind = "flash";
            e.value = parts[2] == "on" ? 1 : 0;
        } else if (what == "readout") {
            // expect readout on|off -- the edge readout is showing. Unlike the
            // glow it is judged immediately: the readout is not a one-frame
            // effect, it lingers while the verdict is fresh.
            if (parts.size() < 3 || (parts[2] != "on" && parts[2] != "off")) {
                note("FAIL readout expectation needs on|off: " + line);
                ++m_failures;
                ++m_checks;
                return false;
            }
            const bool on = m_overlay && m_overlay->readoutVisible();
            if (on != (parts[2] == "on")) {
                // Diagnostic for a failed expectation: the freshness window is
                // the usual suspect, so print the age of the latched verdict.
                const cw::EdgeVerdict v = m_engine ? m_engine->lastArrivalVerdict()
                                                   : cw::EdgeVerdict {};
                LARGE_INTEGER now {}, freq {};
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const long long ageMs = v.tUs > 0
                    ? (cw::counterToUs(now.QuadPart, freq.QuadPart) - v.tUs) / 1000 : -1;
                std::string dbg;
                if (m_overlay)
                    dbg = ", repaints=" + std::to_string(m_overlay->debugRepaints())
                        + " builds=" + std::to_string(m_overlay->debugBuilds())
                        + " hides=" + std::to_string(m_overlay->debugHides())
                        + " lastFresh=" + (m_overlay->debugLastFresh() ? "1" : "0")
                        + " lastTuning=" + (m_overlay->debugLastTuning() ? "1" : "0")
                        + " overlayActive=" + (m_overlay->active() ? "1" : "0")
                        + " runEntered=" + (m_overlay->debugRunEntered() ? "1" : "0")
                        + " stripsBuilt=" + (m_overlay->debugStripsBuilt() ? "1" : "0")
                        + " anchorReady=" + (m_overlay->debugAnchorReady() ? "1" : "0")
                        + " loopExited=" + (m_overlay->debugLoopExited() ? "1" : "0")
                        + " loopResult=" + std::to_string(m_overlay->debugLoopResult())
                        + " messages=" + std::to_string(m_overlay->debugMessages())
                        + " timers=" + std::to_string(m_overlay->debugTimers())
                        + " setTimerOk=" + (m_overlay->debugSetTimerOk() ? "1" : "0")
                        + " setTimerErr=" + std::to_string(m_overlay->debugSetTimerError())
                        + " lastMsg=0x" + [&] {
                              std::ostringstream os;
                              os << std::hex << m_overlay->debugLastMsg();
                              return os.str();
                          }();
                note("FAIL readout diagnostics: verdict tUs age = " + std::to_string(ageMs)
                     + " ms, measured=" + std::to_string(v.measuredPps) + ", gate="
                     + std::to_string(v.thresholdPps) + ", monitor="
                     + std::to_string(v.monitorIndex) + dbg);
            }
            check(on == (parts[2] == "on"), line + " -> expected the readout " + parts[2]
                                               + ", it is " + (on ? "on" : "off"));
            return true;
        } else if (what == "at") {
            if (parts.size() < 4 || !parseInt(parts[2], e.value)
                || !parseInt(parts[3], e.value2)) {
                note("FAIL bad expectation: " + line);
                ++m_failures;
                ++m_checks;
                return false;
            }
            e.kind = "at";
        } else {
            note("FAIL unknown expectation: " + what);
            ++m_failures;
            ++m_checks;
            return false;
        }
        m_expect.push_back(e);
        return true;
    }
    if (cmd == "show") {
        const std::string what = parts.size() > 1 ? parts[1] : "both";
        if (what == "none") {
            m_overlay->stop();
            m_dataWindow->stop();
        } else {
            if (what == "overlay" || what == "both")
                m_overlay->start(m_engine->monitorsSnapshot());
            if (what == "data" || what == "both")
                m_dataWindow->start([this] { return snapshot(); });
        }
        note("show " + what + " (overlay=" + (m_overlay->active() ? "on" : "off")
             + " data=" + (m_dataWindow->active() ? "on" : "off") + ")");
        return true;
    }
    if (cmd == "wait") {
        long long ms = 200;
        if (parts.size() > 1)
            parseInt(parts[1], ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        // Real time passed, so the scenario clock passes too: the verdicts stay
        // roughly comparable with the overlay's real-time freshness window.
        m_clockUs += ms * 1000;
        // A wait is what lets a timed effect (the impact glow, driven by the
        // overlay's 60 ms repaint) reach its next state, so a queued `flash`
        // expectation can be judged after it. Without this it sat in the queue
        // until the next move command and was silently never checked.
        m_flashWait = true;
        settle(line);
        m_flashWait = false;
        return true;
    }
    if (cmd == "dump") {
        dumpState();
        return true;
    }
    if (cmd == "modifier") {
        m_pointer->setModifier(parts.size() > 1 && parts[1] != "none");
        return true;
    }
    if (cmd == "drag") {
        m_pointer->setLeftButton(parts.size() > 1 && parts[1] == "on");
        return true;
    }
    if (cmd == "move") {
        long long x = 0, y = 0, dt = 8;
        if (parts.size() < 3 || !parseInt(parts[1], x) || !parseInt(parts[2], y)) {
            note("FAIL bad move: " + line);
            ++m_failures;
            ++m_checks;
            return false;
        }
        if (parts.size() > 3)
            parseInt(parts[3], dt);
        const auto [nx, ny] = m_pointer->deliver({ x, y, m_clockUs, m_clockUs / 1000 });
        m_clockUs += dt * 1000;
        const EdgeVerdict v = m_engine->lastVerdict();
        m_lastSpeed = v.measuredPps;
        m_lastAccel = v.measuredAccelPps2;
        m_lastWrapped = nx != x || ny != y;
        note("move " + std::to_string(x) + "," + std::to_string(y) + " -> " +
             std::to_string(nx) + "," + std::to_string(ny)
             + (m_lastWrapped ? " WRAP" : "") + " speed=" + std::to_string(m_lastSpeed)
             + " accel=" + std::to_string(m_engine->lastVerdict().measuredAccelPps2));
        settle(line);
        return true;
    }
    if (cmd == "stroke") {
        long long x0 = 0, y0 = 0, x1 = 0, y1 = 0, steps = 16, dt = 8;
        if (parts.size() < 7 || !parseInt(parts[1], x0) || !parseInt(parts[2], y0)
            || !parseInt(parts[3], x1) || !parseInt(parts[4], y1)
            || !parseInt(parts[5], steps) || !parseInt(parts[6], dt)) {
            note("FAIL bad stroke: " + line);
            ++m_failures;
            ++m_checks;
            return false;
        }
        if (steps < 1)
            steps = 1;
        long long landedX = x1, landedY = y1;
        for (long long i = 1; i <= steps; ++i) {
            const long long x = x0 + (x1 - x0) * i / steps;
            const long long y = y0 + (y1 - y0) * i / steps;
            const auto [nx, ny] = m_pointer->deliver({ x, y, m_clockUs, m_clockUs / 1000 });
            m_clockUs += dt * 1000;
            landedX = nx;
            landedY = ny;
        }
        const EdgeVerdict v = m_engine->lastVerdict();
        m_lastSpeed = v.measuredPps;
        m_lastAccel = v.measuredAccelPps2;
        m_lastWrapped = landedX != x1 || landedY != y1;
        note("stroke " + std::to_string(x0) + "," + std::to_string(y0) + " -> "
             + std::to_string(x1) + "," + std::to_string(y1) + " in "
             + std::to_string(steps) + " steps: landed " + std::to_string(landedX) + ","
             + std::to_string(landedY) + (m_lastWrapped ? " WRAP" : "")
             + " speed=" + std::to_string(m_lastSpeed)
             + " accel=" + std::to_string(m_engine->lastVerdict().measuredAccelPps2));
        settle(line);
        return true;
    }
    if (cmd == "brake") {
        long long x0 = 0, dt = 8;
        double v0 = 0, v1 = 0;
        if (parts.size() < 6 || !parseInt(parts[1], x0) || !parseDouble(parts[2], v0)
            || !parseDouble(parts[3], v1) || !parseInt(parts[5], dt)) {
            note("FAIL bad brake: " + line);
            ++m_failures;
            ++m_checks;
            return false;
        }
        const bool right = parts[4] != "left";
        const auto boxes = m_engine->monitorsSnapshot();
        if (boxes.empty()) {
            note("FAIL brake without a monitor");
            ++m_failures;
            ++m_checks;
            return false;
        }
        const int64_t target = right ? boxes.front().right - 1 : boxes.front().left;
        double x = static_cast<double>(x0);
        const double span = static_cast<double>(target - x0);
        int64_t lastX = x0;
        bool wrapped = false;
        for (int guard = 0; guard < 100000; ++guard) {
            const double frac = span == 0.0 ? 1.0
                : std::min(1.0, std::fabs((x - static_cast<double>(x0)) / span));
            const double v = v0 + (v1 - v0) * frac;
            const double step = v * static_cast<double>(dt) / 1000.0;
            x = right ? std::min(static_cast<double>(target), x + step)
                      : std::max(static_cast<double>(target), x - step);
            const auto [nx, ny] = m_pointer->deliver({ static_cast<int64_t>(x), 540, m_clockUs,
                                                       m_clockUs / 1000 });
            m_clockUs += dt * 1000;
            lastX = nx;
            if (nx != static_cast<int64_t>(x)) {
                wrapped = true;
                break;
            }
            if (v < 1.0 || x == static_cast<double>(target))
                break;
        }
        const EdgeVerdict v = m_engine->lastVerdict();
        m_lastSpeed = v.measuredPps;
        m_lastAccel = v.measuredAccelPps2;
        m_lastWrapped = wrapped;
        note("brake " + std::to_string(static_cast<long long>(v0)) + "->"
             + std::to_string(static_cast<long long>(v1)) + " px/s to the "
             + (right ? "right" : "left") + " edge: speed=" + std::to_string(m_lastSpeed)
             + (m_lastWrapped ? " WRAP" : " no-wrap") + " landed="
             + std::to_string(lastX));
        settle(line);
        return true;
    }
    if (cmd == "sweep") {
        // A continuous stroke that FEEDS ITS OWN RESULT BACK, the way a hand does:
        // the pointer keeps moving from wherever the wrap put it. `stroke` cannot
        // test this because it always samples points on the original straight
        // line, so after a wrap it "jumps" to the far side by itself and hides
        // exactly the discontinuity a user would see.
        //
        //   sweep <x> <y> <dx> <steps> <dtMs>    move by (dx,0) per step
        long long x = 0, y = 0, dx = 0, steps = 16, dt = 8;
        if (parts.size() < 6 || !parseInt(parts[1], x) || !parseInt(parts[2], y)
            || !parseInt(parts[3], dx) || !parseInt(parts[4], steps)
            || !parseInt(parts[5], dt)) {
            note("FAIL bad sweep: " + line);
            ++m_failures;
            ++m_checks;
            return false;
        }
        m_pointer->clearTrail();
        long long wraps = 0;
        long long lastX = x, lastY = y;
        long long curX = x, curY = y;
        for (long long i = 0; i < steps; ++i) {
            const long long nextX = curX + dx;
            const auto [nx, ny] = m_pointer->deliver({ nextX, curY, m_clockUs, m_clockUs / 1000 });
            m_clockUs += dt * 1000;
            if (nx != nextX || ny != curY)
                ++wraps;
            curX = nx;
            curY = ny;
            lastX = nx;
            lastY = ny;
        }
        // Continuity: only x should ever change here, and after the wrap the
        // pointer must stay on the axis it was travelling on.
        bool yMoved = false;
        bool sawWrap = false;
        for (const auto& p : m_pointer->trail()) {
            if (p.second != y)
                yMoved = true;
            if (p.first != x && !sawWrap)
                sawWrap = true;
        }
        check(!yMoved, "sweep " + line + " -> the vertical coordinate drifted; a horizontal "
                       "sweep must never move the pointer vertically");
        check(wraps <= 1, "sweep " + line + " -> the pointer wrapped " + std::to_string(wraps) +
                          " times in one continuous sweep; it must wrap at most once");
        check(sawWrap, "sweep " + line + " -> never wrapped, so the sweep proved nothing");
        m_lastWrapped = wraps > 0;
        m_lastSpeed = m_engine->lastVerdict().measuredPps;
        m_lastAccel = m_engine->lastVerdict().measuredAccelPps2;
        note("sweep from " + std::to_string(x) + "," + std::to_string(y) + " step x" +
             std::to_string(dx) + " for " + std::to_string(steps) + ": ended " +
             std::to_string(lastX) + "," + std::to_string(lastY) + " wraps=" +
             std::to_string(wraps));
        settle(line);
        return true;
    }
    if (cmd == "hand") {
        // A physically realistic mouse: speed ramps LINEARLY from v0 to v1 while
        // the pointer travels toward the edge, sampled at `rateHz` with integer
        // pixel quantisation — the thing a real driver delivers. `stroke`
        // cannot model acceleration (equal steps = constant speed) and a hand
        // written as explicit `move` lines cannot model quantisation at 1000 Hz.
        //
        //   hand <x> <v0> <v1> <rateHz> <maxMs> [jitterPx]
        //
        // `jitterPx` superimposes a deterministic ±1 px wobble against the
        // direction of travel on every Nth sample, the way a real driver
        // reports while a hand pushes. Without it the shell's hand walks a
        // perfectly straight line — which is exactly the one situation the
        // acceleration window's tolerance cannot be tested in.
        long long x = 0, rateHz = 125, maxMs = 2000, jitterEvery = 0;
        double v0 = 0, v1 = 0;
        if (parts.size() < 6 || !parseInt(parts[1], x) || !parseDouble(parts[2], v0)
            || !parseDouble(parts[3], v1) || !parseInt(parts[4], rateHz)
            || !parseInt(parts[5], maxMs)) {
            note("FAIL bad hand: " + line);
            ++m_failures;
            ++m_checks;
            return false;
        }
        if (parts.size() > 6)
            parseInt(parts[6], jitterEvery);
        if (rateHz < 1)
            rateHz = 1;
        const int64_t target = edgeTargetX(*m_engine);
        if (target < 0) {
            note("FAIL hand without a monitor");
            ++m_failures;
            ++m_checks;
            return false;
        }
        const long long dtUs = 1'000'000 / rateHz;
        const long long samples = std::max<long long>(1, maxMs * rateHz / 1000);
        double exact = static_cast<double>(x);
        long long emitted = 0;
        bool wrapped = false;
        long long landedX = 0;
        long long landedY = 0;
        for (long long n = 1; n <= samples; ++n) {
            const double frac = static_cast<double>(n) / static_cast<double>(samples);
            const double v = v0 + (v1 - v0) * frac;
            exact += v * static_cast<double>(dtUs) / 1'000'000.0;
            long long px = static_cast<long long>(exact);
            if (jitterEvery > 0 && n % jitterEvery == 0 && px > x + 4)
                px -= 1; // hand jitter: one pixel against the travel
            if (px > target)
                px = target;
            const auto [nx, ny] = m_pointer->deliver({ px, 540, m_clockUs, m_clockUs / 1000 });
            m_clockUs += dtUs;
            ++emitted;
            landedX = nx;
            landedY = ny;
            if (nx != px || ny != 540) {
                wrapped = true;
                break;
            }
            if (px >= target)
                break;
        }
        m_lastSpeed = m_engine->lastVerdict().measuredPps;
        m_lastAccel = m_engine->lastVerdict().measuredAccelPps2;
        m_lastWrapped = wrapped;
        note("hand v=" + std::to_string(static_cast<long long>(v0)) + "->" +
             std::to_string(static_cast<long long>(v1)) + " px/s at " +
             std::to_string(rateHz) + "Hz, " + std::to_string(emitted) +
             " samples" + (jitterEvery > 0 ? " (jitter 1px/" + std::to_string(jitterEvery) + ")" : "")
             + ": speed=" + std::to_string(m_lastSpeed) + " accel=" +
             std::to_string(m_lastAccel) + (wrapped ? " WRAP" : " no-wrap") + " landed=" +
             std::to_string(landedX) + "," + std::to_string(landedY));
        settle(line);
        return true;
    }
    note("FAIL unknown command: " + line);
    ++m_failures;
    ++m_checks;
    return false;
}

// --- modes ------------------------------------------------------------------

void printLayout(const std::vector<MonitorBox>& monitors)
{
    std::printf("monitors: %zu%s\n", monitors.size(),
                ::GetSystemMetrics(SM_REMOTESESSION) ? " (remote session)" : "");
    for (const MonitorBox& m : monitors) {
        std::printf("  [%lld,%lld..%lld,%lld]%s\n", static_cast<long long>(m.left),
                    static_cast<long long>(m.top), static_cast<long long>(m.right),
                    static_cast<long long>(m.bottom), m.primary ? " primary" : "");
    }
}

int runDiag()
{
    const auto monitors = realMonitors();
    printLayout(monitors);
    WrapEngine engine;
    engine.rebuild(monitors);
    const Diagnostics d = engine.diagnostics(true);
    std::printf("outer edges: %d\n", d.outerEdges);
    for (const MonitorEdge& e : engine.outerEdgesSnapshot()) {
        std::printf("  %s edge at %lld\n", edgeName(e.type),
                    static_cast<long long>(e.position));
    }
    for (const std::string& gap : d.gaps)
        std::printf("  gap: %s\n", gap.c_str());
    std::printf("suggest gate: %lld px/s (needs >= 8 arrivals)\n",
                adviseGate(engine.arrivals(), 0).pps);
    return 0;
}

int runScenario(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 2;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);

    // Pre-pass: only the LAYOUT, so the engine is built with the right
    // monitors. Settings are applied in order while the scenario runs, because
    // a scenario is allowed to change them halfway through.
    std::vector<MonitorBox> monitors;
    for (const std::string& raw : lines) {
        const std::string t = trim(raw);
        if (t.rfind("monitor ", 0) == 0)
            monitors = parseMonitors(trim(t.substr(8)));
    }
    if (monitors.empty())
        monitors = realMonitors();

    WrapEngine engine;
    engine.rebuild(monitors);
    ScriptedPointer pointer(&engine);
    Overlay overlay;
    overlay.setEngine(&engine);
    DataWindow dataWindow;

    Scenario scenario(&engine, &pointer, &overlay, &dataWindow);
    Settings settings;
    int scenarioFailures = 0;
    for (const std::string& raw : lines) {
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#')
            continue;
        if (t.rfind("monitor ", 0) == 0)
            continue; // applied in the pre-pass: the layout cannot change mid-run
        if (t.rfind("set ", 0) == 0) {
            // Settings apply IN ORDER, like the search bar: the pre-pass above
            // only established the baseline the engine was built with, so a
            // scenario that switches mode halfway is honoured.
            const auto parts = split(t, ' ');
            std::string error;
            if (parts.size() >= 3 && applySetting(settings, parts[1], parts[2], error)) {
                engine.setSettings(settings);
                scenario.syncWindows(); // a setting drives its window, as in the plugin
                std::printf("set %s = %s\n", parts[1].c_str(), parts[2].c_str());
            } else {
                std::printf("FAIL set %s: %s\n", t.c_str(), error.c_str());
                ++scenarioFailures;
            }
            continue;
        }
        scenario.run(t);
    }

    for (const std::string& entry : scenario.log())
        std::printf("%s\n", entry.c_str());
    const int failures = scenario.failures() + scenarioFailures;
    std::printf("\n%d checks, %d failure(s)\n", scenario.checks() + scenarioFailures,
                failures);
    overlay.stop();
    dataWindow.stop();
    return failures == 0 ? 0 : 1;
}

int runGui(const std::string& scriptPath)
{
    const auto monitors = realMonitors();
    printLayout(monitors);
    Settings settings;
    settings.tuningOverlay = true;
    settings.dataWindow = true;
    settings.impactGate = true;
    settings.impactSpeedPps = 2000;
    settings.impactBandPx = 150;

    WrapEngine engine(settings);
    engine.rebuild(monitors);
    ScriptedPointer pointer(&engine);

    Overlay overlay;
    overlay.setEngine(&engine);
    overlay.start(monitors);

    DataWindow dataWindow;
    dataWindow.start([&] {
        DataSnapshot snap;
        snap.enabled = true;
        snap.hookOk = true;
        snap.hookResponsive = true;
        const Diagnostics d = engine.diagnostics(true);
        snap.monitors = d.monitors;
        snap.outerEdges = d.outerEdges;
        snap.settings = d.settings;
        snap.stats = d.stats;
        snap.verdict = engine.lastVerdict();
        snap.pointer.x = pointer.x();
        snap.pointer.y = pointer.y();
        snap.hasPointer = true;
        snap.arrivals = engine.arrivals();
        snap.decisions = engine.decisions();
        snap.boxes = engine.monitorsSnapshot();
        return snap;
    });

    const int64_t edge = monitors.front().right - 1;
    std::printf("overlay=%s data=%s\n", overlay.active() ? "up" : "down",
                dataWindow.active() ? "up" : "down");

    // --script: feed the same three gestures a key would, then wait, so a
    // screenshot can prove what the panels actually paint. Anything after the
    // script is keyboard-driven.
    if (!scriptPath.empty()) {
        std::ifstream file(scriptPath);
        if (!file) {
            std::fprintf(stderr, "cannot open %s\n", scriptPath.c_str());
            overlay.stop();
            dataWindow.stop();
            return 2;
        }
        Scenario scenario(&engine, &pointer, &overlay, &dataWindow);
        std::string line;
        while (std::getline(file, line)) {
            const std::string t = trim(line);
            // The layout is fixed for the run (the windows are already up), and
            // settings go through the engine so the windows follow them — the
            // same contract as the `run` mode and the plugin.
            if (t.rfind("set ", 0) == 0) {
                const auto parts = split(t, ' ');
                Settings next = engine.settings();
                std::string error;
                if (parts.size() >= 3 && applySetting(next, parts[1], parts[2], error)) {
                    engine.setSettings(next);
                    scenario.syncWindows();
                    std::printf("set %s = %s\n", parts[1].c_str(), parts[2].c_str());
                } else {
                    std::printf("FAIL set %s: %s\n", t.c_str(), error.c_str());
                }
                continue;
            }
            if (t.rfind("monitor ", 0) == 0)
                continue; // the real layout is the point of gui mode
            scenario.run(t);
        }
        for (const std::string& entry : scenario.log())
            std::printf("%s\n", entry.c_str());
        std::printf("\n%d checks, %d failure(s)\n", scenario.checks(), scenario.failures());
        // Leave the windows up so a screenshot can be taken, then exit when
        // the caller asks (a marker file) or after a bounded wait.
        std::printf("windows are up; waiting for the caller\n");
        std::fflush(stdout);
        const DWORD deadline = ::GetTickCount() + 60000;
        while (::GetTickCount() < deadline) {
            if (::GetAsyncKeyState('Q') & 0x8000)
                break;
            const DWORD attrs = ::GetFileAttributesA((scriptPath + ".done").c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        overlay.stop();
        dataWindow.stop();
        return scenario.failures() == 0 ? 0 : 1;
    }

    std::printf(
        "keys: 1 slow drift (must be refused)  2 fast flick (must wrap)  "
        "3 braking flick (must be refused)  q quit\n");

    long long clockUs = 1'000'000;
    for (;;) {
        if (::GetAsyncKeyState('Q') & 0x8000)
            break;
        if (::GetAsyncKeyState('1') & 0x8000) {
            for (long long x = 1200; x <= edge; x += 4) {
                pointer.deliver({ x, 540, clockUs, clockUs / 1000 });
                clockUs += 16000;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::printf("slow drift   measured=%lld px/s  wraps=%lld\n",
                        engine.lastVerdict().measuredPps, pointer.wraps());
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        } else if (::GetAsyncKeyState('2') & 0x8000) {
            for (long long x = 1200; x <= edge; x += 48) {
                pointer.deliver({ x, 540, clockUs, clockUs / 1000 });
                clockUs += 8000;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::printf("fast flick   measured=%lld px/s  wraps=%lld\n",
                        engine.lastVerdict().measuredPps, pointer.wraps());
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        } else if (::GetAsyncKeyState('3') & 0x8000) {
            long long x = 1200;
            while (x < edge) {
                const double frac = static_cast<double>(x - 1200)
                    / static_cast<double>(edge - 1200);
                const double v = 4000.0 + (250.0 - 4000.0) * frac;
                x += static_cast<long long>(v * 0.008);
                if (x > edge)
                    x = edge;
                pointer.deliver({ x, 540, clockUs, clockUs / 1000 });
                clockUs += 8000;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::printf("braking      measured=%lld px/s  wraps=%lld\n",
                        engine.lastVerdict().measuredPps, pointer.wraps());
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    overlay.stop();
    dataWindow.stop();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "gui") {
        const std::string script = argc > 3 && std::string(argv[2]) == "--script"
            ? argv[3]
            : std::string();
        return runGui(script);
    }
    if (mode == "diag")
        return runDiag();
    if (mode == "run") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: cw_devshell run <scenario.txt>\n");
            return 2;
        }
        return runScenario(argv[2]);
    }
    std::printf(
        "cw_devshell - run the real engine, overlay and data window without WinShun\n"
        "\n"
        "  cw_devshell diag                 print the layout and the engine's edges\n"
        "  cw_devshell gui                  show both windows, drive with 1/2/3, q quits\n"
        "  cw_devshell gui --script <f>     show the windows, replay <f>, then wait\n"
        "  cw_devshell run <scenario.txt>   replay a scenario and assert on it\n"
        "\n"
        "Nothing here moves the real pointer, so it works over a Remote Desktop\n"
        "session and never fights the mouse in your hand.\n");
    return 0;
}