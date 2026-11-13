// wrap_logic.cpp — see wrap_logic.h.
#include "wrap_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace cw {
namespace {

bool horizontal(EdgeType t) { return t == EdgeType::Left || t == EdgeType::Right; }

// A driver tick further back than this is stale or garbage rather than a
// queue, so it must not be trusted to stretch the measuring window.
constexpr long long kMaxPlausibleSpanUs = 500 * 1000;

// Elapsed time between two samples in microseconds, taking the driver's own
// tick (osMs) into account when it is available. The two spans are compared as
// unsigned 32-bit differences because MSLLHOOKSTRUCT.time is a DWORD tick that
// wraps every ~49.7 days; a plain subtraction across the wrap would produce a
// huge or negative window.
//
// Why take the maximum: tUs is read when the hook callback runs, so a queued
// event is stamped late. osMs is when the driver produced it. The true elapsed
// time is at least the driver's span, so using it can only lower the measured
// speed — never raise it. A reading that is too low merely fails closed, which
// is the safe direction; a reading that is too high wraps when the user did not
// ask for it, which is the bug this prevents.
long long elapsedUs(const Sample& a, const Sample& b)
{
    long long us = 0;
    if (a.tUs > 0 && b.tUs > a.tUs)
        us = b.tUs - a.tUs;
    if (a.osMs > 0 && b.osMs > 0) {
        const unsigned long long la = static_cast<unsigned long long>(a.osMs);
        const unsigned long long lb = static_cast<unsigned long long>(b.osMs);
        // 32-bit wrap: the difference taken modulo 2^32 is the real gap.
        const unsigned long long d32 = static_cast<unsigned>(lb - la);
        const long long osUs = static_cast<long long>(d32) * 1000LL;
        if (osUs > us && osUs <= kMaxPlausibleSpanUs)
            us = osUs;
    }
    return us;
}

} // namespace

const char* skipReasonName(SkipReason reason)
{
    switch (reason) {
    case SkipReason::None: return "wrapped";
    case SkipReason::NoMonitors: return "no-monitors";
    case SkipReason::SingleMonitor: return "single-monitor";
    case SkipReason::Dragging: return "dragging";
    case SkipReason::NoModifier: return "no-modifier";
    case SkipReason::Guard: return "guard";
    case SkipReason::OffMonitor: return "off-monitor";
    case SkipReason::NotOnEdge: return "not-on-edge";
    case SkipReason::ImpactGate: return "impact-gate";
    }
    return "unknown";
}

WrapEngine::WrapEngine(Settings settings)
    : m_settings(settings)
{
}

void WrapEngine::setSettings(const Settings& settings)
{
    std::lock_guard lock(m_mutex);
    m_settings = settings;
}

Settings WrapEngine::settings() const
{
    std::lock_guard lock(m_mutex);
    return m_settings;
}

void WrapEngine::rebuild(const std::vector<MonitorBox>& monitors)
{
    std::lock_guard lock(m_mutex);
    // A rebuild is also driven by WM_DISPLAYCHANGE, which an RDP session
    // fires while it is reconnecting or resizing: EnumDisplayMonitors can
    // legitimately return NOTHING for a moment there. Applying that would
    // wipe a working topology and leave the plugin inert until the next
    // rebuild — and if the session settles without another notification, for
    // the rest of the session. Keeping the last good layout is the safe
    // reading: a desktop with no displays is not a state the user can be in.
    if (monitors.empty() && m_topology.monitorCount() > 0) {
        ++m_stats.rebuildRefused;
        return;
    }
    Topology fresh;
    fresh.initialize(monitors, m_settings.adjacencyTolerancePx);
    m_topology = std::move(fresh);
    ++m_stats.rebuilt;
    m_history.clear();
    m_hasPrevious = false;
    m_hasLastWrap = false;
    m_hasWrapEdge = false;
    m_previous = { kNever, kNever };
    m_lastWrap = { kNever, kNever };
}

void WrapEngine::resetState()
{
    std::lock_guard lock(m_mutex);
    m_history.clear();
    m_hasPrevious = false;
    m_hasLastWrap = false;
    m_hasWrapEdge = false;
    m_previous = { kNever, kNever };
    m_lastWrap = { kNever, kNever };
    m_verdict = {};
    m_arrivals.clear();
    m_arrivalSeq = 0;
    m_hasArrivalEdge = false;
    m_arrivalMonitor = -1;
    m_decisions.clear();
    m_decisionSeq = 0;
    m_hasLastSample = false;
    m_lastSample = { kNever, kNever };
    m_lastReason = SkipReason::None;
}

