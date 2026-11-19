// topology_settle.h — when is a monitor enumeration safe to commit.
//
// Display and session transitions deliver their layout in bursts: an RDP
// disconnect, an unlock, or a resolution change each re-arrange the desktop
// for a moment before Windows settles, and EnumDisplayMonitors can report a
// transitional layout (or nothing at all) inside that window. Committing the
// first reading put a half-built desktop into the wrap engine — the user's
// "after I disconnect remote desktop and unlock, wrapping stops at an inner
// rectangle until I restart the plugin". A layout only commits once two
// consecutive enumerations agree.
#pragma once

#include "wrap_topology.h"

#include <cstddef>
#include <vector>

namespace cw {

// Two layouts are the same display set: same count, same rects, same work
// areas. Pure; unit-tested.
bool sameMonitorLayout(const std::vector<MonitorBox>& a, const std::vector<MonitorBox>& b);

class TopologySettle {
public:
    // Feed one enumeration. True means it equals the previous one and may be
    // committed; false means keep waiting (the caller decides when to stop).
    // An empty enumeration never settles — there is nothing to commit, and
    // the engine keeps the last good topology on its own.
    bool settled(const std::vector<MonitorBox>& candidate);

    // How many samples this window has seen since the last reset.
    int samples() const { return m_samples; }

    void reset();

private:
    std::vector<MonitorBox> m_previous;
    bool m_hasPrevious = false;
    int m_samples = 0;
};

} // namespace cw
