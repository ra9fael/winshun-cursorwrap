// wrap_topology.cpp — see wrap_topology.h.
//
// Ported from PowerToys CursorWrap MonitorTopology.cpp (MIT, Microsoft
// Corporation), with the logging stripped and the OS dependencies (HMONITOR,
// RECT, EnumDisplayMonitors) replaced by plain structs so the engine is
// unit-testable without a display.
#include "wrap_topology.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace cw {
namespace {

int64_t iabs(int64_t v) { return v < 0 ? -v : v; }

int64_t imin(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t imax(int64_t a, int64_t b) { return a > b ? a : b; }

bool oppositeTypes(EdgeType a, EdgeType b)
{
    return (a == EdgeType::Left && b == EdgeType::Right)
        || (a == EdgeType::Right && b == EdgeType::Left)
        || (a == EdgeType::Top && b == EdgeType::Bottom)
        || (a == EdgeType::Bottom && b == EdgeType::Top);
}

bool horizontal(EdgeType t) { return t == EdgeType::Left || t == EdgeType::Right; }

EdgeType oppositeOf(EdgeType t)
{
    switch (t) {
    case EdgeType::Left: return EdgeType::Right;
    case EdgeType::Right: return EdgeType::Left;
    case EdgeType::Top: return EdgeType::Bottom;
    case EdgeType::Bottom: return EdgeType::Top;
    }
    return EdgeType::Left;
}

} // namespace

void Topology::initialize(const std::vector<MonitorBox>& monitors, int64_t adjacencyTolerance)
{
    m_monitors = monitors;
    // Deterministic order: primary first, then left/top. HMONITOR handles can
    // change on display changes; indices only need stability within one
    // snapshot, but a stable order also makes tests reproducible.
    std::stable_sort(m_monitors.begin(), m_monitors.end(), [](const MonitorBox& a, const MonitorBox& b) {
        if (a.primary != b.primary)
            return a.primary;
        if (a.left != b.left)
            return a.left < b.left;
        return a.top < b.top;
    });
    m_outerEdges.clear();
    m_edges.clear();
    if (m_monitors.empty())
        return;
    buildEdges();
    identifyOuter(adjacencyTolerance > 0 ? adjacencyTolerance : 50);
}

void Topology::buildEdges()
{
    for (size_t idx = 0; idx < m_monitors.size(); ++idx) {
        const MonitorBox& m = m_monitors[idx];
        const int i = static_cast<int>(idx);

        MonitorEdge left;
        left.monitorIndex = i;
        left.type = EdgeType::Left;
        left.position = m.left;
        left.start = m.top;
        left.end = m.bottom;
        left.outer = true;
        m_edges[{i, EdgeType::Left}] = left;

        MonitorEdge right = left;
        right.type = EdgeType::Right;
        right.position = m.right - 1;
        m_edges[{i, EdgeType::Right}] = right;

        MonitorEdge top;
        top.monitorIndex = i;
        top.type = EdgeType::Top;
        top.position = m.top;
        top.start = m.left;
        top.end = m.right;
        top.outer = true;
        m_edges[{i, EdgeType::Top}] = top;

        MonitorEdge bottom = top;
        bottom.type = EdgeType::Bottom;
        bottom.position = m.bottom - 1;
        m_edges[{i, EdgeType::Bottom}] = bottom;
    }
}

void Topology::identifyOuter(int64_t tolerance)
{
    for (auto& [key, edge] : m_edges) {
        (void)key;
        edge.outer = true;
        for (const auto& [key2, other] : m_edges) {
            (void)key2;
            if (other.monitorIndex == edge.monitorIndex)
                continue;
            if (edgesAdjacent(edge, other, tolerance)) {
                edge.outer = false;
                break;
            }
        }
        if (edge.outer)
            m_outerEdges.push_back(edge);
    }
    // Deterministic order for tests and diagnostics.
    std::sort(m_outerEdges.begin(), m_outerEdges.end(), [](const MonitorEdge& a, const MonitorEdge& b) {
        if (a.monitorIndex != b.monitorIndex)
            return a.monitorIndex < b.monitorIndex;
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    });
}

bool Topology::edgesAdjacent(const MonitorEdge& a, const MonitorEdge& b, int64_t tolerance) const
{
    if (!oppositeTypes(a.type, b.type))
        return false;
    if (iabs(a.position - b.position) > tolerance)
        return false;
    const int64_t overlapStart = imax(a.start, b.start);
    const int64_t overlapEnd = imin(a.end, b.end);
    return overlapEnd > overlapStart + tolerance;
}

const MonitorEdge* Topology::edgeOf(int monitorIndex, EdgeType type) const
{
    const auto it = m_edges.find({monitorIndex, type});
    return it == m_edges.end() ? nullptr : &it->second;
}

int Topology::monitorAt(int64_t x, int64_t y) const
{
    for (size_t i = 0; i < m_monitors.size(); ++i)
        if (m_monitors[i].contains(x, y))
            return static_cast<int>(i);
    return -1;
}

int Topology::nearestMonitor(int64_t x, int64_t y) const
{
    // For a pointer that is inside no monitor. `monitorAt` answers "which
    // rect contains this point" (exact, -1 outside — what the layout tests
    // and gap detection need); the wrap path needs "which monitor is this
    // pointer on", and a real pointer can be outside the union of the rects
    // on a stale topology, at a seam during a hot-plug, or on a display
    // Windows reports with a gap. Returning -1 there refused to wrap
    // everywhere: a real-machine run logged 1008 such samples.
    if (m_monitors.empty())
        return -1;
    int best = 0;
    int64_t bestDistance = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < m_monitors.size(); ++i) {
        const MonitorBox& m = m_monitors[i];
        const int64_t dx = x < m.left ? m.left - x : (x >= m.right ? x - (m.right - 1) : 0);
        const int64_t dy = y < m.top ? m.top - y : (y >= m.bottom ? y - (m.bottom - 1) : 0);
        const int64_t distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool Topology::monitorRect(int index, MonitorBox& out) const
{
    if (index < 0 || index >= static_cast<int>(m_monitors.size()))
        return false;
    out = m_monitors[static_cast<size_t>(index)];
    return true;
}

EdgeType Topology::prioritize(const std::vector<EdgeType>& candidates,
                              const CursorDirection* direction) const
{
    if (candidates.empty())
        return EdgeType::Left; // unreachable: callers pass non-empty lists
    if (candidates.size() == 1 || direction == nullptr)
        return candidates.front();

    const bool horiz = direction->primarilyHorizontal();
    for (const EdgeType e : candidates) {
        if (horiz) {
            if (direction->movingLeft() && e == EdgeType::Left)
                return e;
            if (direction->movingRight() && e == EdgeType::Right)
                return e;
        } else {
            if (direction->movingUp() && e == EdgeType::Top)
                return e;
            if (direction->movingDown() && e == EdgeType::Bottom)
                return e;
        }
    }
    // Fall back to any edge on the dominant axis.
    for (const EdgeType e : candidates)
        if (horizontal(e) == horiz)
            return e;
    return candidates.front();
}

bool Topology::onOuterEdge(int index, int64_t x, int64_t y, EdgeType& outEdge,
                           WrapMode mode, const CursorDirection* direction,
                           int64_t edgeThreshold) const
{
    MonitorBox box;
    if (!monitorRect(index, box))
        return false;
    const int64_t threshold = edgeThreshold < 0 ? 0 : edgeThreshold;

    std::vector<EdgeType> candidates;
    const bool verticalAllowed = mode == WrapMode::Both || mode == WrapMode::VerticalOnly;
    const bool horizontalAllowed = mode == WrapMode::Both || mode == WrapMode::HorizontalOnly;

    if (horizontalAllowed && x <= box.left + threshold) {
        if (const MonitorEdge* e = edgeOf(index, EdgeType::Left); e && e->outer)
            candidates.push_back(EdgeType::Left);
    }
    if (horizontalAllowed && x >= box.right - 1 - threshold) {
        if (const MonitorEdge* e = edgeOf(index, EdgeType::Right); e && e->outer)
            candidates.push_back(EdgeType::Right);
    }
    if (verticalAllowed && y <= box.top + threshold) {
        if (const MonitorEdge* e = edgeOf(index, EdgeType::Top); e && e->outer)
            candidates.push_back(EdgeType::Top);
    }
    if (verticalAllowed && y >= box.bottom - 1 - threshold) {
        if (const MonitorEdge* e = edgeOf(index, EdgeType::Bottom); e && e->outer)
            candidates.push_back(EdgeType::Bottom);
    }
    if (candidates.empty())
        return false;

    const EdgeType chosen = prioritize(candidates, direction);
    if (const MonitorEdge* src = edgeOf(index, chosen)) {
        const int64_t coord = horizontal(chosen) ? y : x;
        if (findOpposite(chosen, coord, *src).found) {
            outEdge = chosen;
            return true;
        }
    }
    // Corner tie-break: try the remaining candidates.
    for (const EdgeType candidate : candidates) {
        if (candidate == chosen)
            continue;
        if (const MonitorEdge* src = edgeOf(index, candidate)) {
            const int64_t coord = horizontal(candidate) ? y : x;
            if (findOpposite(candidate, coord, *src).found) {
                outEdge = candidate;
                return true;
            }
        }
    }
    return false;
}

MonitorEdge Topology::findOppositeOverlapping(EdgeType fromEdge, int64_t coordinate) const
{
    const EdgeType target = oppositeOf(fromEdge);
    const bool findMax = fromEdge == EdgeType::Left || fromEdge == EdgeType::Top;
    const MonitorEdge* best = nullptr;
    for (const MonitorEdge& e : m_outerEdges) {
        if (e.type != target)
            continue;
        if (coordinate < e.start || coordinate >= e.end)
            continue;
        if (!best || (findMax ? e.position > best->position : e.position < best->position))
            best = &e;
    }
    return best ? *best : MonitorEdge {};
}

int64_t Topology::projectCoordinate(int64_t cursorCoordinate, const MonitorEdge& source,
                                    const MonitorEdge& target)
{
    // Windows-like behaviour for non-overlapping regions: preserve the
    // coordinate inside the shared span, otherwise clamp to the nearest
    // boundary of the target edge (with a 1px inset so the cursor stays on
    // the target edge, not past it).
    const int64_t sharedStart = imax(source.start, target.start);
    const int64_t sharedEnd = imin(source.end, target.end);
    if (cursorCoordinate >= sharedStart && cursorCoordinate <= sharedEnd)
        return cursorCoordinate;
    int64_t projected = cursorCoordinate < sharedStart ? target.start + 1 : target.end - 1;
    return imax(target.start, imin(projected, target.end - 1));
}

OppositeEdge Topology::findOpposite(EdgeType fromEdge, int64_t coordinate,
                                    const MonitorEdge& sourceEdge) const
{
    OppositeEdge result;
    MonitorEdge direct = findOppositeOverlapping(fromEdge, coordinate);
    if (direct.monitorIndex >= 0) {
        result.found = true;
        result.projection = false;
        result.edge = direct;
        return result;
    }
    // No direct overlap: pick the nearest opposite edge by coordinate distance.
    const EdgeType target = oppositeOf(fromEdge);
    const bool findMax = fromEdge == EdgeType::Left || fromEdge == EdgeType::Top;
    int64_t bestDistance = std::numeric_limits<int64_t>::max();
    const MonitorEdge* bestEdge = nullptr;
    for (const MonitorEdge& e : m_outerEdges) {
        if (e.type != target)
            continue;
        int64_t distance = 0;
        if (coordinate < e.start)
            distance = e.start - coordinate;
        else if (coordinate > e.end)
            distance = coordinate - e.end;
        else
            distance = 0; // covered above; defensive
        if (distance < bestDistance
            || (distance == bestDistance && bestEdge
                && (findMax ? e.position > bestEdge->position : e.position < bestEdge->position))) {
            bestDistance = distance;
            bestEdge = &e;
        }
    }
    if (!bestEdge)
        return result;
    result.found = true;
    result.projection = true;
    result.edge = *bestEdge;
    result.projectedCoordinate = projectCoordinate(coordinate, sourceEdge, *bestEdge);
    return result;
}

std::pair<int64_t, int64_t> Topology::clampToWorkArea(int index, int64_t x, int64_t y) const
{
    MonitorBox box;
    if (!monitorRect(index, box))
        return {x, y};
    const int64_t loX = box.areaLeft();
    const int64_t hiX = box.areaRight() - 1;
    const int64_t loY = box.areaTop();
    const int64_t hiY = box.areaBottom() - 1;
    // A degenerate or missing work area (a caller that does not know it) must
    // behave exactly as before: the full monitor rect.
    if (hiX < loX || hiY < loY)
        return {x, y};
    return { imax(loX, imin(x, hiX)), imax(loY, imin(y, hiY)) };
}

std::pair<int64_t, int64_t> Topology::wrapDestination(int index, int64_t x, int64_t y,
                                                      EdgeType edge, WrapMode mode) const
{
    (void)mode; // mode filtering happens in onOuterEdge; kept for signature symmetry
    MonitorBox box;
    if (!monitorRect(index, box))
        return {x, y};
    const MonitorEdge* src = edgeOf(index, edge);
    if (!src)
        return {x, y};
    const int64_t coord = horizontal(edge) ? y : x;
    OppositeEdge opposite = findOpposite(edge, coord, *src);
    if (!opposite.found) {
        // Same-monitor wrap: cross to the opposite edge of this monitor, one
        // pixel inside it, and never onto the taskbar.
        switch (edge) {
        case EdgeType::Left: return clampToWorkArea(index, box.right - 2, y);
        case EdgeType::Right: return clampToWorkArea(index, box.left + 1, y);
        case EdgeType::Top: return clampToWorkArea(index, x, box.bottom - 2);
        case EdgeType::Bottom: return clampToWorkArea(index, x, box.top + 1);
        }
        return {x, y};
    }
    // Land one pixel inside the OTHER monitor, measured from the edge we are
    // landing on. Deriving the inset from the TARGET edge is what makes the
    // four directions symmetric and keeps the landing on the target monitor:
    // the old code offset the pressed edge instead, so pressing Top landed on
    // `bottom` — one pixel BELOW the target monitor, i.e. not on it at all —
    // and pressing Bottom landed one pixel above it. Windows then clamped the
    // pointer back, which is the "wrap feels like it sticks" the user reported.
    const MonitorEdge& target = opposite.edge;
    int64_t nx = x;
    int64_t ny = y;
    switch (target.type) {
    case EdgeType::Left: nx = target.position + 1; break;
    case EdgeType::Right: nx = target.position - 1; break;
    case EdgeType::Top: ny = target.position + 1; break;
    case EdgeType::Bottom: ny = target.position - 1; break;
    }
    if (opposite.projection) {
        if (horizontal(edge))
            ny = opposite.projectedCoordinate;
        else
            nx = opposite.projectedCoordinate;
    }
    // The landing must stay inside the target monitor's WORK area: a cursor that
    // reappears under the taskbar is a click on the taskbar, not a wrap.
    return clampToWorkArea(target.monitorIndex, nx, ny);
}

std::vector<Topology::Gap> Topology::detectGaps(int64_t gapThreshold) const
{
    std::vector<Gap> gaps;
    for (size_t i = 0; i < m_monitors.size(); ++i) {
        for (size_t j = i + 1; j < m_monitors.size(); ++j) {
            const MonitorBox& a = m_monitors[i];
            const MonitorBox& b = m_monitors[j];
            const int64_t vOverlap = imin(a.bottom, b.bottom) - imax(a.top, b.top);
            const int64_t hOverlap = imin(a.right, b.right) - imax(a.left, b.left);
            if (vOverlap > 0) {
                const int64_t gap = a.left < b.left ? b.left - a.right : a.left - b.right;
                if (gap > 0 && gap <= gapThreshold)
                    gaps.push_back({static_cast<int>(i), static_cast<int>(j), gap, vOverlap});
            } else if (hOverlap > 0) {
                const int64_t gap = a.top < b.top ? b.top - a.bottom : a.top - b.bottom;
                if (gap > 0 && gap <= gapThreshold)
                    gaps.push_back({static_cast<int>(i), static_cast<int>(j), gap, hOverlap});
            }
        }
    }
    return gaps;
}

} // namespace cw