void WrapEngine::noteDecision(const Sample& sample, SkipReason reason, bool onEdge,
                              const EdgeVerdict& verdict, int monitorIndex)
{
    Decision d;
    d.seq = ++m_decisionSeq;
    d.tUs = sample.tUs;
    d.x = sample.x;
    d.y = sample.y;
    d.monitorIndex = monitorIndex;
    d.edge = verdict.edge;
    d.onEdge = onEdge;
    d.measuredPps = verdict.measuredPps;
    d.gatePps = verdict.thresholdPps;
    d.reason = reason;
    d.wrapped = reason == SkipReason::None;
    m_decisions.push_back(d);
    if (m_decisions.size() > kDecisionMax)
        m_decisions.erase(m_decisions.begin(),
                          m_decisions.begin() + (m_decisions.size() - kDecisionMax));
}

void WrapEngine::noteSkip(SkipReason reason, const Sample& sample, int monitorIndex,
                          const EdgeVerdict* verdict)
{
    ++m_stats.suppressed;
    const int index = static_cast<int>(reason);
    if (index >= 0 && index < kSkipReasonCount)
        ++m_stats.skipped[index];
    // Log the first sample of a run, not every event of it: a slow drift
    // along the bottom edge is one "not-on-edge", not four hundred.
    if (reason != m_lastReason) {
        const EdgeVerdict none {};
        noteDecision(sample, reason, verdict != nullptr, verdict ? *verdict : none,
                     monitorIndex);
    }
    m_lastReason = reason;
}

std::vector<Decision> WrapEngine::decisions() const
{
    std::lock_guard lock(m_mutex);
    return m_decisions;
}

bool WrapEngine::lastSample(Sample& out) const
{
    std::lock_guard lock(m_mutex);
    if (!m_hasLastSample)
        return false;
    out = m_lastSample;
    return true;
}

CursorDirection WrapEngine::direction(const Sample& s) const
{
    CursorDirection d;
    if (m_hasPrevious) {
        d.dx = s.x - m_previous.x;
        d.dy = s.y - m_previous.y;
    }
    return d;
}

bool WrapEngine::withinGuard(int64_t x, int64_t y) const
{
    // Near the LANDING point: a wrap teleports the pointer, so the hand is still
    // where it was. Without this the next sample wraps straight back.
    if (m_hasLastWrap) {
        const int64_t dx = x - m_lastWrap.x;
        const int64_t dy = y - m_lastWrap.y;
        const int64_t guard = m_settings.wrapGuardPx;
        if (dx * dx + dy * dy <= guard * guard)
            return true;
    }
    // The pointer has not yet LEFT the edge it wrapped at. This is the case the
    // user reported: holding the pointer at x=1919 keeps producing samples, and
    // each one is a fresh "arrival" unless the plugin demands that the pointer
    // leave first. Measured before this check: every event at the edge wrapped,
    // and sliding ALONG the bottom edge wrapped repeatedly although nothing was
    // arriving from outside.
    //
    // "Left" is measured against the approach band, not against the single edge
    // line: a hand that is still within impactBandPx of the same edge has not
    // gone anywhere, while a deliberate move of more than that is a new
    // approach. Without the band the check would either latch forever (never
    // wraps again) or fire on every pixel of a slow slide.
    if (m_hasWrapEdge) {
        // Still within the edge BAND of the edge that wrapped? Then the pointer
        // has not left it and this sample is the same arrival, not a new one.
        //
        // The test is per-axis and small: only the perpendicular distance to
        // that one edge matters. Using the wide impactBandPx here would swallow
        // a deliberate move to a DIFFERENT edge of the same monitor (a corner
        // turn), which is a genuine second arrival and must wrap.
        const int64_t away = (m_wrapEdge == EdgeType::Left || m_wrapEdge == EdgeType::Right)
            ? std::llabs(x - m_wrapEdgeCoord)
            : std::llabs(y - m_wrapEdgeCoord);
        const int64_t slack = std::max<int64_t>(m_settings.edgeThresholdPx, 2);
        if (away <= slack)
            return true; // still on that edge: not a new arrival
    }
    return false;
}

void WrapEngine::noteSample(const Sample& sample)
{
    m_history.push_back(sample);
    if (m_history.size() > kHistoryMax)
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - kHistoryMax));
}

