// overlay.h — the edge tuning overlay (settings visualization).
//
// When the user turns the tuner on (search bar: `tune` / `调参`), the plugin
// draws — over every OUTER edge only, never over the middle of a monitor:
//   * a thin translucent strip (impactBandPx wide) along the measuring band,
//   * a crisp 2px accent line exactly where a wrap can happen,
//   * nothing on inner edges (two monitors touching): no wrap happens there,
//   * a small floating panel near the last edge hit with the measured approach
//     speed against the gate, hidden again two seconds later.
//
// The histogram and the suggested gate live in the data window (they were
// duplicated here as "edge arrivals" with a second, differently-scaled
// implementation; one histogram now, one place to be right). What stays on the
// edges is what the eye needs WHILE the hand is at the edge: bands, and the
// instantaneous readout.
//
// Design notes (both learned the hard way):
//   * A whole-monitor layered window with colour-key transparency is wrong.
//     A colour key is all-transparent or all-opaque per pixel, so a
//     translucent band is impossible and the window becomes a solid sheet
//     over the desktop. The middle of a monitor must stay untouched anyway.
//   * Per-pixel alpha needs UpdateLayeredWindow, which needs a DIB. Building a
//     monitor-sized (33 MB at 4K) DIB per frame blew the plugin's 64 MB job
//     cap. Every strip here owns ONE small DIB (≈1.5 MB) painted once; only
//     the 196x60 readout repaints, and only when its numbers change.
#pragma once

#include "impact_flash.h"
#include "wrap_logic.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

namespace cw {

class Overlay {
public:
    Overlay() = default;
    ~Overlay() { stop(); }
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // Show the overlay over this monitor layout. Safe from any thread.
    void start(const std::vector<MonitorBox>& monitors);
    void stop();
    bool active() const { return m_running.load(); }

    // The engine to poll for the latest verdict (and current settings).
    void setEngine(WrapEngine* engine) { m_engine = engine; }

    // Theme colours from the host's appearance notification.
    struct Colors {
        COLORREF accent = RGB(0x22, 0xa7, 0xc4);
        COLORREF text = RGB(0xf2, 0xf2, 0xf5);
        COLORREF background = RGB(0x1d, 0x1d, 0x20);
        bool dark = true;
    };
    void setColors(const Colors& colors);

    // Whether the impact glow is on screen right now. Exists for the devshell
    // scenario DSL: the glow is decoration, and its trigger rule is the only
    // part of it worth asserting.
    bool impactFlashVisible() const;
    // Whether the edge readout is on screen right now (the scenario DSL
    // asserts refused arrivals still show their measured numbers).
    bool readoutVisible() const { return m_readout.shown; }
    // TEMPORARY diagnostics for the readout-not-showing investigation.
    long long debugRepaints() const { return m_debugRepaints.load(); }
    long long debugBuilds() const { return m_debugBuilds.load(); }
    long long debugHides() const { return m_debugHides.load(); }
    bool debugLastFresh() const { return m_debugLastFresh.load(); }
    bool debugLastTuning() const { return m_debugLastTuning.load(); }
    bool debugRunEntered() const { return m_debugRunEntered.load(); }
    bool debugAnchorReady() const { return m_debugAnchorReady.load(); }
    bool debugStripsBuilt() const { return m_debugStripsBuilt.load(); }
    bool debugLoopExited() const { return m_debugLoopExited.load(); }
    long long debugLoopResult() const { return m_debugLoopResult.load(); }
    long long debugMessages() const { return m_debugMessages.load(); }
    long long debugTimers() const { return m_debugTimers.load(); }
    bool debugSetTimerOk() const { return m_debugSetTimerOk.load(); }
    long long debugSetTimerError() const { return m_debugSetTimerError.load(); }
    long long debugLastMsg() const { return m_debugLastMsg.load(); }

private:
    // One layered surface with its own top-down 32bpp DIB.
    struct Layer {
        HWND hwnd = nullptr;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        HDC dc = nullptr; // memory DC holding the DIB
        HDC screenDc = nullptr;
        HBITMAP dib = nullptr;
        HGDIOBJ oldBitmap = nullptr;
        uint32_t* bits = nullptr; // 0xAARRGGBB while painting
        bool shown = false;
    };

    struct Strip {
        Layer layer;
        int monitorIndex = 0;
        EdgeType edge = EdgeType::Left;
    };

    void run();
    bool createLayer(Layer& layer, int x, int y, int w, int h);
    void destroyLayer(Layer& layer);
    void present(Layer& layer);
    // Store a coverage value in the top byte of every pixel in a rectangle.
    void setAlpha(const Layer& layer, int x0, int y0, int x1, int y1, uint8_t alpha);
    void rebuildStrips(const Settings& settings, const Colors& colors);
    void buildReadout(const EdgeVerdict& verdict, const Settings& settings,
                      const Colors& colors);
    // The impact-wrap glow: one bar along the edge the pointer came through,
    // fading out on ImpactFlash's clock. Painting it is best-effort — it is
    // decoration, so a failure here must never affect anything else. Fed by the
    // engine's LATCHED wrap event (WrapEvent), not the per-sample verdict: the
    // verdict lives for one sample and a 60 ms poll missed it every time.
    void paintImpactFlash(const Settings& settings, const Colors& colors,
                          int monitorIndex);
    void hideImpactFlash();
    // The length of one edge of one monitor, so the glow can be sized as a
    // fraction of it.
    int edgeLengthOf(int monitorIndex, EdgeType edge) const;
    void hideReadout();
    void repaint(const Settings& settings, const Colors& colors);

    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    std::vector<MonitorBox> m_monitors;
    std::vector<MonitorEdge> m_edges;
    std::vector<Strip> m_strips;
    Layer m_anchor; // a 1x1 offscreen window that only owns the repaint timer
    Layer m_readout;
    Layer m_flash;
    ImpactFlash m_flashState;
    long long m_drawnFlashSeq = 0; // the wrap event the glow was painted for
    std::thread m_thread;
    std::atomic<bool> m_running { false };
    DWORD m_threadId = 0;
    WrapEngine* m_engine = nullptr;
    int m_bandPx = 0; // the band width the strips were built for
    bool m_tuningOverlayBuilt = false;
    long long m_drawnVerdictUs = -1;
    long long m_drawnPps = -1;
    long long m_drawnAccel = -1; // part of the readout's change key (with the above)
    bool m_readoutShown = false;

    std::mutex m_colorsMutex;
    Colors m_colors;
    // TEMPORARY readout diagnostics.
    std::atomic<long long> m_debugRepaints { 0 };
    std::atomic<long long> m_debugBuilds { 0 };
    std::atomic<long long> m_debugHides { 0 };
    std::atomic<bool> m_debugLastFresh { false };
    std::atomic<bool> m_debugLastTuning { false };
    std::atomic<bool> m_debugRunEntered { false };
    std::atomic<bool> m_debugAnchorReady { false };
    std::atomic<bool> m_debugStripsBuilt { false };
    std::atomic<bool> m_debugLoopExited { false };
    std::atomic<long long> m_debugLoopResult { 0 };
    std::atomic<long long> m_debugMessages { 0 };
    std::atomic<long long> m_debugTimers { 0 };
    std::atomic<bool> m_debugSetTimerOk { false };
    std::atomic<long long> m_debugSetTimerError { 0 };
    std::atomic<long long> m_debugLastMsg { 0 };
};

} // namespace cw
