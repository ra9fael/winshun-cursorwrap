// data_window.h — the live data window: what the engine just decided, why,
// and how the arrival speeds are distributed.
//
// The one diagnostics surface. It absorbed the overlay's "edge arrivals"
// panel (which duplicated the histogram with a second, differently-scaled
// implementation): the engine clears the trace when the impact settings
// change, so everything here always describes the CURRENT gate.
//
// Design notes:
//   * One persistent window that repaints itself, not a layer per frame: a
//     GDI window costs a few hundred KB and no per-frame allocation.
//   * It is topmost and never takes focus (WS_EX_NOACTIVATE), so it does not
//     steal the caret from the app the user is tuning against. Closing it
//     hides it; the setting stays on until the user turns it off.
//   * It polls the engine on a timer instead of being pushed to: the engine
//     is lock-protected and cheap to snapshot, and the hook path stays free.
#pragma once

#include "wrap_logic.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "arrival_hist.h"

namespace cw {

// What the window shows; filled by the plugin from the engine and the hook.
struct DataSnapshot {
    bool enabled = true;
    bool hookOk = false;
    bool hookResponsive = true;
    long long hookReinstalls = 0;
    long long setCursorFailures = 0;
    int monitors = 0;
    int outerEdges = 0;
    Settings settings;
    Stats stats;
    EdgeVerdict verdict;
    bool hasPointer = false;
    Sample pointer;
    std::vector<Arrival> arrivals;
    std::vector<Decision> decisions;
    std::vector<MonitorBox> boxes;
};

class DataWindow {
public:
    DataWindow() = default;
    ~DataWindow() { stop(); }
    DataWindow(const DataWindow&) = delete;
    DataWindow& operator=(const DataWindow&) = delete;

    using Provider = std::function<DataSnapshot()>;

    // Show the window; `provider` is called on the window's own timer.
    void start(Provider provider);
    void stop();
    bool active() const { return m_running.load(); }

    // The user closed the window by hand (the X, or Alt+F4). The plugin polls
    // this from its own thread and clears the setting, so a closed window does
    // not resurrect itself on the next settings change. Reading it clears it.
    bool takeCloseRequest() { return m_closeRequested.exchange(false); }

    struct Colors {
        COLORREF accent = RGB(0x22, 0xa7, 0xc4);
        COLORREF text = RGB(0xf2, 0xf2, 0xf5);
        COLORREF background = RGB(0x1d, 0x1d, 0x20);
        bool dark = true;
    };
    void setColors(const Colors& colors);

    static constexpr int kWidth = 480;
    static constexpr int kHeight = 620;

private:
    void run();
    void paint();
    void paintHistogram(HDC dc, int x, int y, int w, int h, const DataSnapshot& snap,
                        const Colors& colors);

    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    std::thread m_thread;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_closeRequested { false };
    DWORD m_threadId = 0;
    HWND m_hwnd = nullptr;
    Provider m_provider;
    HFONT m_font = nullptr;
    HFONT m_bold = nullptr;
    HFONT m_mono = nullptr;

    std::mutex m_colorsMutex;
    Colors m_colors;
};

} // namespace cw