WrapEngine::ApproachMeasure WrapEngine::approachSpeedPps(EdgeType edge,
                                                         int64_t edgeCoord) const
{
    // The speed of the FREE motion that arrived at the edge — not the speed
    // of the arrival event itself. Four mechanisms:
    //
    // 1. Clamped tail: Windows clamps the pointer to the desktop, so once
    //    the hand reaches an edge every further event repeats the same
    //    coordinate. Those samples are the wall, not deceleration; averaging
    //    them in drags a genuine 2000 px/s hit down to 1787, 1625, … 0 while
    //    the hand keeps pushing. Drop the repeats first.
    // 2. Approach band: the stroke is cut at the first sample farther than
    //    impactBandPx from the edge, so a fast flight that braked before
    //    the band measures by what happened near the edge, not en route.
    // 3. Stroke split: the walk stops at the first reversal or pause, so
    //    bounce-then-hit sequences measure the last hit, not an average
    //    diluted by the bounce.
    // 4. Minimal travel: a stroke shorter than kImpactMinTravelPx is
    //    jitter at the edge, not a hit — fail closed.
    //
    // Fail closed otherwise: fewer than two timestamped samples, a
    // non-monotonic clock or a zero window all mean "cannot measure".
    if (m_history.size() < 2)
        return {};
    const auto coord = [](const Sample& s, EdgeType e) -> long long {
        return horizontal(e) ? s.x : s.y;
    };
    // +1 when approaching means increasing coordinates (Right/Bottom),
    // -1 when decreasing (Left/Top).
    const int dir = (edge == EdgeType::Left || edge == EdgeType::Top) ? -1 : 1;
    const auto travel = [&coord, dir, edge](const Sample& a, const Sample& b) {
        return (coord(b, edge) - coord(a, edge)) * dir; // +: toward the edge
    };
    const long long band = m_settings.impactBandPx;

    // (1) Skip the clamped tail: samples that repeat the previous coordinate
    // because the pointer cannot move past the edge.
    size_t last = m_history.size() - 1;
    while (last > 0 && m_history[last].tUs > 0
           && coord(m_history[last], edge) == coord(m_history[last - 1], edge))
        --last;
    if (last == 0)
        return {};
    const Sample& newest = m_history[last];
    if (newest.tUs <= 0)
        return {};

    // (2) Decide whether the arrival sample is usable. When the hand runs into
    // the edge, Windows clamps the pointer, so that last step covers LESS
    // distance than the hand actually moved (measured: 15 px where the step was
    // 16, and 6 px where it was 17). Including it measures the pointer's
    // truncated path, not the hand's speed: a genuine 2000 px/s hit reads 1987
    // and a 2125 px/s hit reads 1970, so both are refused. Detect the
    // truncation as "the arrival sits on the edge line and its step is shorter
    // than the step before it", and measure to the last FREE sample instead.
    // A genuine arrival is not shorter than the step before it, so it is kept.
    size_t end = last;
    if (coord(m_history[last], edge) == edgeCoord && last >= 2) {
        const long long arrivalStep = travel(m_history[last - 1], m_history[last]);
        const long long priorStep = travel(m_history[last - 2], m_history[last - 1]);
        if (arrivalStep < priorStep)
            end = last - 1;
    }
    if (end == 0)
        return {};

    // (3)(4) Measure the SHORTEST window that is still meaningful: walk back one
    // step at a time and stop as soon as the walk carries kImpactMinTravelPx.
    // Shortest is the point — it is what separates "how fast did the hand
    // travel" from "how fast is the hand moving AT the edge", which is what the
    // user asked for: a flick that brakes into the edge must not wrap. Measured
    // on a hand braking 4000 -> 250 px/s, the whole-band average read
    // 312-2020 px/s and wrapped; the shortest window reads the arrival and
    // correctly refuses.
    //
    // The approach band stays a hard limit: including a sample from outside it
    // would put the flight back into the number, which is the original defect.
    // If the in-band part is too short to measure, the stroke genuinely cannot
    // be measured and the gate fails closed — that is the honest answer, and the
    // band is a user-facing setting precisely so it can be widened when a coarse
    // polling rate leaves too little in-band travel.
    size_t i = end;
    while (i > 0) {
        const Sample& prev = m_history[i - 1];
        if (prev.tUs <= 0 || elapsedUs(prev, m_history[end]) > kImpactWindowUs)
            break; // clock gap or window end: the stroke starts at i
        if (travel(prev, m_history[i]) < 0)
            break; // reversed: the stroke toward the edge starts at i
        if (std::llabs(coord(prev, edge) - edgeCoord) > band)
            break; // left the approach band: the stroke starts at i
        --i;
        // Enough distance to represent a speed? Then this is the window: a
        // longer one would average in the flight the user braked out of.
        if (travel(m_history[i], m_history[end]) >= kImpactMinTravelPx)
            break;
    }

    // (3b) Acceleration needs a window that is LONG IN TIME, and it must not
    // borrow the speed window: the two answer different questions.
    //
    // Measured defects this replaces, all of them the user's report:
    //   * The window widened only until each half held 4 px, which at 125 Hz is
    //     ~16 ms — two samples at the same speed. The same 800→4000 px/s flick
    //     read 0 px/s² at 125 Hz, 0 at 500 Hz and 27833 at 1000 Hz.
    //   * Widening the window in place also moved the SPEED window, so with the
    //     acceleration floor on, a genuine 2300 px/s flick reported the 40 ms
    //     average (1700 px/s) and was refused by the speed floor.
    //
    // The noise budget decides the length. Position quantises to ±0.5 px, so a
    // slope over a window T has a standard deviation of roughly
    // 0.29·sqrt(12/n)/T_half / T_half, which is ±1443 px/s² at 40 ms but ±227
    // px/s² at 100 ms (125 Hz) and ±80 px/s² (1000 Hz). A genuine flick is
    // 15000-37500 px/s², so a window of ~100 ms separates them by an order of
    // magnitude at every report rate — and 40 ms does not.
    //
    // The approach band still caps it, honestly: a hand crossing a 150 px band at
    // 8000 px/s spends only 19 ms in it, and no time window can invent the rest.
    // When that happens the window is too short to measure and the slope is
    // reported as NOT measured, so the speed floor alone decides. Fabricating a
    // slope from two quantised samples is what made the numbers untunable.
    struct AccelWindow {
        size_t start = 0;
        size_t end = 0;
        bool usable = false;
    } aw;
    if (m_settings.impactAccelPps2 > 0) {
        // The acceleration window may reach FURTHER BACK than the speed band.
        // Sharing the band capped the window at the same distance, so a narrow
        // band meant a window too short to fit a slope — and before this was
        // fixed, "unmeasurable" meant "the floor does not apply", so a narrow
        // band made the gate MORE permissive. The slope only needs the hand to
        // have been travelling toward the edge; how close it was when measured is
        // the speed window's business.
        const long long accelBand =
            std::max<int64_t>(band, kImpactAccelMaxBandPx);
        size_t j = end;
        while (j > 0) {
            const Sample& prev = m_history[j - 1];
            if (prev.tUs <= 0)
                break; // clock gap: the stroke starts at j
            if (elapsedUs(prev, m_history[end]) > kImpactAccelWindowUs)
                break; // the window is full
            if (travel(prev, m_history[j]) < 0)
                break; // reversed
            if (std::llabs(coord(prev, edge) - edgeCoord) > accelBand)
                break; // too far back to be the same gesture
            --j;
        }
        aw.start = j;
        aw.end = end;
        aw.usable = elapsedUs(m_history[j], m_history[end]) >= kImpactAccelMinWindowUs;
    }

    // (5) Minimal travel: a stroke shorter than kImpactMinTravelPx is jitter at
    // the edge, not a hit — fail closed.
    const long long stroke = travel(m_history[i], m_history[end]);
    if (stroke < kImpactMinTravelPx)
        return {};
    const long long windowUs = elapsedUs(m_history[i], m_history[end]);
    if (windowUs <= 0)
        return {};
    // Queued-delivery guard: the samples may have been stamped when they were
    // DELIVERED rather than when they happened, which inflates the speed by an
    // arbitrary factor. MSLLHOOKSTRUCT.time carries the driver's own tick, so
    // elapsedUs already takes the larger span, and a queue can only make a
    // reading slower, never faster.
    const long long pps = stroke * 1'000'000LL / windowUs;
    if (pps <= 0)
        return {};
    ApproachMeasure out;
    // Nobody moves a mouse faster than kImpactSpeedMaxPps. A larger value
    // means the timestamps do not describe hand motion — the usual cause is
    // queued delivery. Clamping keeps the reading honest
    // instead of printing a six-digit number, and the gate still passes it.
    out.speedPps = pps > kImpactSpeedMaxPps ? kImpactSpeedMaxPps : pps;

    // Forward acceleration: the slope of speed against time over the FIXED
    // acceleration window, fitted by least squares through every sample in it.
    //
    // A two-point, two-half difference was the previous approach and it is why
    // the number could not be tuned: at 1000 Hz each half is ~20 ms of samples
    // that quantise to 1 px steps, so the difference of two averages bounced
    // between -2500 and +27833 px/s² for a hand that was, in one case,
    // accelerating at a genuine 5000 px/s² and in another holding a constant
    // speed. Regressing position against time over the whole window uses all the
    // samples instead of two, so quantisation averages out (about sqrt(n)) and
    // the sign is stable.
    //
    // sign convention: POSITIVE means the hand was speeding up as it approached.
    // A hand that brakes into the edge gives a negative value, which is exactly
    // what must refuse to wrap.
    // Forward acceleration: compare the fitted speed of the FIRST half of the
    // acceleration window with the fitted speed of the SECOND half.
    //
    // Each half speed is a least-squares slope through every sample in that half,
    // which is what suppresses quantisation: the standard deviation of that slope
    // falls as sqrt(12/n), so ±0.5 px steps become a usable number once a half
    // holds a handful of samples. Differencing two ENDPOINTS of the same window
    // (the previous approach) put the whole ±0.5 px straight into the answer.
    //
    // sign convention: POSITIVE means the hand was speeding up as it approached.
    // A hand that brakes into the edge gives a negative value, which is exactly
    // what must refuse to wrap.
    if (aw.usable) {
        const size_t mid = aw.start + (aw.end - aw.start) / 2;
        const long long firstUs = elapsedUs(m_history[aw.start], m_history[mid]);
        const long long secondUs = elapsedUs(m_history[mid], m_history[aw.end]);
        const long long firstPx = travel(m_history[aw.start], m_history[mid]);
        const long long secondPx = travel(m_history[mid], m_history[aw.end]);
        // Each half must carry enough samples, time and travel for its slope to
        // mean something. Otherwise the honest answer is "not measurable": the
        // gate then falls back to the speed floor instead of acting on noise.
        if (mid > aw.start && mid < aw.end && firstUs >= kImpactAccelMinHalfUs
            && secondUs >= kImpactAccelMinHalfUs
            && firstPx >= kImpactAccelMinHalfTravelPx
            && secondPx >= kImpactAccelMinHalfTravelPx) {
            const long long firstPps = firstPx * 1'000'000LL / firstUs;
            const long long secondPps = secondPx * 1'000'000LL / secondUs;
            // The two speeds are separated by the time between the MIDDLES of the
            // halves, which is (firstUs + secondUs) / 2.
            const long long dtUs = (firstUs + secondUs) / 2;
            if (dtUs > 0) {
                long long accel = (secondPps - firstPps) * 1'000'000LL / dtUs;
                if (accel > kImpactAccelMaxPps2)
                    accel = kImpactAccelMaxPps2;
                if (accel < -kImpactAccelMaxPps2)
                    accel = -kImpactAccelMaxPps2;
                out.accelPps2 = accel;
                out.accelMeasured = true;
            }
        }
    }
    return out;
}

