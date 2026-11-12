// overlay.h — the per-monitor tuning overlay (settings visualization).
//
// When the user turns the tuner on (search bar: `tune` / `调参`), the plugin
// draws, over every monitor's outer edges:
//   * the impact measuring band as a translucent strip (impactBandPx wide),
//   * the anti-oscillation guard circle after each wrap (wrapGuardPx),
//   * a live readout at the edge: measured approach speed vs the gate, and
//   * the edge state in colour (grey: off-edge, amber: gate blocked,
//     green: wrapped).
//
// The overlay is a layered, click-through, non-activating tool window per
// monitor, repainted on a 30 ms timer from an atomic verdict snapshot fed
// by the hook thread. Nothing here blocks the hook.
#pragma once

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

    // Show the overlay over these monitors. Safe from any thread.
    void start(const std::vector<MonitorBox>& monitors);
    void stop();
    bool active() const { return m_running.load(); }

    // The engine to poll for the latest verdict (and current settings).
    void setEngine(WrapEngine* engine) { m_engine = engine; }

    // Theme colours (#AARRGGBB) from the host's appearance notification.
    struct Colors {
        COLORREF accent = RGB(0x0e, 0x74, 0x90);
        COLORREF text = RGB(0xf2, 0xf2, 0xf5);
        COLORREF background = RGB(0x1d, 0x1d, 0x20);
        bool dark = true;
    };
    void setColors(const Colors& colors);

private:
    void run();
    void paintWindow(HWND hwnd, const MonitorBox& box);
    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    std::vector<MonitorBox> m_monitors;
    std::vector<HWND> m_windows;
    std::thread m_thread;
    std::atomic<bool> m_running { false };
    DWORD m_threadId = 0;
    WrapEngine* m_engine = nullptr;

    std::mutex m_colorsMutex;
    Colors m_colors;
};

} // namespace cw
