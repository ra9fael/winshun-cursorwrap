// test_wrap_topology.cpp — unit tests for the edge topology engine.
// Layout scenarios ported from PowerToys CursorWrap CursorWrapTests.h
// (3x3 grid configurations, MIT, Microsoft Corporation).
#include "test_harness.h"
#include "wrap_topology.h"
#include "wrap_logic.h"

#include "winshun.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace cw;

namespace {

MonitorBox box(int64_t l, int64_t t, int64_t r, int64_t b, bool primary = false)
{
    MonitorBox m;
    m.left = l;
    m.top = t;
    m.right = r;
    m.bottom = b;
    m.primary = primary;
    m.dpi = 96;
    return m;
}

MonitorBox boxDpi(int64_t l, int64_t t, int64_t r, int64_t b, int64_t dpi, bool primary = false)
{
    MonitorBox m = box(l, t, r, b, primary);
    m.dpi = dpi;
    return m;
}

} // namespace

// ---------------------------------------------------------------- single

CW_TEST(topology_single_monitor_all_edges_outer)
{
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true) });
    CW_CHECK_EQ(t.outerEdges().size(), size_t(4));
}

CW_TEST(topology_single_monitor_wraps_within_itself)
{
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true) });
    // The destination is the opposite edge's position (right-1 / bottom-1);
    // the 50px guard in WrapEngine is what stops the bounce-back.
    const auto [x, y] = t.wrapDestination(0, 0, 540, EdgeType::Left, WrapMode::Both);
    CW_CHECK_EQ(x, 1918);
    CW_CHECK_EQ(y, 540);
    const auto [x2, y2] = t.wrapDestination(0, 1919, 540, EdgeType::Right, WrapMode::Both);
    CW_CHECK_EQ(x2, 1);
    const auto [x3, y3] = t.wrapDestination(0, 960, 0, EdgeType::Top, WrapMode::Both);
    CW_CHECK_EQ(y3, 1078);
    const auto [x4, y4] = t.wrapDestination(0, 960, 1079, EdgeType::Bottom, WrapMode::Both);
    CW_CHECK_EQ(y4, 1);
}

CW_TEST(topology_landing_respects_work_area)
{
    // The taskbar occupies the bottom 48 rows: the work area ends at 1032.
    // A wrap must never DELIVER the pointer into the taskbar band, and the
    // work area is per monitor — monitor 1's own must be respected.
    MonitorBox a = box(0, 0, 1920, 1080, true);
    a.workLeft = 0;
    a.workTop = 0;
    a.workRight = 1920;
    a.workBottom = 1032;
    a.hasWorkArea = true;
    Topology t;
    t.initialize({ a });
    // Bottom -> top edge: 1. Top -> bottom edge: the last WORK row (1031),
    // not the monitor's last row (1079), which is the taskbar.
    const auto [x, y] = t.wrapDestination(0, 960, 1079, EdgeType::Bottom, WrapMode::Both);
    CW_CHECK_EQ(y, 1);
    const auto [x2, y2] = t.wrapDestination(0, 960, 0, EdgeType::Top, WrapMode::Both);
    CW_CHECK_EQ(y2, 1031);
    // Horizontal landings are unaffected by a bottom taskbar.
    const auto [x3, y3] = t.wrapDestination(0, 0, 540, EdgeType::Left, WrapMode::Both);
    CW_CHECK_EQ(x3, 1918);
    CW_CHECK_EQ(y3, 540);
}

// -------------------------------------------------- grid scenarios (PowerToys)