std::pair<int64_t, int64_t> WrapEngine::onMouseMove(const Sample& sample, bool modifierHeld,
                                                    bool leftButtonHeld)
{
    std::lock_guard lock(m_mutex);
    ++m_stats.moves;
    noteSample(sample);
    m_lastSample = sample;
    m_hasLastSample = true;

    // Every exit funnels through here so the per-reason counters see every
    // move: "it does not wrap at all" is only answerable when the diagnostic
    // says WHICH gate keeps saying no.
    const auto skip = [&](SkipReason reason, int monitorIndex = -1,
                          const EdgeVerdict* verdict = nullptr) {
        noteSkip(reason, sample, monitorIndex, verdict);
        m_previous = sample;
        m_hasPrevious = true;
        return std::pair { sample.x, sample.y };
    };

    if (!m_topology.monitorCount())
        return skip(SkipReason::NoMonitors);
    const int monitorCount = static_cast<int>(m_topology.monitorCount());
    if (m_settings.disableOnSingleMonitor && monitorCount <= 1)
        return skip(SkipReason::SingleMonitor);
    if (m_settings.disableDuringDrag && leftButtonHeld)
        return skip(SkipReason::Dragging);
    if (m_settings.activation != ActivationMode::Always && !modifierHeld)
        return skip(SkipReason::NoModifier);
    if (withinGuard(sample.x, sample.y)) {
        m_verdict.guardBlocked = true;
        return skip(SkipReason::Guard);
    }
    // Past the guard: the pointer is neither near the landing point nor on an
    // outer edge, so the next arrival is a genuine new one.
    m_hasLastWrap = false;
    m_hasWrapEdge = false;
    const int index = m_topology.nearestMonitor(sample.x, sample.y);
    if (index < 0)
        return skip(SkipReason::OffMonitor); // no monitors at all

    const CursorDirection dir = direction(sample);
    EdgeType edge = EdgeType::Left;
    if (!m_topology.onOuterEdge(index, sample.x, sample.y, edge, m_settings.wrapMode, &dir,
                                m_settings.edgeThresholdPx)) {
        // Left the edge (or never reached one): the next arrival is a new one.
        m_hasArrivalEdge = false;
        return skip(SkipReason::NotOnEdge, index);
    }

    // Impact gate: reaching the edge must look like a hit, not a drift.
    // Measured over the last continuous stroke, inside the approach band; a
    // sample without a timestamp (tUs == 0) cannot prove speed and fails closed.
    //
    // Two conditions, both mandatory when enabled:
    //   1. the speed at the arrival must reach impactSpeedPps;
    //   2. the hand must be SPEEDING UP into the edge (impactAccelPps2, 0 = off).
    // The second is the user's request: a constant-speed sweep that happens to
    // be fast is a different gesture from a flick that accelerates at the edge,
    // and only the flick should wrap. It also refuses a hand that is still fast
    // but already braking, which the speed floor alone accepts.
    bool gatedOut = false;
    if (m_settings.impactGate) {
        const MonitorBox box = m_topology.monitors()[static_cast<size_t>(index)];
        const int64_t edgeCoord = (edge == EdgeType::Left || edge == EdgeType::Top)
            ? (edge == EdgeType::Left ? box.left : box.top)
            : (edge == EdgeType::Right ? box.right - 1 : box.bottom - 1);
        const ApproachMeasure measure = approachSpeedPps(edge, edgeCoord);
        const long long pps = measure.speedPps;
        m_verdict.tUs = sample.tUs;
        m_verdict.monitorIndex = index;
        m_verdict.edge = edge;
        m_verdict.edgeCoord = edgeCoord;
        m_verdict.x = sample.x;
        m_verdict.y = sample.y;
        m_verdict.measuredPps = pps;
        m_verdict.thresholdPps = m_settings.impactSpeedPps;
        m_verdict.measuredAccelPps2 = measure.accelPps2;
        m_verdict.accelMeasured = measure.accelMeasured;
        m_verdict.thresholdAccelPps2 = m_settings.impactAccelPps2;
        m_verdict.gateOff = false;
        m_verdict.wrapped = false;
        m_verdict.guardBlocked = false;
        m_verdict.accelBlocked = false;

        const bool tooSlow = pps < m_settings.impactSpeedPps;
        // Acceleration only applies when the user enabled it.
        //
        // MEASURABLE OR REFUSED — never "not applicable". The old rule was
        // `accelEnabled && accelMeasured && accel < floor`, so when the slope
        // could not be measured the floor simply did not apply and the move went
        // through. That inverted the setting: the slope is unmeasurable exactly
        // when the hand crosses the whole band inside the 100 ms window, i.e.
        // when it is FASTEST. Measured on the devshell with impactAccelPps2=1600:
        // a steady 12000 px/s move, a steady 18000 px/s move and even a BRAKING
        // 16000->9000 move all wrapped. That is the user's report, "with
        // acceleration set to 1600 it wraps MORE easily", and it was literally
        // true.
        //
        // So an enabled floor that cannot be checked FAILS CLOSED. The refusal is
        // honest: we cannot show the hand was speeding up, so we do not act on
        // it. The verdict carries accelMeasured = false, which the overlay and
        // the data window show as "unmeasurable" so this is diagnosable rather
        // than mysterious.
        const bool accelEnabled = m_settings.impactAccelPps2 > 0;
        const bool accelUnmeasurable = accelEnabled && !measure.accelMeasured;
        const bool accelTooLow = accelEnabled && measure.accelMeasured
            && measure.accelPps2 < m_settings.impactAccelPps2;
        gatedOut = tooSlow || accelUnmeasurable || accelTooLow;
        m_verdict.accelBlocked = !tooSlow && (accelUnmeasurable || accelTooLow);
        if (!gatedOut)
            m_verdict.wrapped = true;
    } else {
        m_verdict.tUs = sample.tUs;
        m_verdict.monitorIndex = index;
        m_verdict.edge = edge;
        m_verdict.x = sample.x;
        m_verdict.y = sample.y;
        const MonitorBox box = m_topology.monitors()[static_cast<size_t>(index)];
        m_verdict.edgeCoord = (edge == EdgeType::Left) ? box.left
            : (edge == EdgeType::Right) ? box.right - 1
            : (edge == EdgeType::Top) ? box.top : box.bottom - 1;
        m_verdict.measuredPps = 0;
        m_verdict.thresholdPps = 0;
        m_verdict.measuredAccelPps2 = 0;
        m_verdict.thresholdAccelPps2 = 0;
        m_verdict.accelBlocked = false;
        m_verdict.gateOff = true;
        m_verdict.wrapped = true;
        m_verdict.guardBlocked = false;
    }

    // The arrival itself: record it BEFORE the gate decides, so a blocked
    // drift is part of the trace too. A trace of only accepted hits could
    // never show where the two populations separate — which is precisely what
    // the suggestion needs. Repeated samples at the same edge (the pointer is
    // clamped there while the hand keeps pushing) are one arrival, not many.
    const bool newArrival = !m_hasArrivalEdge || m_arrivalEdge != edge
        || m_arrivalMonitor != index;
    if (newArrival) {
        Arrival a;
        a.seq = ++m_arrivalSeq;
        a.tUs = sample.tUs;
        a.monitorIndex = index;
        a.edge = edge;
        a.measuredPps = m_verdict.measuredPps;
        a.gatePps = m_verdict.thresholdPps;
        a.gateOff = m_verdict.gateOff;
        a.wrapped = !gatedOut;
        m_arrivals.push_back(a);
        if (m_arrivals.size() > kTraceMax)
            m_arrivals.erase(m_arrivals.begin(),
                             m_arrivals.begin() + (m_arrivals.size() - kTraceMax));
        m_arrivalEdge = edge;
        m_arrivalMonitor = index;
        m_hasArrivalEdge = true;
    }
    if (gatedOut)
        return skip(SkipReason::ImpactGate, index, &m_verdict);

    const auto [nx, ny] = m_topology.wrapDestination(index, sample.x, sample.y, edge,
                                                     m_settings.wrapMode);
    m_verdict.destX = nx;
    m_verdict.destY = ny;

    m_previous = sample;
    m_hasPrevious = true;
    if (nx != sample.x || ny != sample.y) {
        m_lastWrap = { nx, ny };
        m_hasLastWrap = true;
        // The pointer has not left the band of this edge yet: another sample at
        // the same edge is the same arrival, not a new one.
        m_hasWrapEdge = true;
        m_wrapEdge = edge;
        // The edge coordinate this wrap happened at, derived from the landing
        // side so it does not depend on a gate-only local variable.
        {
            const MonitorBox box = m_topology.monitors()[static_cast<size_t>(index)];
            m_wrapEdgeCoord = (edge == EdgeType::Left) ? box.left
                : (edge == EdgeType::Right) ? box.right - 1
                : (edge == EdgeType::Top) ? box.top : box.bottom - 1;
        }
        ++m_stats.wraps;
        noteDecision(sample, SkipReason::None, true, m_verdict, index);
        m_lastReason = SkipReason::None;
        m_history.clear(); // the landing point starts a fresh movement
        return { nx, ny };
    }
    return { sample.x, sample.y };
}

