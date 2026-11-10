// wrap_logic.h — the pure wrap state machine on top of Topology.
//
// Ported from PowerToys CursorWrap CursorWrapCore.{h,cpp} (MIT, Microsoft
// Corporation). Decides, per mouse-move sample, whether the cursor should
// wrap and where. No OS headers: pointer position, timestamp, button state
// and modifier state are all parameters, so the whole decision path
// unit-tests on any platform.
//
// Thread model: one engine is shared by the protocol thread
// (setSettings / rebuild / resetState / diagnostics / stats) and the hook
// thread (onMouseMove). Every public method takes the internal mutex, so
// the hot path costs one uncontended lock per event and the settings a wrap
// decision reads are always consistent.
#pragma once

#include "wrap_topology.h"

#include <mutex>
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
    // Impact gate: wrap only when the pointer reaches the edge fast enough.
    // The speed is measured over the last continuous stroke toward the edge,
    // clipped to the approach band (impactBandPx) so a fast flight that
    // braked before the edge does not count; per-event acceleration is noise
    // at 1000 Hz report rates, so the knob is a speed floor (the same
    // conclusion PowerToys Find My Mouse reached for its shake gate).
    bool impactGate = false;
    int impactSpeedPps = 2000;      // 500..20000 px/s
    int impactBandPx = 150;         // 50..500 px: the approach band speed is measured in
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
    bool impactGate = false;
    double impactSpeedPps = 2000;
    double impactBandPx = 150;
    bool hasWrapMode = false;
    bool hasActivation = false;
    bool hasDrag = false;
    bool hasSingle = false;
    bool hasEdgeThreshold = false;
    bool hasWrapGuard = false;
    bool hasAdjacency = false;
    bool hasImpactGate = false;
    bool hasImpactSpeed = false;
    bool hasImpactBand = false;
};

// Apply RawSettings onto Settings. Every bad value falls back to the default
// (or to the current value when merging partial updates) — a wrong settings
// file must never break the plugin. Pure; unit-tested.
void applySettings(const RawSettings& raw, Settings& out, bool merge);

// One mouse-move sample in pointer space. tUs is the event timestamp in
// microseconds (QueryPerformanceCounter at the hook). 0 means unknown: the
// sample still drives the state machine, but the impact gate cannot measure
// a speed from it and fails closed (no wrap).
struct Sample {
    int64_t x = 0;
    int64_t y = 0;
    long long tUs = 0;
};

// Statistics for the status row and diagnostics copy.
struct Stats {
    long long moves = 0;      // all WM_MOUSEMOVE events seen
    long long wraps = 0;      // wraps performed
    long long suppressed = 0; // recognized-but-skipped (guard, drag, single, activation, impact)
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
    Settings settings() const;

    // Rebuild topology from a monitor snapshot (on WM_DISPLAYCHANGE etc).
    void rebuild(const std::vector<MonitorBox>& monitors);

    // Forget movement history (after enabling/re-enabling or a rebuild).
    void resetState();

    // Decide the wrap for one mouse move. Returns the new cursor position;
    // equals `sample` when no wrap should happen. `modifierHeld` is the
    // activation key state (GetAsyncKeyState result at the call site),
    // `leftButtonHeld` the drag state.
    std::pair<int64_t, int64_t> onMouseMove(const Sample& sample, bool modifierHeld,
                                            bool leftButtonHeld);

    Stats stats() const;
    Diagnostics diagnostics(bool hookOk) const;

    // Locked topology sizes (the hook thread may rebuild concurrently).
    size_t monitorCount() const;
    size_t outerEdgeCount() const;

    static constexpr int64_t kNever = INT64_MIN;
    // The impact speed is measured over the last continuous stroke toward
    // the edge, at most this long (older samples are irrelevant anyway).
    static constexpr long long kImpactWindowUs = 100 * 1000; // 100 ms
    // 1000 Hz gaming mice: 32 samples were only 32 ms; 128 covers the
    // window with headroom for a busy hook chain.
    static constexpr size_t kHistoryMax = 128;
    // A stroke shorter than this is jitter, not a hit: the pointer must
    // actually travel toward the edge before its speed means anything.
    static constexpr int64_t kImpactMinTravelPx = 20;

private:
    bool withinGuard(int64_t x, int64_t y) const;
    CursorDirection direction(const Sample& s) const;
    // Signed approach speed toward `edge` over the last continuous stroke
    // clipped to the approach band; 0 when it cannot be measured. The mutex
    // is held by the caller.
    long long approachSpeedPps(EdgeType edge, int64_t edgeCoord) const;
    void noteSample(const Sample& sample); // mutex held by the caller

    mutable std::mutex m_mutex;
    Settings m_settings;
    Topology m_topology;
    Stats m_stats;
    std::vector<Sample> m_history;
    Sample m_previous { kNever, kNever };
    bool m_hasPrevious = false;
    Sample m_lastWrap { kNever, kNever };
    bool m_hasLastWrap = false;
};

} // namespace cw
