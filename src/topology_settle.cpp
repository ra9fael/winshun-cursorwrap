// topology_settle.cpp — see topology_settle.h.
#include "topology_settle.h"

namespace cw {

bool sameMonitorLayout(const std::vector<MonitorBox>& a, const std::vector<MonitorBox>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const MonitorBox& x = a[i];
        const MonitorBox& y = b[i];
        if (x.left != y.left || x.top != y.top || x.right != y.right
            || x.bottom != y.bottom || x.primary != y.primary
            || x.workLeft != y.workLeft || x.workTop != y.workTop
            || x.workRight != y.workRight || x.workBottom != y.workBottom
            || x.hasWorkArea != y.hasWorkArea)
            return false;
    }
    return true;
}

bool TopologySettle::settled(const std::vector<MonitorBox>& candidate)
{
    ++m_samples;
    if (candidate.empty()) {
        m_previous.clear();
        m_hasPrevious = false;
        return false;
    }
    const bool settled = m_hasPrevious && sameMonitorLayout(candidate, m_previous);
    m_previous = candidate;
    m_hasPrevious = true;
    return settled;
}

void TopologySettle::reset()
{
    m_previous.clear();
    m_hasPrevious = false;
    m_samples = 0;
}

} // namespace cw