void WrapEngine::onWrapFailed()
{
    std::lock_guard lock(m_mutex);
    // Undo the record onMouseMove just made. The pointer is still at the edge the
    // hand was pushing on, so the guard must not be armed at the landing point it
    // never reached: leave it unarmed and let the next sample decide again.
    m_hasLastWrap = false;
    m_hasWrapEdge = false;
    m_hasArrivalEdge = false;
    if (m_stats.wraps > 0)
        --m_stats.wraps;
    // A failed wrap is not an arrival for the tuner's histogram: it must not
    // teach suggestGate that this speed wraps.
    if (!m_arrivals.empty())
        m_arrivals.pop_back();
    if (m_arrivalSeq > 0)
        --m_arrivalSeq;
    // The hand is still moving toward the edge, so keep the samples: the next
    // event is a fresh arrival at the same place and must be able to wrap.
    m_lastReason = SkipReason::None;
}

Stats WrapEngine::stats() const
{
    return m_stats;
}

EdgeVerdict WrapEngine::lastVerdict() const
{
    std::lock_guard lock(m_mutex);
    return m_verdict;
}

std::vector<MonitorBox> WrapEngine::monitorsSnapshot() const
{
    std::lock_guard lock(m_mutex);
    return m_topology.monitors();
}

std::vector<MonitorEdge> WrapEngine::outerEdgesSnapshot() const
{
    std::lock_guard lock(m_mutex);
    return m_topology.outerEdges();
}

