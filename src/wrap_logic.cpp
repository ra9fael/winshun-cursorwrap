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
    case SkipReason::Taskbar: return "taskbar";
    }
    return "unknown";
}

const char* accelIssueName(AccelIssue issue)
{
    switch (issue) {
    case AccelIssue::Off: return "off";
    case AccelIssue::Ok: return "ok";
    case AccelIssue::FewSamples: return "few-samples";
    case AccelIssue::WindowTooShort: return "window-too-short";
    case AccelIssue::Reversal: return "reversal";
    case AccelIssue::HalfSamples: return "half-one-sample";
    case AccelIssue::HalfTime: return "half-too-short";
    case AccelIssue::HalfTravel: return "half-travel-too-short";
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
    // The trace only means anything under the gate it was measured with. Every
    // arrival stores the verdict made under the settings of its moment, so
    // keeping rows across a gate/band/accel change mixes eras — the histogram
    // then shows slow rows recorded by an old, lower gate next to a "gate 3000"
    // line, and the suggestion argues with the setting it is supposed to tune.
    // Normal mode (the gate off) never reads this trace; clearing it costs
    // nothing but a few rows of tuner history.
    const bool impactChanged = settings.impactGate != m_settings.impactGate
        || settings.impactSpeedPps != m_settings.impactSpeedPps
        || settings.impactBandPx != m_settings.impactBandPx
        || settings.impactAccelPps2 != m_settings.impactAccelPps2;
    m_settings = settings;
    if (impactChanged) {
        m_arrivals.clear();
        m_hasArrivalEdge = false;
        m_arrivalMonitor = -1;
    }
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
    m_wrapMonitor = -1;
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
    m_wrapMonitor = -1;
    m_previous = { kNever, kNever };
    m_lastWrap = { kNever, kNever };
    m_verdict = {};
    m_arrivalVerdict = {};
    m_arrivals.clear();
    m_arrivalSeq = 0;
    m_hasArrivalEdge = false;
    m_arrivalMonitor = -1;
    m_decisions.clear();
    m_decisionSeq = 0;
    m_wrapEvent = {};
    m_wrapSeq = 0;
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
    d.measuredAccelPps2 = verdict.measuredAccelPps2;
    d.accelMeasured = verdict.accelMeasured;
    d.accelIssue = verdict.accelIssue;
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

bool WrapEngine::withinGuard(int64_t x, int64_t y, const Sample& sample) const
{
    // (1) SPACE — near the LANDING point. A wrap teleports the pointer, so the
    // hand is still where it was; without this the next sample wraps straight
    // back. PowerToys' CursorWrap has exactly this (WRAP_DISTANCE_THRESHOLD=50).
    //
    // The landing circle TRAPS a push at the landing edge, though: an R->L wrap
    // lands ON the right monitor's right edge, and the next L->R gesture pins
    // the pointer there — clamped, it can never travel the 50 px out of the
    // circle, so "push right at the edge" refused forever (the user's "从左到右
    // 无法穿越": their R->L test succeeded first and left the landing behind).
    // So the circle expires by TIME in both modes now: the hand's residual push
    // points AWAY from the landing edge (it was the motion that wrapped), so
    // after the cooldown nothing re-arms it but a deliberate return — and the
    // gate-off path gets the same escape the gated path has, where a pinned
    // push then measures no travel and is refused fail-closed anyway.
    static constexpr long long kLandingGuardUs = 300 * 1000;
    if (m_hasLastWrap) {
        const int64_t dx = x - m_lastWrap.x;
        const int64_t dy = y - m_lastWrap.y;
        const int64_t guard = m_settings.wrapGuardPx;
        const bool inside = dx * dx + dy * dy <= guard * guard;
        const bool expired = m_lastWrapTimeUs > 0
            && sample.tUs >= m_lastWrapTimeUs
            && sample.tUs - m_lastWrapTimeUs > kLandingGuardUs;
        if (inside && !expired)
            return true;
    }
    // (2) PUSH — one continuous push is one wrap, however long the hand holds.
    // The latch is released in onMouseMove as soon as the pointer is more than
    // edgeThresholdPx inside the monitor again, so a deliberate second push
    // always re-arms. This is the direction-of-travel distinction, not a 2 px
    // distance test: the latter was the v0.6.0 bug, and was driver-jitter-sized
    // and off by one in opposite directions on the two horizontal edges.
    if (m_hasWrapEdge)
        return true;
    return false;
}

void WrapEngine::noteSample(const Sample& sample)
{
    m_history.push_back(sample);
    if (m_history.size() > kHistoryMax)
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - kHistoryMax));
}

