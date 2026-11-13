// readout_layout.cpp — see readout_layout.h.
#include "readout_layout.h"

#include <cstdio>

namespace cw {

// The one-word reason for the readout's caption line, which has ~180 px. The
// full name ("window-too-short") lives in accelIssueName and the data window;
// here it collapses to the word that answers "what do I change".
const char* accelIssueShort(AccelIssue issue)
{
    switch (issue) {
    case AccelIssue::FewSamples:
    case AccelIssue::HalfSamples:
        return "sparse";
    case AccelIssue::WindowTooShort:
    case AccelIssue::HalfTime:
    case AccelIssue::HalfTravel:
        return "short";
    case AccelIssue::Reversal:
        return "reversed";
    default:
        return "n/a";
    }
}

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
    // "not accelerating" was only half the story: when the slope could not be
    // measured at all the refusal is fail-closed, and the user must see WHY —
    // a bare "not accelerating" read as "the hand really was steady".
    if (v.accelBlocked && !v.accelMeasured)
        return std::string("accel n/a (") + accelIssueShort(v.accelIssue) + ")";
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
