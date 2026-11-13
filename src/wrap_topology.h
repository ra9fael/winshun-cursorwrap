// wrap_topology.h — the pure display-edge topology and wrap-destination engine.
//
// Ported from PowerToys CursorWrap (MIT, Microsoft Corporation):
//   src/modules/MouseUtils/CursorWrap/MonitorTopology.{h,cpp}
// The algorithm is edge-based: every monitor contributes four edges; edges
// with an adjacent monitor within a tolerance are "inner" and never wrap;
// only outer edges wrap to the nearest opposite outer edge.
//
// This header must not include OS headers: monitors are passed in as plain
// structs, so the whole engine unit-tests on any platform. The exe-side
// (src/main.cpp) converts EnumDisplayMonitors output into MonitorBox values.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cw {

struct MonitorBox {
    int64_t left = 0;
    int64_t top = 0;
    int64_t right = 0;
    int64_t bottom = 0;
    bool primary = false;
    int64_t dpi = 96;
    std::string device;
    // The work area: the monitor rect minus the taskbar (and any other appbar).
    // Defaults to the full rect so a caller that does not know them behaves
    // exactly as before. Wrapping must land INSIDE this area — the user does
    // not want the cursor to reappear on the taskbar.
    int64_t workLeft = 0;
    int64_t workTop = 0;
    int64_t workRight = 0;
    int64_t workBottom = 0;
    bool hasWorkArea = false;

    int64_t width() const { return right - left; }
    int64_t height() const { return bottom - top; }
    bool contains(int64_t x, int64_t y) const
    {
        return x >= left && x < right && y >= top && y < bottom;
    }

    // The work-area rectangle, falling back to the full rect.
    int64_t areaLeft() const { return hasWorkArea ? workLeft : left; }
    int64_t areaTop() const { return hasWorkArea ? workTop : top; }
    int64_t areaRight() const { return hasWorkArea ? workRight : right; }
    int64_t areaBottom() const { return hasWorkArea ? workBottom : bottom; }
};

enum class EdgeType : int { Left = 0, Right = 1, Top = 2, Bottom = 3 };

enum class WrapMode : int { Both = 0, VerticalOnly = 1, HorizontalOnly = 2 };

// Cursor movement between two samples.
struct CursorDirection {
    int64_t dx = 0; // positive = right
    int64_t dy = 0; // positive = down

    bool movingLeft() const { return dx < 0; }
    bool movingRight() const { return dx > 0; }
    bool movingUp() const { return dy < 0; }
    bool movingDown() const { return dy > 0; }
    // Horizontal movement dominates (used to break corner ties).
    bool primarilyHorizontal() const
    {
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        return ax >= ay;
    }
};

// One edge of one monitor, in topology-index space.
struct MonitorEdge {
    int monitorIndex = -1; // index into Topology::monitors (stable within one snapshot)
    EdgeType type = EdgeType::Left;
    int64_t start = 0; // vertical edge: y range [start, end); horizontal: x range
    int64_t end = 0;
    int64_t position = 0; // vertical edge: x coordinate; horizontal edge: y coordinate
    bool outer = false;
};

// Result of finding the opposite edge for a cursor coordinate.
struct OppositeEdge {
    bool found = false;
    bool projection = false; // true: cursor is outside the target edge's range
    MonitorEdge edge;
    int64_t projectedCoordinate = 0;
};

class Topology {
public:
    // Rebuild from a monitor snapshot. Monitors are sorted left/top first so
    // that indices are deterministic across rebuilds. `adjacencyTolerance`
    // decides when two edges count as adjacent (inner edges never wrap).
    void initialize(const std::vector<MonitorBox>& monitors, int64_t adjacencyTolerance = 50);

    const std::vector<MonitorBox>& monitors() const { return m_monitors; }
    const std::vector<MonitorEdge>& outerEdges() const { return m_outerEdges; }
    size_t monitorCount() const { return m_monitors.size(); }

    // Index of the monitor containing (x, y), or -1 when none does.
    int monitorAt(int64_t x, int64_t y) const;

    // Index of the monitor a POINTER should be considered to be on: the one
    // containing it, else the nearest (never -1 unless there are no
    // monitors). The wrap path uses this so a sample outside the union of the
    // rects cannot disable wrapping.
    int nearestMonitor(int64_t x, int64_t y) const;

    bool monitorRect(int index, MonitorBox& out) const;

    // Is the point on an outer edge of monitor `index`, within
    // `edgeThreshold` pixels of it? At corners several edges can match;
    // `direction` breaks the tie.
    bool onOuterEdge(int index, int64_t x, int64_t y, EdgeType& outEdge,
                     WrapMode mode, const CursorDirection* direction,
                     int64_t edgeThreshold = 1) const;

    // Wrap destination for a point on the given edge. Falls back to the same
    // monitor's opposite edge when no other monitor provides one.
    // Returns the destination point; equals the input when no wrap applies.
    std::pair<int64_t, int64_t> wrapDestination(int index, int64_t x, int64_t y,
                                                EdgeType edge, WrapMode mode) const;
    // Pull a point into the monitor's work area (the rect minus the taskbar).
    // A no-op when the caller never supplied a work area.
    std::pair<int64_t, int64_t> clampToWorkArea(int index, int64_t x, int64_t y) const;

    // Diagnostics: pairs of monitors whose rects should touch but do not
    // (layout gaps break wrapping in unintuitive ways).
    struct Gap {
        int a = -1;
        int b = -1;
        int64_t horizontalGap = 0;
        int64_t verticalOverlap = 0;
    };
    std::vector<Gap> detectGaps(int64_t gapThreshold) const;

private:
    std::vector<MonitorBox> m_monitors;
    std::vector<MonitorEdge> m_outerEdges;
    std::map<std::pair<int, EdgeType>, MonitorEdge> m_edges;

    const MonitorEdge* edgeOf(int monitorIndex, EdgeType type) const;
    void buildEdges();
    void identifyOuter(int64_t tolerance);
    bool edgesAdjacent(const MonitorEdge& a, const MonitorEdge& b, int64_t tolerance) const;
    EdgeType prioritize(const std::vector<EdgeType>& candidates,
                        const CursorDirection* direction) const;
    OppositeEdge findOpposite(EdgeType fromEdge, int64_t coordinate,
                              const MonitorEdge& sourceEdge) const;
    MonitorEdge findOppositeOverlapping(EdgeType fromEdge, int64_t coordinate) const;
    static int64_t projectCoordinate(int64_t cursorCoordinate, const MonitorEdge& source,
                                     const MonitorEdge& target);
};

} // namespace cw