CW_TEST(grid_dual_horizontal)
{
    // Monitor A left (primary), monitor B right, touching.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) });
    CW_CHECK_EQ(t.outerEdges().size(), size_t(6)); // 8 edges - 2 inner
    EdgeType edge;
    CursorDirection right { 5, 0 };
    CW_CHECK(t.onOuterEdge(0, 1919, 540, edge, WrapMode::Both, &right) == false);
    CW_CHECK(t.onOuterEdge(1, 1920, 540, edge, WrapMode::Both, &right) == false);
    // Monitor 1's right edge is outer; the wrap target is the outer Left
    // edge with the smallest position — monitor 0's left edge (pos 0).
    const auto [x, y] = t.wrapDestination(1, 1919, 540, EdgeType::Right, WrapMode::Both);
    CW_CHECK_EQ(x, 1);
    CW_CHECK_EQ(y, 540);
    // Monitor 0's left edge wraps to the outer Right edge with the largest
    // position — monitor 1's right edge (pos 3839).
    const auto [x2, y2] = t.wrapDestination(0, 0, 540, EdgeType::Left, WrapMode::Both);
    CW_CHECK_EQ(x2, 3838);
    // A's left edge is outer; B's right edge is outer.
    CW_CHECK(t.onOuterEdge(1, 3839, 540, edge, WrapMode::Both, &right));
    CW_CHECK(edge == EdgeType::Right);
}

CW_TEST(grid_dual_vertical)
{
    // Monitor B on top, monitor A (primary) below.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080), box(0, 1080, 1920, 2160, true) });
    EdgeType edge;
    CursorDirection down { 0, 5 };
    CursorDirection up { 0, -5 };
    // The touching bottom of the top monitor and top of the bottom monitor
    // are inner: no wrap on either.
    CW_CHECK(!t.onOuterEdge(0, 960, 1079, edge, WrapMode::Both, &up));
    CW_CHECK(!t.onOuterEdge(1, 960, 1080, edge, WrapMode::Both, &down));
    // Top monitor's top edge wraps to the outer Bottom edge with the largest
    // position — the bottom monitor's bottom edge (pos 2159).
    const auto [x, y] = t.wrapDestination(0, 960, 0, EdgeType::Top, WrapMode::Both);
    CW_CHECK_EQ(y, 2158);
    // Bottom monitor's bottom edge wraps to the outer Top edge with the
    // smallest position — the top monitor's top edge (pos 0).
    const auto [x2, y2] = t.wrapDestination(1, 960, 2159, EdgeType::Bottom, WrapMode::Both);
    CW_CHECK_EQ(y2, 1);
}

CW_TEST(grid_triple_l_shape)
{
    // L-shape: top monitor over the right column, main left-bottom, right-bottom.
    // top: (0..1920, -1440..0); left-bottom (primary): (0..1920, 0..2560);
    // right-bottom: (1920..3840, 0..2560).
    Topology t;
    t.initialize({ box(0, 0, 1920, 2560, true), box(1920, 0, 3840, 2560),
                  box(0, -1440, 1920, 0) });
    // 12 edges; inner: left|right between bottom pair (2) and top|bottom
    // between left-bottom and top (2) => 8 outer.
    CW_CHECK_EQ(t.outerEdges().size(), size_t(8));
    // Left-bottom's top edge is inner (touching the top monitor).
    EdgeType edge;
    CursorDirection up { 0, -5 };
    CW_CHECK(!t.onOuterEdge(0, 100, 0, edge, WrapMode::Both, &up));
    // Right-bottom's top edge is outer (nothing above it); the wrap target
    // is the outer Bottom edge with the largest position (2559).
    const auto [x, y] = t.wrapDestination(1, 2000, 0, EdgeType::Top, WrapMode::Both);
    CW_CHECK_EQ(y, 2558);
    CW_CHECK_EQ(x, 2000);
}

CW_TEST(grid_non_overlapping_vertical_projection)
{
    // Diagonally offset pair: the bottom display's bottom edge at x=2000 has
    // no top edge overlapping that coordinate (the top display's top edge
    // ends at 1920). The wrap projects x into the target edge's span.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true), box(960, 1080, 2880, 2160) });
    const auto [x1, y1] = t.wrapDestination(1, 2000, 2159, EdgeType::Bottom, WrapMode::Both);
    CW_CHECK_EQ(x1, 1919); // clamped into the top edge's span [0..1920)
    CW_CHECK_EQ(y1, 1);    // the top edge's position
    // Inside the overlap: x preserved.
    const auto [x2, y2] = t.wrapDestination(1, 1500, 2159, EdgeType::Bottom, WrapMode::Both);
    CW_CHECK_EQ(x2, 1500);
    CW_CHECK_EQ(y2, 1);
    // Touching edges between the two displays are inner.
    EdgeType edge;
    CursorDirection down { 0, 5 };
    CW_CHECK(!t.onOuterEdge(0, 1500, 1079, edge, WrapMode::Both, &down));
    CW_CHECK(!t.onOuterEdge(1, 1500, 1080, edge, WrapMode::Both, &down));
}

