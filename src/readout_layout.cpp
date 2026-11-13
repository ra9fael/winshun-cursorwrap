// readout_layout.cpp — see readout_layout.h.
#include "readout_layout.h"

#include <cstdio>

namespace cw {

std::string accelLineText(const EdgeVerdict& v)
{
    char buf[96];
    if (v.accelMeasured)
        std::snprintf(buf, sizeof buf, "accel %+lld / %lld px/s²",
                      static_cast<long long>(v.measuredAccelPps2),
                      static_cast<long long>(v.thresholdAccelPps2));
    else
        std::snprintf(buf, sizeof buf, "accel n/a / %lld px/s²",
                      static_cast<long long>(v.thresholdAccelPps2));
    return buf;
}

std::string readoutCaption(const EdgeVerdict& v)
{
    if (v.gateOff)
        return "edge · impact off";
    // A suppressed sample says SO, and nothing else: it can never also claim a
    // wrap, because the verdict is rebuilt per sample (see wrap_logic.cpp).
    if (v.guardBlocked)
        return "guard · blocked";
    if (v.accelBlocked)
        return "not accelerating";
    return "approach speed";
}

std::string readoutPrimary(const EdgeVerdict& v)
{
    char buf[96];
    if (v.gateOff || v.guardBlocked || v.thresholdPps <= 0)
        return v.wrapped ? "wrap" : "—";
    std::snprintf(buf, sizeof buf, "%lld / %lld px/s",
                  static_cast<long long>(v.measuredPps),
                  static_cast<long long>(v.thresholdPps));
    return buf;
}

} // namespace cw