std::vector<Arrival> WrapEngine::arrivals() const
{
    std::lock_guard lock(m_mutex);
    return m_arrivals;
}

void WrapEngine::clearArrivals()
{
    std::lock_guard lock(m_mutex);
    m_arrivals.clear();
}

// The suggestion rule, in one paragraph: an intentional push and an incidental
// drift produce two clusters of arrival speeds. Sort them, find the widest
// relative gap between consecutive distinct values, and put the gate in the
// middle of that gap. With no gap worth the name (one cluster only) return 0
// rather than inventing a threshold — a wrong threshold feels more broken than
// no suggestion.
long long suggestGate(const std::vector<Arrival>& arrivals)
{
    std::vector<long long> speeds;
    speeds.reserve(arrivals.size());
    for (const Arrival& a : arrivals) {
        if (a.gateOff || a.measuredPps <= 0)
            continue; // unmeasurable arrivals say nothing about the gate
        speeds.push_back(a.measuredPps);
    }
    if (speeds.size() < 6)
        return 0; // too few arrivals to cluster
    std::sort(speeds.begin(), speeds.end());

    // Widest relative gap between consecutive values, ignoring both tails:
    // the slowest drift and the fastest flick are outliers, not clusters.
    const size_t lo = std::max<size_t>(1, speeds.size() / 10);
    const size_t hi = speeds.size() - speeds.size() / 10;
    long long bestGap = 0;
    size_t bestAt = 0;
    for (size_t i = lo; i < hi; ++i) {
        const long long gap = speeds[i] - speeds[i - 1];
        const long long base = std::max<long long>(speeds[i - 1], 200);
        const long long relative = gap * 1000 / base; // per mille
        if (relative > bestGap) {
            bestGap = relative;
            bestAt = i;
        }
    }
    // A gap under 25% of the lower cluster is noise, not two populations.
    if (bestAt == 0 || bestGap < 250)
        return 0;
    const long long mid = (speeds[bestAt - 1] + speeds[bestAt]) / 2;
    return mid < 500 ? 500 : (mid > 20000 ? 20000 : mid);
}

