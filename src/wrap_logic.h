// wrap_logic.h — the pure wrap state machine on top of Topology.
//
// Ported from PowerToys CursorWrap CursorWrapCore.{h,cpp} (MIT, Microsoft
// Corporation). Decides, per mouse-move sample, whether the cursor should
// wrap and where. No OS headers: pointer position, movement direction,
// button state and modifier state are all parameters, so the whole decision
// path unit-tests on any platform.
#pragma once

#include "wrap_topology.h"

#include <string>
#include <vector>

namespace cw {

enum class ActivationMode : int { Always = 0, WhileCtrl = 1, WhileShift = 2 };

struct Settings {
    WrapMode wrapMode = WrapMode::Both;
    ActivationMode activation = ActivationMode::Always;
    bool disableDuringDrag = true;
    bool disableOnSingleMonitor = false;
    int edgeThresholdPx = 1;        // 1..20
    int wrapGuardPx = 50;           // 10..200
    int adjacencyTolerancePx = 50;  // 10..200
};

// Parsed-setting values straight from the host JSON, already type-checked.
// wrap_logic stays independent of the SDK's Json type: the exe side converts
// winshun::Json into this struct (see settings.h), tests fill it directly.
struct RawSettings {
    std::string wrapMode;         // "both" / "vertical" / "horizontal"
    std::string activationMode;   // "always" / "ctrl" / "shift"
    bool disableDuringDrag = true;
    bool disableOnSingleMonitor = false;
    double edgeThresholdPx = 1;
    double wrapGuardPx = 50;
    double adjacencyTolerancePx = 50;
    bool hasWrapMode = false;
    bool hasActivation = false;
    bool hasDrag = false;
    bool hasSingle = false;
    bool hasEdgeThreshold = false;
    bool hasWrapGuard = false;
    bool hasAdjacency = false;
};

// Apply RawSettings onto Settings. Every bad value falls back to the default
// (or to the current value when merging partial updates) — a wrong settings
// file must never break the plugin. Pure; unit-tested.
void applySettings(const RawSettings& raw, Settings& out, bool merge);

// One mouse-move sample in pointer space.
struct Sample {
    int64_t x = 0;
    int64_t y = 0;
};

// Statistics for the status row and diagnostics copy.
struct Stats {
    long long moves = 0;      // all WM_MOUSEMOVE events seen
    long long wraps = 0;      // wraps performed
    long long suppressed = 0; // recognized-but-skipped (guard, drag, single, activation)
    long long rebuilt = 0;    // topology rebuilds
};

// Diagnostic snapshot for the Shift+Enter copy row.
struct Diagnostics {
    bool enabled = true;
    bool hookOk = false;
    int wrapMode = 0;
    int activation = 0;
    int monitors = 0;
    int outerEdges = 0;
    Settings settings;
    Stats stats;
    std::vector<std::string> gaps; // human-readable layout gap notes
};

class WrapEngine {
public:
    explicit WrapEngine(Settings settings = {});

    void setSettings(const Settings& settings);
    const Settings& settings() const { return m_settings; }

    // Rebuild topology from a monitor snapshot (on WM_DISPLAYCHANGE etc).
    void rebuild(const std::vector<MonitorBox>& monitors);
    const Topology& topology() const { return m_topology; }

    // Forget movement history (after enabling/re-enabling or a rebuild).
    void resetState();

    // Decide the wrap for one mouse move. Returns the new cursor position;
    // equals `sample` when no wrap should happen. `modifierHeld` is the
    // activation key state (GetAsyncKeyState result at the call site),
    // `leftButtonHeld` the drag state.
    std::pair<int64_t, int64_t> onMouseMove(const Sample& sample, bool modifierHeld,
                                            bool leftButtonHeld);

    const Stats& stats() const { return m_stats; }
    Diagnostics diagnostics(bool hookOk) const;

    static constexpr int64_t kNever = INT64_MIN;

private:
    bool withinGuard(int64_t x, int64_t y) const;
    CursorDirection direction(const Sample& s) const;

    Settings m_settings;
    Topology m_topology;
    Stats m_stats;
    Sample m_previous { kNever, kNever };
    bool m_hasPrevious = false;
    Sample m_lastWrap { kNever, kNever };
    bool m_hasLastWrap = false;
};

} // namespace cw
