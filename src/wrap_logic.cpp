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

} // namespace

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
    Topology fresh;
    fresh.initialize(monitors, m_settings.adjacencyTolerancePx);
    m_topology = std::move(fresh);
    ++m_stats.rebuilt;
    m_history.clear();
    m_hasPrevious = false;
    m_hasLastWrap = false;
    m_previous = { kNever, kNever };
    m_lastWrap = { kNever, kNever };
}

void WrapEngine::resetState()
{
    std::lock_guard lock(m_mutex);
    m_history.clear();
    m_hasPrevious = false;
    m_hasLastWrap = false;
    m_previous = { kNever, kNever };
    m_lastWrap = { kNever, kNever };
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
    if (!m_hasLastWrap)
        return false;
    const int64_t dx = x - m_lastWrap.x;
    const int64_t dy = y - m_lastWrap.y;
    const int64_t guard = m_settings.wrapGuardPx;
    return dx * dx + dy * dy <= guard * guard;
}

void WrapEngine::noteSample(const Sample& sample)
{
    m_history.push_back(sample);
    if (m_history.size() > kHistoryMax)
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - kHistoryMax));
}

long long WrapEngine::approachSpeedPps(EdgeType edge, int64_t edgeCoord) const
{
    // The impact speed of the LAST continuous stroke toward the edge,
    // clipped to the approach band. Three de-jitter mechanisms:
    //
    // 1. Approach band: the stroke is cut at the first sample farther than
    //    impactBandPx from the edge, so a fast flight that braked before
    //    the band measures by what happened near the edge, not en route.
    // 2. Stroke split: the walk stops at the first reversal or pause, so
    //    bounce-then-hit sequences measure the last hit, not an average
    //    diluted by the bounce.
    // 3. Minimal travel: a stroke shorter than kImpactMinTravelPx is
    //    jitter at the edge, not a hit — fail closed.
    //
    // Fail closed otherwise: fewer than two timestamped samples, a
    // non-monotonic clock or a zero window all mean "cannot measure".
    if (m_history.size() < 2)
        return 0;
    const Sample& newest = m_history.back();
    if (newest.tUs <= 0)
        return 0;
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

    // Walk back over the last continuous toward-the-edge stroke, inside the
    // window and inside the band.
    size_t i = m_history.size() - 1;
    while (i > 0) {
        const Sample& prev = m_history[i - 1];
        if (prev.tUs <= 0 || newest.tUs - prev.tUs > kImpactWindowUs)
            break; // clock gap or window end: the stroke starts at i
        if (travel(prev, m_history[i]) < 0)
            break; // reversed: the stroke toward the edge starts at i
        if (std::llabs(coord(prev, edge) - edgeCoord) > band)
            break; // left the approach band: the stroke starts at i
        --i;
    }
    const Sample& first = m_history[i];
    if (first.tUs <= 0 || newest.tUs <= first.tUs)
        return 0;
    const long long stroke = travel(first, newest);
    if (stroke < kImpactMinTravelPx)
        return 0; // jitter, not a hit
    const long long windowUs = newest.tUs - first.tUs;
    if (windowUs <= 0)
        return 0;
    const long long pps = stroke * 1'000'000LL / windowUs;
    return pps > 0 ? pps : 0;
}

std::pair<int64_t, int64_t> WrapEngine::onMouseMove(const Sample& sample, bool modifierHeld,
                                                    bool leftButtonHeld)
{
    std::lock_guard lock(m_mutex);
    ++m_stats.moves;
    noteSample(sample);

    const auto skip = [&]() {
        ++m_stats.suppressed;
        m_previous = sample;
        m_hasPrevious = true;
        return std::pair { sample.x, sample.y };
    };

    if (!m_topology.monitorCount())
        return skip();
    if (m_settings.disableOnSingleMonitor && m_topology.monitorCount() <= 1)
        return skip();
    if (m_settings.disableDuringDrag && leftButtonHeld)
        return skip();
    if (m_settings.activation != ActivationMode::Always && !modifierHeld)
        return skip();
    if (withinGuard(sample.x, sample.y))
        return skip();
    if (m_hasLastWrap && !withinGuard(sample.x, sample.y))
        m_hasLastWrap = false;

    const int index = m_topology.monitorAt(sample.x, sample.y);
    if (index < 0) {
        // Off every monitor (should not happen for real pointers).
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }

    const CursorDirection dir = direction(sample);
    EdgeType edge = EdgeType::Left;
    if (!m_topology.onOuterEdge(index, sample.x, sample.y, edge, m_settings.wrapMode, &dir,
                                m_settings.edgeThresholdPx))
        return skip();

    // Impact gate: reaching the edge must look like a hit, not a drift.
    // Measured over the last continuous stroke, inside the approach band;
    // a sample without a timestamp (tUs == 0) cannot prove speed and fails
    // closed.
    if (m_settings.impactGate) {
        const MonitorBox box = m_topology.monitors()[static_cast<size_t>(index)];
        const int64_t edgeCoord = (edge == EdgeType::Left || edge == EdgeType::Top)
            ? (edge == EdgeType::Left ? box.left : box.top)
            : (edge == EdgeType::Right ? box.right - 1 : box.bottom - 1);
        const long long pps = approachSpeedPps(edge, edgeCoord);
        if (pps < m_settings.impactSpeedPps)
            return skip();
    }

    const auto [nx, ny] = m_topology.wrapDestination(index, sample.x, sample.y, edge,
                                                     m_settings.wrapMode);
    m_previous = sample;
    m_hasPrevious = true;
    if (nx != sample.x || ny != sample.y) {
        m_lastWrap = { nx, ny };
        m_hasLastWrap = true;
        ++m_stats.wraps;
        m_history.clear(); // the landing point starts a fresh movement
        return { nx, ny };
    }
    return { sample.x, sample.y };
}

Stats WrapEngine::stats() const
{
    std::lock_guard lock(m_mutex);
    return m_stats;
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
    (void)merge; // partial updates: untouched fields keep `previous` above
}

} // namespace cw