Diagnostics WrapEngine::diagnostics(bool hookOk) const
{
    std::lock_guard lock(m_mutex);
    Diagnostics d;
    d.enabled = true; // the caller overlays the runtime enable flag
    d.hookOk = hookOk;
    d.wrapMode = static_cast<int>(m_settings.wrapMode);
    d.activation = static_cast<int>(m_settings.activation);
    d.monitors = static_cast<int>(m_topology.monitorCount());
    d.outerEdges = static_cast<int>(m_topology.outerEdges().size());
    d.settings = m_settings;
    d.stats = m_stats;
    for (const auto& gap : m_topology.detectGaps(m_settings.adjacencyTolerancePx)) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
                      "gap between display %d and %d: %lld px, overlap %lld px",
                      gap.a, gap.b, static_cast<long long>(gap.horizontalGap),
                      static_cast<long long>(gap.verticalOverlap));
        d.gaps.push_back(buf);
    }
    return d;
}

size_t WrapEngine::monitorCount() const
{
    std::lock_guard lock(m_mutex);
    return m_topology.monitorCount();
}

size_t WrapEngine::outerEdgeCount() const
{
    std::lock_guard lock(m_mutex);
    return m_topology.outerEdges().size();
}

void applySettings(const RawSettings& raw, Settings& out, bool merge)
{
    const Settings fallback; // the manifest defaults
    const Settings previous = out;

    if (raw.hasWrapMode) {
        if (raw.wrapMode == "both")
            out.wrapMode = WrapMode::Both;
        else if (raw.wrapMode == "vertical")
            out.wrapMode = WrapMode::VerticalOnly;
        else if (raw.wrapMode == "horizontal")
            out.wrapMode = WrapMode::HorizontalOnly;
    }
    if (raw.hasActivation) {
        if (raw.activationMode == "always")
            out.activation = ActivationMode::Always;
        else if (raw.activationMode == "ctrl")
            out.activation = ActivationMode::WhileCtrl;
        else if (raw.activationMode == "shift")
            out.activation = ActivationMode::WhileShift;
    }
    if (raw.hasDrag)
        out.disableDuringDrag = raw.disableDuringDrag;
    if (raw.hasSingle)
        out.disableOnSingleMonitor = raw.disableOnSingleMonitor;
    if (raw.hasImpactGate)
        out.impactGate = raw.impactGate;
    if (raw.hasTuningOverlay)
        out.tuningOverlay = raw.tuningOverlay;
    if (raw.hasDataWindow)
        out.dataWindow = raw.dataWindow;

    auto applyNumber = [](double v, bool has, int lo, int hi, int def, int prev) {
        if (!has)
            return prev;
        if (!std::isfinite(v) || v < lo || v > hi)
            return def;
        return static_cast<int>(v);
    };
    out.edgeThresholdPx = applyNumber(raw.edgeThresholdPx, raw.hasEdgeThreshold, 1, 20,
                                      fallback.edgeThresholdPx, previous.edgeThresholdPx);
    out.wrapGuardPx = applyNumber(raw.wrapGuardPx, raw.hasWrapGuard, 10, 200,
                                  fallback.wrapGuardPx, previous.wrapGuardPx);
    out.adjacencyTolerancePx = applyNumber(raw.adjacencyTolerancePx, raw.hasAdjacency, 10, 200,
                                           fallback.adjacencyTolerancePx,
                                           previous.adjacencyTolerancePx);
    out.impactSpeedPps = applyNumber(raw.impactSpeedPps, raw.hasImpactSpeed, 500, 20000,
                                     fallback.impactSpeedPps, previous.impactSpeedPps);
    out.impactBandPx = applyNumber(raw.impactBandPx, raw.hasImpactBand, 50, 500,
                                   fallback.impactBandPx, previous.impactBandPx);
    // 0 is meaningful here (acceleration condition off), so it must survive the
    // clamp: the range is 0..200000 rather than 200..200000.
    //
    // The credible floor is 200 px/s², not the old 2000+: measured, a hand
    // holding a constant speed reads 0 at 125, 500 and 1000 Hz (the least-squares
    // half slopes cancel exactly), while a firm flick reads 1600-2000. So anything
    // in the low hundreds already separates "speeding up" from "steady", and a
    // higher floor would refuse normal flicks — which is what "the numbers are not
    // usable" meant.
    out.impactAccelPps2 = applyNumber(raw.impactAccelPps2, raw.hasImpactAccel, 0, 200000,
                                      fallback.impactAccelPps2, previous.impactAccelPps2);
    if (out.impactAccelPps2 != 0 && out.impactAccelPps2 < 200)
        out.impactAccelPps2 = 200; // below this is noise, not a usable floor
    (void)merge; // partial updates: untouched fields keep `previous` above
}

} // namespace cw