bool WrapEngine::taskbarGuardBlocks(int monitorIndex, int64_t x, int64_t y, EdgeType edge) const
{
    if (m_settings.taskbarGuardPx < 0)
        return false; // negative disables the guard
    if (edge != EdgeType::Left && edge != EdgeType::Right)
        return false; // vertical wraps are unaffected by an appbar's band
    MonitorBox box;
    if (!m_topology.monitorRect(monitorIndex, box) || !box.hasWorkArea)
        return false;
    // A work area that is not a proper sub-rect of the monitor tells nothing
    // about an appbar: refuse to guess.
    if (box.workLeft < box.left || box.workTop < box.top
        || box.workRight > box.right || box.workBottom > box.bottom
        || box.workLeft >= box.workRight || box.workTop >= box.workBottom)
        return false;
    const int64_t widen = m_settings.taskbarGuardPx;
    // An appbar exists only where the work area pulls in from the monitor
    // rect; the guard covers that band plus `widen` pixels of the work area
    // beside it. Widening a side with no appbar would block that whole edge.
    if (box.workBottom < box.bottom && y >= box.workBottom - widen)
        return true;
    if (box.workTop > box.top && y < box.workTop + widen)
        return true;
    if (box.workLeft > box.left && x < box.workLeft + widen)
        return true;
    if (box.workRight < box.right && x >= box.workRight - widen)
        return true;
    return false;
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
    //
    // Jitter: a real mouse reports ±1 px AGAINST the direction of travel while
    // the hand pushes. v0.7.0 broke the window at the first backward step, so on
    // real hardware it collapsed to one or two samples and the slope read n/a
    // almost every time — the shell never saw this because its scripted hand
    // walks a straight line. A reversal is now "the NET travel from this sample
    // back to the arrival went backwards past kImpactAccelJitterPx", which a
    // genuine direction change still trips and a 1 px wobble does not.
    struct AccelWindow {
        size_t start = 0;
        size_t end = 0;
        bool usable = false;
        AccelIssue issue = AccelIssue::WindowTooShort;
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
            if (std::llabs(coord(prev, edge) - edgeCoord) > accelBand)
                break; // too far back to be the same gesture
            if (travel(prev, m_history[end]) < -kImpactAccelJitterPx) {
                aw.issue = AccelIssue::Reversal;
                break; // a genuine direction change: the stroke starts at j
            }
            --j;
        }
        aw.start = j;
        aw.end = end;
        aw.usable = elapsedUs(m_history[j], m_history[end]) >= kImpactAccelMinWindowUs;
        if (!aw.usable && aw.issue != AccelIssue::Reversal) {
            // The window never got long enough to mean anything: the usual
            // causes are a wide band crossed at high speed, or a report rate
            // so coarse that the whole stroke fits in a few samples.
            aw.issue = elapsedUs(m_history[j], m_history[end]) <= 0
                ? AccelIssue::FewSamples : AccelIssue::WindowTooShort;
        }
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

    // Forward acceleration: compare the fitted speed of the FIRST half of the
    // acceleration window with the fitted speed of the SECOND half.
    //
    // Each half speed is a LEAST-SQUARES slope through every sample in that
    // half. v0.7.0 claimed this in its comment but actually differenced the two
    // endpoints of each half, which puts the ±0.5 px quantisation of two single
    // samples straight into the answer; regressing position against time over
    // the half uses all of its samples, and that error falls as sqrt(n). The
    // two speeds are separated by the time between the halves' middles.
    //
    // sign convention: POSITIVE means the hand was speeding up as it approached.
    // A hand that brakes into the edge gives a negative value, which is exactly
    // what must refuse to wrap.
    if (!aw.usable) {
        out.accelIssue = m_settings.impactAccelPps2 > 0 ? aw.issue : AccelIssue::Off;
        return out;
    }
    {
        const size_t mid = aw.start + (aw.end - aw.start) / 2;
        const long long firstUs = elapsedUs(m_history[aw.start], m_history[mid]);
        const long long secondUs = elapsedUs(m_history[mid], m_history[aw.end]);
        const long long firstPx = travel(m_history[aw.start], m_history[mid]);
        const long long secondPx = travel(m_history[mid], m_history[aw.end]);
        // Each half must carry enough samples, time and travel for its slope to
        // mean something. Otherwise the honest answer is "not measurable": the
        // gate then falls back to the speed floor instead of acting on noise.
        if (mid <= aw.start || mid >= aw.end) {
            out.accelIssue = AccelIssue::HalfSamples;
            return out;
        }
        if (firstUs < kImpactAccelMinHalfUs || secondUs < kImpactAccelMinHalfUs) {
            out.accelIssue = AccelIssue::HalfTime;
            return out;
        }
        if (firstPx < kImpactAccelMinHalfTravelPx
            || secondPx < kImpactAccelMinHalfTravelPx) {
            out.accelIssue = AccelIssue::HalfTravel;
            return out;
        }
        // Least-squares slope of coordinate against time, px/s. Returns 0 when
        // the half's timestamps do not span anything (a degenerate fit).
        const auto fittedPps = [&](size_t a, size_t b) -> long long {
            long long t0 = 0;
            for (size_t k = a; k <= b; ++k) {
                if (m_history[k].tUs <= 0)
                    return 0; // a hole in the clock: no fit
                if (t0 == 0)
                    t0 = m_history[k].tUs;
            }
            const double n = static_cast<double>(b - a + 1);
            double sumT = 0, sumC = 0, sumTT = 0, sumTC = 0;
            for (size_t k = a; k <= b; ++k) {
                const double t = static_cast<double>(m_history[k].tUs - t0); // us
                const double c = static_cast<double>(coord(m_history[k], edge));
                sumT += t;
                sumC += c;
                sumTT += t * t;
                sumTC += t * c;
            }
            const double denom = n * sumTT - sumT * sumT;
            if (!(denom > 0.0))
                return 0;
            const double pxPerUs = (n * sumTC - sumT * sumC) / denom;
            return static_cast<long long>(pxPerUs * 1'000'000.0);
        };
        const long long firstPps = fittedPps(aw.start, mid);
        const long long secondPps = fittedPps(mid, aw.end);
        if (firstPps == 0 || secondPps == 0) {
            out.accelIssue = AccelIssue::FewSamples;
            return out;
        }
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
            out.accelIssue = AccelIssue::Ok;
        } else {
            out.accelIssue = AccelIssue::WindowTooShort;
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
    // The verdict is PER SAMPLE, never patched: it is what the overlay and the
    // data window read. Reset before any early exit so a missing monitor, drag,
    // modifier refusal, or guard cannot inherit a previous sample's wrapped flag.
    m_verdict = EdgeVerdict {};

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
    // Release the "still pressed against that edge" latch as soon as the pointer
    // is more than edgeThresholdPx INSIDE the monitor again: the push is over.
    // This must happen HERE, before the guard can swallow the sample, or the
    // latch could never be released by anything a hand pressing on an edge
    // produces (the v0.6.0 bug).
    if (m_hasWrapEdge) {
        const MonitorBox box = m_topology.monitors()[static_cast<size_t>(m_wrapMonitor)];
        const int64_t inset = std::max<int64_t>(m_settings.edgeThresholdPx, 1);
        bool backInside = false;
        switch (m_wrapEdge) {
        case EdgeType::Left: backInside = sample.x > box.left + inset; break;
        case EdgeType::Right: backInside = sample.x < box.right - 1 - inset; break;
        case EdgeType::Top: backInside = sample.y > box.top + inset; break;
        case EdgeType::Bottom: backInside = sample.y < box.bottom - 1 - inset; break;
        }
        if (backInside) {
            m_hasWrapEdge = false;
            m_wrapMonitor = -1;
        }
    }
    if (withinGuard(sample.x, sample.y, sample)) {
        m_verdict.tUs = sample.tUs;
        m_verdict.x = sample.x;
        m_verdict.y = sample.y;
        m_verdict.monitorIndex = m_topology.nearestMonitor(sample.x, sample.y);
        m_verdict.guardBlocked = true;
        // The guard swallow must not also swallow the next arrival: the hand is
        // still pressing the same edge, and when the latch finally releases the
        // new push must be recorded as a NEW arrival, not merged into the old
        // row (which carried a different speed and a different verdict).
        m_hasArrivalEdge = false;
        return skip(SkipReason::Guard);
    }
    // Past the guard: the pointer is neither near the landing point nor on an
    // outer edge, so the next arrival is a genuine new one.
    m_hasLastWrap = false;
    m_hasWrapEdge = false;
    m_wrapMonitor = -1;
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

    // Over the taskbar (or another appbar): the pointer is on appbar area, and
    // the wrap would carry the hand's push out of it mid-interaction. Only the
    // horizontal edges: a vertical wrap out of the taskbar band is where the
    // pointer cannot be anyway (the band runs along the horizontal edges).
    if (taskbarGuardBlocks(index, sample.x, sample.y, edge))
        return skip(SkipReason::Taskbar, index);

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
        m_verdict.accelIssue = measure.accelIssue;
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
        m_verdict.accelIssue = AccelIssue::Off;
    }

    // Latch the arrival verdict. The per-sample verdict is gone with the next
    // sample, so a 60 ms poll of the readout usually missed it entirely; the
    // latched one stays until the next edge arrival and is what the readout
    // actually shows.
    m_arrivalVerdict = m_verdict;

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
        a.measuredAccelPps2 = m_verdict.measuredAccelPps2;
        a.accelMeasured = m_verdict.accelMeasured;
        a.accelIssue = m_verdict.accelIssue;
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
        m_lastWrapTimeUs = sample.tUs;
        m_hasWrapEdge = true;
        m_wrapEdge = edge;
        m_wrapMonitor = index;
        // Start the cooldown. This is the whole "one push, one wrap" mechanism
        // now: a hand pressing on an edge keeps producing samples for tens of ms,
        // and none of them may wrap again.
        ++m_stats.wraps;
        noteDecision(sample, SkipReason::None, true, m_verdict, index);
        m_lastReason = SkipReason::None;
        m_history.clear(); // the landing point starts a fresh movement
        // Latch the wrap for the overlay's glow. The glow's consumer polls at
        // 60 ms, so it must survive the guard-blocked samples that follow; the
        // per-sample verdict cannot.
        const MonitorBox& srcBox = m_topology.monitors()[static_cast<size_t>(index)];
        ++m_wrapSeq;
        m_wrapEvent.seq = m_wrapSeq;
        m_wrapEvent.tUs = sample.tUs;
        m_wrapEvent.monitorIndex = index;
        m_wrapEvent.edge = edge;
        m_wrapEvent.edgeCoord = m_verdict.edgeCoord;
        m_wrapEvent.along = horizontal(edge) ? sample.y - srcBox.top
                                             : sample.x - srcBox.left;
        m_wrapEvent.x = nx;
        m_wrapEvent.y = ny;
        return { nx, ny };
    }
    return { sample.x, sample.y };
}

