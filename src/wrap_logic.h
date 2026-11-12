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
    // The two runtime windows. They live in Settings (not only in the command
    // layer) so the host card, the search bar and the persisted overrides all
    // see the same switch: the plugin's settings page must be able to turn on
    // what the search bar can turn on.
    bool tuningOverlay = false;     // draw the edge bands and the readout
    bool dataWindow = false;        // show the live data window
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
    bool tuningOverlay = false;
    bool dataWindow = false;
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
    bool hasTuningOverlay = false;
    bool hasDataWindow = false;
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

// Why a move did not wrap. A single "suppressed" total is useless when the
// user reports "it does not wrap at all": nine different gates can each say
// no, and only the split tells them apart.
enum class SkipReason : int {
    None = 0,       // the move wrapped
    NoMonitors,
    SingleMonitor,
    Dragging,
    NoModifier,
    Guard,
    OffMonitor,
    NotOnEdge,
    ImpactGate,
};
inline constexpr int kSkipReasonCount = static_cast<int>(SkipReason::ImpactGate) + 1;

// The name used in diagnostics and the data window (English, stable).
const char* skipReasonName(SkipReason reason);

// Statistics for the status row and diagnostics copy.
struct Stats {
    long long moves = 0;      // all WM_MOUSEMOVE events seen
    long long wraps = 0;      // wraps performed
    long long suppressed = 0; // sum of `skipped` (kept for the existing rows)
    long long rebuilt = 0;    // topology rebuilds
    long long skipped[kSkipReasonCount] = {}; // per-reason counts
};

// One recorded decision: an edge arrival, or a change of the reason a move
// was skipped. Bounded so a long session cannot grow without limit.
struct Decision {
    long long seq = 0;
    long long tUs = 0;
    int64_t x = 0;
    int64_t y = 0;
    int monitorIndex = -1;
    EdgeType edge = EdgeType::Left;
    bool onEdge = false;
    long long measuredPps = 0;
    long long gatePps = 0;
    SkipReason reason = SkipReason::None; // None + wrapped: an accepted arrival
    bool wrapped = false;
};

// One edge-arrival verdict, for the tuning overlay and diagnostics: what the
// gate measured the last time the pointer reached an outer edge.
struct EdgeVerdict {
    long long tUs = 0;         // when the verdict was made (0 = none yet)
    int monitorIndex = -1;
    EdgeType edge = EdgeType::Left;
    int64_t edgeCoord = 0;     // the edge line's coordinate (x for Left/Right)
    int64_t x = 0;             // where the pointer was, for placing the readout
    int64_t y = 0;
    long long measuredPps = 0; // the approach speed the gate measured
    long long thresholdPps = 0; // the configured gate (0 = gate off)
    bool wrapped = false;      // the move wrapped
    bool gateOff = false;      // impact gate disabled: arrival wraps outright
    bool guardBlocked = false; // suppressed by the anti-oscillation guard
    int64_t destX = 0;         // the wrap destination (valid when wrapped)
    int64_t destY = 0;
};

// One edge arrival, recorded for the tuning trace. The measured speed alone
// answers "would gate X have wrapped this?" for every X, so a full trace
// makes the optimal gate a reading, not a guess.
struct Arrival {
    long long seq = 0;         // monotonically increasing, for incremental drains
    long long tUs = 0;         // when the arrival happened
    int monitorIndex = -1;
    EdgeType edge = EdgeType::Left;
    long long measuredPps = 0; // the approach speed measured in the band
    long long gatePps = 0;     // the gate configured at that moment
    bool wrapped = false;
    bool gateOff = false;      // gate was off: the arrival wrapped outright
};

// The gate between the two clusters of arrival speeds: intentional pushes sit
// in the upper cluster, incidental drifts in the lower one. Returns 0 when the
// trace has no clear gap yet — never a fabricated number. Pure; unit-tested.
long long suggestGate(const std::vector<Arrival>& arrivals);

// Diagnostics snapshot for the Shift+Enter copy row.
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
    // Hook health, filled in by the plugin (wrap_logic does not see the hook).
    long long hookReinstalls = 0;   // times the watchdog reinstalled the hook
    long long hookMissed = 0;       // watchdog cycles that saw a moved pointer
                                    // with no event: the hook was being dropped
    long long hookDropped = 0;      // hook callbacks Windows reported as timed out
    long long setCursorFailures = 0; // SetCursorPos said no (no wrap happened)
    bool hookResponsive = true;      // false after the last watchdog fired
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

    // The most recent edge-arrival verdict (for the tuning overlay). Never
    // blocks: a small struct copied under the engine mutex.
    EdgeVerdict lastVerdict() const;

    // The recorded edge arrivals, oldest first. The tuner copies these to draw
    // the histogram and the suggested gate. Bounded (kTraceMax) so a long
    // session cannot grow without limit.
    std::vector<Arrival> arrivals() const;
    // Drop the recorded arrivals (the tuner's "clear" action).
    void clearArrivals();
    static constexpr size_t kTraceMax = 512;

    // The most recent decisions (arrivals and first-of-a-run skip reasons),
    // newest last. Answers "why did the last push not wrap?" in the data
    // window; bounded by kDecisionMax.
    std::vector<Decision> decisions() const;
    static constexpr size_t kDecisionMax = 24;

    // The last mouse position the engine saw, even when the move was
    // skipped. The data window draws it so the user sees the pointer the
    // engine sees.
    bool lastSample(Sample& out) const;

    // A copy of the current monitor boxes (the overlay windows cover them).
    std::vector<MonitorBox> monitorsSnapshot() const;

    // The edges that can actually wrap, so the tuning overlay draws lines
    // only where a wrap is possible and leaves inner edges alone.
    std::vector<MonitorEdge> outerEdgesSnapshot() const;

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
    EdgeVerdict m_verdict; // the latest edge arrival, for the overlay
    std::vector<Arrival> m_arrivals; // bounded trace, for the tuner
    long long m_arrivalSeq = 0;
    // The edge this arrival run belongs to: repeated samples while the pointer
    // is clamped at one edge are one arrival, not hundreds.
    EdgeType m_arrivalEdge = EdgeType::Left;
    int m_arrivalMonitor = -1;
    bool m_hasArrivalEdge = false;
    // The decision log and the last position seen (for the data window).
    std::vector<Decision> m_decisions;
    long long m_decisionSeq = 0;
    Sample m_lastSample { kNever, kNever };
    bool m_hasLastSample = false;
    // The skip reason of the previous sample: a decision is logged when it
    // changes, not on every event (a 1000 Hz mouse would flood the log).
    SkipReason m_lastReason = SkipReason::None;

    // Record one decision (mutex held by the caller). Bounded by kDecisionMax.
    void noteDecision(const Sample& sample, SkipReason reason, bool onEdge,
                      const EdgeVerdict& verdict, int monitorIndex);
    // Count a skip and log its reason when it is new (mutex held).
    void noteSkip(SkipReason reason, const Sample& sample, int monitorIndex,
                  const EdgeVerdict* verdict = nullptr);
};

} // namespace cw
