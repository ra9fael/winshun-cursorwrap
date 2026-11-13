// readout_layout.h — the tuning readout's geometry and its text, as pure
// functions.
//
// They are pure for one reason: the two defects these fix were both invisible to
// every test the project had.
//
//   * The window was a hard-coded 60 px while the acceleration line was drawn
//     from y=46 with a 13 px font, so the glyphs ran to y≈63 — past the window,
//     with the bottom border drawn through them. A constant cannot be checked;
//     a derived height that each row has to fit inside can be.
//   * The readout shows `wrapped` and `guardBlocked`, and a guard-suppressed
//     sample used to inherit the previous sample's `wrapped`, so it displayed
//     "guard · blocked" and "wrap" together. A mutually-exclusive invariant on
//     the verdict is checkable; a drawing is not.
#ifndef CW_READOUT_LAYOUT_H
#define CW_READOUT_LAYOUT_H

#include "wrap_logic.h"

#include <string>

namespace cw {

// Row metrics. Both the window size and the drawing positions come from these,
// so they cannot drift apart.
struct ReadoutMetrics {
    static constexpr int width = 196;
    static constexpr int padTop = 7;
    static constexpr int padBottom = 6;
    static constexpr int padX = 10;
    static constexpr int captionH = 14; // the small caption line
    static constexpr int bigH = 26;     // the primary value
    static constexpr int accelH = 16;   // the acceleration line (small)
    static constexpr int borderH = 2;   // the border occupies the last 2 px
};

// Whether this verdict will paint the acceleration line at all.
inline bool showsAccelLine(const EdgeVerdict& v)
{
    return v.thresholdAccelPps2 > 0 && !v.gateOff;
}

// The one-word reason an unmeasurable slope is unmeasurable (caption-sized).
const char* accelIssueShort(AccelIssue issue);

// The window height this verdict needs. The bottom of the last row, plus the
// border, plus the bottom padding.
inline int readoutHeight(const EdgeVerdict& v)
{
    int bottom = ReadoutMetrics::padTop + ReadoutMetrics::captionH
        + ReadoutMetrics::bigH;
    if (showsAccelLine(v))
        bottom += ReadoutMetrics::accelH;
    return bottom + ReadoutMetrics::padBottom + ReadoutMetrics::borderH;
}

// The y of the primary value and of the acceleration line, so a test can assert
// the last row's ink fits inside the window.
inline int readoutBigY() { return ReadoutMetrics::padTop + ReadoutMetrics::captionH; }
inline int readoutAccelY(const EdgeVerdict& v) { return readoutBigY() + ReadoutMetrics::bigH; }

// The acceleration line's text. "n/a" means the slope could not be measured —
// which an em dash communicated so badly that the user read the line as "the
// acceleration IS 1600".
std::string accelLineText(const EdgeVerdict& v);

// The caption and the primary value, as UTF-8 for tests. The overlay draws
// UTF-16; these exist so the wording is testable.
std::string readoutCaption(const EdgeVerdict& v);
std::string readoutPrimary(const EdgeVerdict& v);

} // namespace cw

#endif // CW_READOUT_LAYOUT_H