CW_TEST(grid_wrap_mode_filters)
{
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true) });
    EdgeType edge;
    CursorDirection left { -5, 0 };
    CursorDirection up { 0, -5 };
    // HorizontalOnly ignores top/bottom edges.
    CW_CHECK(!t.onOuterEdge(0, 960, 0, edge, WrapMode::HorizontalOnly, &up));
    CW_CHECK(t.onOuterEdge(0, 0, 540, edge, WrapMode::HorizontalOnly, &left));
    // VerticalOnly ignores left/right edges.
    CW_CHECK(!t.onOuterEdge(0, 0, 540, edge, WrapMode::VerticalOnly, &left));
    CW_CHECK(t.onOuterEdge(0, 960, 0, edge, WrapMode::VerticalOnly, &up));
    CW_CHECK(edge == EdgeType::Top);
}

CW_TEST(grid_corner_tie_break_by_direction)
{
    // Single monitor: the top-left corner matches both Left and Top; the
    // movement direction decides which edge wraps.
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true) });
    EdgeType edge;
    CursorDirection leftward { -8, 3 };
    CW_CHECK(t.onOuterEdge(0, 0, 0, edge, WrapMode::Both, &leftward));
    CW_CHECK(edge == EdgeType::Left);
    CursorDirection upward { 3, -8 };
    CW_CHECK(t.onOuterEdge(0, 0, 0, edge, WrapMode::Both, &upward));
    CW_CHECK(edge == EdgeType::Top);
}

CW_TEST(topology_adjacency_tolerance)
{
    // Two monitors with a 30px horizontal gap: adjacent within the default
    // 50px tolerance, outer when the tolerance is tightened to 10px.
    const std::vector<MonitorBox> layout {
        box(0, 0, 1920, 1080, true),
        box(1950, 0, 3870, 1080),
    };
    Topology loose;
    loose.initialize(layout, 50);
    CW_CHECK_EQ(loose.outerEdges().size(), size_t(6));
    Topology strict;
    strict.initialize(layout, 10);
    CW_CHECK_EQ(strict.outerEdges().size(), size_t(8));
}

CW_TEST(topology_gap_detection)
{
    Topology t;
    t.initialize({ box(0, 0, 1920, 1080, true), box(1950, 0, 3870, 1080) });
    const auto gaps = t.detectGaps(50);
    CW_CHECK_EQ(gaps.size(), size_t(1));
    if (gaps.size() == 1) {
        CW_CHECK_EQ(gaps[0].horizontalGap, 30);
        CW_CHECK_EQ(gaps[0].verticalOverlap, 1080);
    }
    Topology clean;
    clean.initialize({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) });
    CW_CHECK(clean.detectGaps(50).empty());
}

CW_TEST(topology_empty_and_degenerate)
{
    Topology t;
    t.initialize({});
    CW_CHECK_EQ(t.monitorCount(), size_t(0));
    EdgeType edge;
    CursorDirection d { 1, 0 };
    CW_CHECK(!t.onOuterEdge(0, 0, 0, edge, WrapMode::Both, &d));
    const auto [x, y] = t.wrapDestination(0, 5, 5, EdgeType::Left, WrapMode::Both);
    CW_CHECK_EQ(x, 5);
    CW_CHECK_EQ(y, 5);
}

