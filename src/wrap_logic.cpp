// wrap_logic.cpp — see wrap_logic.h.
#include "wrap_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cw {
namespace {

int64_t iabs(int64_t v) { return v < 0 ? -v : v; }

std::string modeName(WrapMode m)
{
    switch (m) {
    case WrapMode::Both: return "both";
    case WrapMode::VerticalOnly: return "vertical";
    case WrapMode::HorizontalOnly: return "horizontal";
    }
    return "both";
}

std::string activationName(ActivationMode a)
{
    switch (a) {
    case ActivationMode::Always: return "always";
    case ActivationMode::WhileCtrl: return "ctrl";
    case ActivationMode::WhileShift: return "shift";
    }
    return "always";
}

} // namespace

WrapEngine::WrapEngine(Settings settings)
    : m_settings(settings)
{
}

void WrapEngine::setSettings(const Settings& settings)
{
    m_settings = settings;
}

void WrapEngine::rebuild(const std::vector<MonitorBox>& monitors)
{
    Topology fresh;
    fresh.initialize(monitors, m_settings.adjacencyTolerancePx);
    m_topology = std::move(fresh);
    ++m_stats.rebuilt;
    resetState();
}

void WrapEngine::resetState()
{
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

std::pair<int64_t, int64_t> WrapEngine::onMouseMove(const Sample& sample, bool modifierHeld,
                                                    bool leftButtonHeld)
{
    ++m_stats.moves;

    if (!m_topology.monitorCount()) {
        ++m_stats.suppressed;
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }
    if (m_settings.disableOnSingleMonitor && m_topology.monitorCount() <= 1) {
        ++m_stats.suppressed;
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }
    if (m_settings.disableDuringDrag && leftButtonHeld) {
        ++m_stats.suppressed;
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }
    if (m_settings.activation != ActivationMode::Always && !modifierHeld) {
        ++m_stats.suppressed;
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }
    if (withinGuard(sample.x, sample.y)) {
        ++m_stats.suppressed;
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }
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
                                m_settings.edgeThresholdPx)) {
        m_previous = sample;
        m_hasPrevious = true;
        return { sample.x, sample.y };
    }

    const auto [nx, ny] = m_topology.wrapDestination(index, sample.x, sample.y, edge,
                                                     m_settings.wrapMode);
    m_previous = sample;
    m_hasPrevious = true;
    if (nx != sample.x || ny != sample.y) {
        m_lastWrap = { nx, ny };
        m_hasLastWrap = true;
        ++m_stats.wraps;
        return { nx, ny };
    }
    return { sample.x, sample.y };
}

Diagnostics WrapEngine::diagnostics(bool hookOk) const
{
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

void applySettings(const RawSettings& raw, Settings& out, bool merge)
{
    Settings fallback; // the manifest defaults
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
    (void)merge; // partial updates: untouched fields keep `previous` above
}

} // namespace cw