void WrapEngine::onWrapFailed()
{
    std::lock_guard lock(m_mutex);
    // Undo the record onMouseMove just made. The pointer is still at the edge the
    // hand was pushing on, so the guard must not be armed at the landing point it
    // never reached: leave it unarmed and let the next sample decide again. The
    // cooldown is cleared too — this wrap never happened.
    m_hasLastWrap = false;
    m_lastWrapTimeUs = 0;
    m_hasWrapEdge = false;
    m_wrapMonitor = -1;
    m_hasArrivalEdge = false;
    if (m_stats.wraps > 0)
        --m_stats.wraps;
    // The wrap this glow was latched for never happened: drop it, or the next
    // poll of the overlay would light an edge for a wrap the OS refused.
    m_wrapEvent = {};
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

EdgeVerdict WrapEngine::lastArrivalVerdict() const
{
    std::lock_guard lock(m_mutex);
    return m_arrivalVerdict;
}

WrapEvent WrapEngine::lastWrapEvent() const
{
    std::lock_guard lock(m_mutex);
    return m_wrapEvent;
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

// The advice rule, in one paragraph: an intentional push and an incidental
// drift produce two clusters of arrival speeds. Sort them and look for the
// widest MULTIPLICATIVE gap between consecutive values (a 700→1000 px/s step
// is a 43% jump and means nothing; 1000→3000 is a 3× jump and means two
// gestures), then put the gate at the geometric middle of that gap. Both sides
// must hold at least a sixth of the samples, so a single outlier pair cannot
// win, and a gap under 1.5× is treated as noise, not two populations. When the
// current gate already sits inside the widest gap, `fits` says so: the user
// asked why a good gate kept getting a different suggestion, and the answer
// "keep it" is worth more than a second number.
GateAdvice adviseGate(const std::vector<Arrival>& arrivals, long long currentGate)
{
    GateAdvice advice;
    std::vector<long long> speeds;
    speeds.reserve(arrivals.size());
    for (const Arrival& a : arrivals) {
        if (a.gateOff || a.measuredPps <= 0)
            continue; // unmeasurable arrivals say nothing about the gate
        speeds.push_back(a.measuredPps);
    }
    advice.considered = static_cast<int>(speeds.size());
    if (speeds.size() < 8)
        return advice; // too few arrivals to cluster
    std::sort(speeds.begin(), speeds.end());

    const size_t n = speeds.size();
    // Ignore both tails: the slowest drift and the fastest flick are outliers.
    const size_t lo = std::max<size_t>(1, n / 6);
    const size_t hi = n - n / 6;
    long long bestRatio = 0; // per mille, 1000 = same speed
    size_t bestAt = 0;
    for (size_t i = lo; i < hi; ++i) {
        const long long base = std::max<long long>(speeds[i - 1], 200);
        const long long ratio = speeds[i] * 1000 / base;
        if (ratio > bestRatio) {
            bestRatio = ratio;
            bestAt = i;
        }
    }
    if (bestAt == 0 || bestRatio < 1500)
        return advice; // no gap worth the name: one population
    const long long lower = speeds[bestAt - 1];
    const long long upper = speeds[bestAt];
    // Geometric middle, then clamped into the setting's range.
    const double mid = std::sqrt(static_cast<double>(lower) * static_cast<double>(upper));
    long long suggested = static_cast<long long>(mid);
    suggested = suggested < 500 ? 500 : (suggested > 20000 ? 20000 : suggested);
    // The gate fits when it already separates the two clusters: inside the gap
    // with a little slack for the measurement noise either side of it.
    const bool fits = currentGate > 0 && currentGate * 1000 >= lower * 900
        && currentGate * 1000 <= upper * 1100;
    if (fits) {
        advice.fits = true;
        return advice;
    }
    advice.pps = suggested;
    return advice;
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

bool settingRange(const char* key, SettingRange& out)
{
    // THE one range table. applySettings rejects values outside these with
    // the same numbers; the parser and the tuning window clamp typed input
    // with them.
    const std::string k = key;
    if (k == "edgeThresholdPx") { out = { 1, 20 }; return true; }
    if (k == "wrapGuardPx") { out = { 10, 200 }; return true; }
    if (k == "adjacencyTolerancePx") { out = { 10, 200 }; return true; }
    if (k == "taskbarGuardPx") { out = { -1, 500 }; return true; }
    if (k == "impactSpeedPps") { out = { 500, 20000 }; return true; }
    if (k == "impactBandPx") { out = { 50, 500 }; return true; }
    if (k == "impactAccelPps2") { out = { 0, 200000 }; return true; }
    return false;
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
    if (raw.hasImpactFlash)
        out.impactFlash = raw.impactFlash;

    auto applyNumber = [&](double v, bool has, const char* key, int def, int prev) {
        if (!has)
            return prev;
        SettingRange range;
        if (!std::isfinite(v) || !settingRange(key, range) || v < range.lo || v > range.hi)
            return def;
        return static_cast<int>(v);
    };
    out.edgeThresholdPx = applyNumber(raw.edgeThresholdPx, raw.hasEdgeThreshold, "edgeThresholdPx",
                                      fallback.edgeThresholdPx, previous.edgeThresholdPx);
    out.wrapGuardPx = applyNumber(raw.wrapGuardPx, raw.hasWrapGuard, "wrapGuardPx",
                                  fallback.wrapGuardPx, previous.wrapGuardPx);
    out.adjacencyTolerancePx = applyNumber(raw.adjacencyTolerancePx, raw.hasAdjacency,
                                           "adjacencyTolerancePx",
                                           fallback.adjacencyTolerancePx,
                                           previous.adjacencyTolerancePx);
    out.taskbarGuardPx = applyNumber(raw.taskbarGuardPx, raw.hasTaskbarGuard, "taskbarGuardPx",
                                     fallback.taskbarGuardPx, previous.taskbarGuardPx);
    out.impactSpeedPps = applyNumber(raw.impactSpeedPps, raw.hasImpactSpeed, "impactSpeedPps",
                                     fallback.impactSpeedPps, previous.impactSpeedPps);
    out.impactBandPx = applyNumber(raw.impactBandPx, raw.hasImpactBand, "impactBandPx",
                                   fallback.impactBandPx, previous.impactBandPx);
    // 0 is meaningful here (acceleration condition off), so it must survive the
    // clamp: the range is 0..200000 rather than 200..200000.
    //
    // The credible floor is 200 px/s2, not the old 2000+: measured, a hand
    // holding a constant speed reads 0 at 125, 500 and 1000 Hz (the least-squares
    // half slopes cancel exactly), while a firm flick reads 1600-2000. So anything
    // in the low hundreds already separates "speeding up" from "steady", and a
    // higher floor would refuse normal flicks — which is what "the numbers are not
    // usable" meant.
    out.impactAccelPps2 = applyNumber(raw.impactAccelPps2, raw.hasImpactAccel, "impactAccelPps2",
                                      fallback.impactAccelPps2, previous.impactAccelPps2);
    if (out.impactAccelPps2 != 0 && out.impactAccelPps2 < 200)
        out.impactAccelPps2 = 200; // below this is noise, not a usable floor
    (void)merge; // partial updates: untouched fields keep `previous` above
}

} // namespace cw