CW_TEST(topology_monitor_at_and_sorting)
{
    Topology t;
    t.initialize({ box(1920, 0, 3840, 1080), box(0, 0, 1920, 1080, true) });
    CW_CHECK_EQ(t.monitorAt(100, 100), 0);      // primary first
    CW_CHECK_EQ(t.monitorAt(2000, 100), 1);
    CW_CHECK_EQ(t.monitorAt(5000, 100), -1);    // outside everything
    CW_CHECK_EQ(t.monitorAt(-50, 100), -1);     // negative coordinates
}

CW_TEST(topology_dpi_mixed_layout)
{
    // A 150%-dpi laptop panel next to a 100% external one: geometry is
    // already in physical pixels, dpi only annotates the box.
    Topology t;
    t.initialize({ boxDpi(0, 0, 2560, 1440, 144, true), boxDpi(2560, 0, 4480, 2160, 96) });
    CW_CHECK_EQ(t.outerEdges().size(), size_t(6));
    EdgeType edge;
    CursorDirection right { 5, 0 };
    CW_CHECK(!t.onOuterEdge(0, 2559, 700, edge, WrapMode::Both, &right));
    // Right monitor's right edge is outer; the wrap target is the outer Left
    // edge with the smallest position — the primary's left edge (pos 0).
    const auto [x, y] = t.wrapDestination(1, 4479, 700, EdgeType::Right, WrapMode::Both);
    CW_CHECK_EQ(x, 1);
}

// ---------------------------------------------------- engine state machine

CW_TEST(engine_wraps_and_records_guard)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    // Move to the right edge with the guard inactive: wrap to the opposite
    // (left) edge position.
    const auto [wx, wy] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(wx, 1);
    CW_CHECK_EQ(wy, 540);
    CW_CHECK_EQ(e.stats().wraps, 1);
    // Within the 50px guard: no second wrap even though this is an edge.
    const auto [gx, gy] = e.onMouseMove({ 0, 540 }, false, false);
    CW_CHECK_EQ(gx, 0);
    CW_CHECK_EQ(e.stats().suppressed >= 1, true);
    // Leaving the guard clears it; a wrap can happen again.
    e.onMouseMove({ 300, 540 }, false, false);
    const auto [wx2, wy2] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(wx2, 1);
    CW_CHECK_EQ(e.stats().wraps, 2);
}

CW_TEST(engine_single_monitor_disable)
{
    Settings s;
    s.disableOnSingleMonitor = true;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
}

CW_TEST(engine_drag_suppression)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540 }, false, true);
    CW_CHECK_EQ(x, 1919);
    CW_CHECK_EQ(e.stats().wraps, 0);
    // Button released: wrap happens (to the left edge, x = 0).
    const auto [x2, y2] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x2, 1);
}

CW_TEST(engine_activation_gate)
{
    Settings s;
    s.activation = ActivationMode::WhileCtrl;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x, 1919); // Ctrl not held: no wrap
    const auto [x2, y2] = e.onMouseMove({ 1919, 540 }, true, false);
    CW_CHECK_EQ(x2, 1);   // Ctrl held: wrap to the opposite edge
}

CW_TEST(engine_mode_vertical_only)
{
    Settings s;
    s.wrapMode = WrapMode::VerticalOnly;
    WrapEngine e(s);
    e.rebuild({ box(0, 0, 1920, 1080, true) });
    const auto [x, y] = e.onMouseMove({ 1919, 540 }, false, false);
    CW_CHECK_EQ(x, 1919); // horizontal edge ignored
    const auto [x2, y2] = e.onMouseMove({ 960, 1079 }, false, false);
    CW_CHECK_EQ(y2, 1);   // bottom edge wraps to the top edge position
}

CW_TEST(engine_settings_roundtrip_and_clamps)
{
    RawSettings raw;
    raw.wrapMode = "vertical";
    raw.hasWrapMode = true;
    raw.activationMode = "shift";
    raw.hasActivation = true;
    raw.edgeThresholdPx = 999;      // out of range -> default
    raw.hasEdgeThreshold = true;
    raw.wrapGuardPx = 5;            // below range -> default
    raw.hasWrapGuard = true;
    raw.adjacencyTolerancePx = 120; // valid
    raw.hasAdjacency = true;
    raw.disableDuringDrag = false;
    raw.hasDrag = true;
    Settings s;
    applySettings(raw, s, false);
    CW_CHECK(s.wrapMode == WrapMode::VerticalOnly);
    CW_CHECK(s.activation == ActivationMode::WhileShift);
    CW_CHECK_EQ(s.edgeThresholdPx, 1);
    CW_CHECK_EQ(s.wrapGuardPx, 50);
    CW_CHECK_EQ(s.adjacencyTolerancePx, 120);
    CW_CHECK_EQ(s.disableDuringDrag, false);

    RawSettings bad;
    bad.wrapMode = "diagonal";
    bad.hasWrapMode = true;
    bad.activationMode = "win";
    bad.hasActivation = true;
    applySettings(bad, s, true);
    CW_CHECK(s.wrapMode == WrapMode::VerticalOnly); // unknown value keeps the previous
    CW_CHECK(s.activation == ActivationMode::WhileShift);
}

CW_TEST(engine_diagnostics_shape)
{
    WrapEngine e;
    e.rebuild({ box(0, 0, 1920, 1080, true), box(1920, 0, 3840, 1080) });
    const Diagnostics d = e.diagnostics(true);
    CW_CHECK_EQ(d.monitors, 2);
    CW_CHECK_EQ(d.outerEdges, 6);
    CW_CHECK(d.hookOk);
    CW_CHECK(d.gaps.empty());
}

// ------------------------------------------------------ layout fixtures (JSON)

namespace {

struct FixtureCase {
    int64_t x = 0;
    int64_t y = 0;
    std::string edge;    // left / right / top / bottom
    bool expectWrap = false;
    int expectMonitor = -1; // -1 = same
};

bool runFixtureFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto parsed = winshun::Json::parse(buffer.str());
    if (!parsed || !parsed->isObject())
        return false;
    const winshun::Json& root = *parsed;

    std::vector<MonitorBox> monitors;
    for (const winshun::Json& m : root["monitors"].toArray()) {
        MonitorBox b;
        b.left = m["left"].toInt();
        b.top = m["top"].toInt();
        b.right = m["right"].toInt();
        b.bottom = m["bottom"].toInt();
        b.primary = m["primary"].toBool();
        b.dpi = m["dpi"].toInt(96);
        b.device = m["device"].toString();
        monitors.push_back(b);
    }
    Topology t;
    t.initialize(monitors);

    for (const winshun::Json& c : root["cases"].toArray()) {
        EdgeType edge;
        const std::string es = c["edge"].toString();
        if (es == "left")
            edge = EdgeType::Left;
        else if (es == "right")
            edge = EdgeType::Right;
        else if (es == "top")
            edge = EdgeType::Top;
        else
            edge = EdgeType::Bottom;
        const int64_t x = c["x"].toInt();
        const int64_t y = c["y"].toInt();
        const bool expect = c["wrap"].toBool();
        const int monitor = t.monitorAt(x, y);
        const bool got = monitor >= 0 && t.onOuterEdge(monitor, x, y, edge, WrapMode::Both, nullptr);
        if (got != expect) {
            ++cwtest::failures();
            std::printf("  fixture %s case (%lld,%lld %s): expected wrap=%d got %d\n",
                        path.c_str(), static_cast<long long>(x), static_cast<long long>(y),
                        es.c_str(), expect ? 1 : 0, got ? 1 : 0);
        }
    }
    return true;
}

} // namespace

int runLayoutFixtures(const std::string& dir)
{
    int count = 0;
    for (const char* name :
         { "single.json", "dual-horizontal.json", "dual-vertical.json", "triple-l.json",
           "non-overlap-vertical.json", "gap-50.json" }) {
        const std::string path = dir + "/" + name;
        if (runFixtureFile(path))
            ++count;
    }
    std::printf("[FIXT ] %d layout fixture file(s) from %s\n", count, dir.c_str());
    return count;
}
