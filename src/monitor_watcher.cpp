// monitor_watcher.cpp — see monitor_watcher.h.
#include "monitor_watcher.h"

#include "topology_settle.h"

#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

namespace cw {
namespace {

constexpr UINT WM_APP_REBUILD_NOW = WM_APP + 2;
constexpr UINT DEBOUNCE_MS = 500;
// The desktop settles in bursts: an RDP disconnect or an unlock flips the
// layout, then Windows re-arranges it within a second or two. Two
// consecutive equal enumerations mean the layout is holding still; before
// that, nothing is committed (the engine keeps the last good topology).
constexpr UINT SETTLE_MS = 300;
constexpr int SETTLE_MAX_TRIES = 8; // ~2.4 s of settling before giving up

} // namespace

// Out-of-line: it references the private wndProc through the shim, and a
// free function inside an anonymous namespace cannot see private members.
LRESULT CALLBACK MonitorWatcher::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE && lp) {
        auto* self = static_cast<MonitorWatcher*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    auto* self = reinterpret_cast<MonitorWatcher*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self)
        return self->wndProc(msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

bool MonitorWatcher::start(Enumerate enumerate, Commit commit)
{
    if (m_running.load())
        return true;
    // A previous start() that failed leaves a joinable thread behind; joining it
    // here is the only safe way to reuse this object (see stop()).
    if (m_thread.joinable())
        m_thread.join();
    m_enumerate = std::move(enumerate);
    m_commit = std::move(commit);
    m_settle.reset();
    m_settleTries = 0;
    m_running.store(true);
    m_thread = std::thread([this] { run(); });
    return m_running.load();
}

void MonitorWatcher::stop()
{
    // Join whatever is joinable, never depend on m_running: run() clears it when
    // the window cannot be created, and a joinable std::thread reaches
    // std::terminate in its destructor.
    m_running.store(false);
    if (m_window)
        PostMessage(m_window, WM_CLOSE, 0, 0);
    if (m_thread.joinable())
        m_thread.join();
    m_window = nullptr;
}

void MonitorWatcher::run()
{
    WNDCLASSEX wc {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndProcStatic;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"ra9fael.cursorwrap.watcher";
    RegisterClassEx(&wc);

    // A hidden TOP-LEVEL window, never shown. Broadcasts (WM_DISPLAYCHANGE,
    // WM_SETTINGCHANGE, WM_DEVICECHANGE) reach every top-level window — a
    // message-only one (HWND_MESSAGE) lives under the message window and is
    // skipped, which is exactly why an RDP disconnect used to leave the plugin
    // on the stale topology until it was restarted. It must not be visible, so
    // it is never painted, and the toolwindow styles keep it out of Alt+Tab.
    m_window = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
                              WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);
    if (!m_window) {
        m_running.store(false);
        return;
    }
    // Session transitions (unlock, RDP connect/disconnect, logon) move the
    // display layout as surely as a resolution change does, and an unlock
    // after an RDP disconnect is where the physical layout comes back.
    ::WTSRegisterSessionNotification(m_window, NOTIFY_FOR_THIS_SESSION);

    MSG msg;
    while (m_running.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    ::WTSUnRegisterSessionNotification(m_window);
    if (m_window) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    UnregisterClass(wc.lpszClassName, wc.hInstance);
}

void MonitorWatcher::sampleNow()
{
    const std::vector<MonitorBox> monitors = m_enumerate ? m_enumerate()
                                                         : std::vector<MonitorBox> {};
    if (monitors.empty()) {
        // A transitional moment (a reconnect or a driver blink): the engine
        // keeps the last good layout, and the settle window keeps trying.
        if (++m_settleTries <= SETTLE_MAX_TRIES)
            SetTimer(m_window, m_settleTimer, SETTLE_MS, nullptr);
        return;
    }
    if (m_settle.settled(monitors)) {
        m_settleTries = 0;
        if (m_commit)
            m_commit(monitors);
        return;
    }
    if (++m_settleTries > SETTLE_MAX_TRIES) {
        // The layout kept moving for the whole settle window (a rotating
        // remote session): commit the newest reading rather than hold a
        // topology that is known stale.
        m_settleTries = 0;
        if (m_commit)
            m_commit(monitors);
        return;
    }
    SetTimer(m_window, m_settleTimer, SETTLE_MS, nullptr);
}

LRESULT MonitorWatcher::wndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: // SPI_SETWORKAREA (taskbar) also moves edges
    case WM_DEVICECHANGE:
    case WM_POWERBROADCAST: // resume from sleep: the layout may differ now
    case WM_WTSSESSION_CHANGE: // unlock, RDP connect/disconnect, logon
        if (m_running.load())
            // A burst of notifications is one change: the debounce restarts.
            SetTimer(m_window, m_debounceTimer, DEBOUNCE_MS, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == m_debounceTimer) {
            KillTimer(m_window, m_debounceTimer);
            m_settle.reset();
            m_settleTries = 0;
            sampleNow();
        } else if (wp == m_settleTimer) {
            KillTimer(m_window, m_settleTimer);
            sampleNow();
        }
        return 0;
    case WM_APP_REBUILD_NOW:
        if (m_running.load()) {
            KillTimer(m_window, m_debounceTimer);
            m_settle.reset();
            m_settleTries = 0;
            sampleNow();
        }
        return 0;
    case WM_CLOSE:
        KillTimer(m_window, m_debounceTimer);
        KillTimer(m_window, m_settleTimer);
        return 0;
    default:
        return DefWindowProc(m_window, msg, wp, lp);
    }
}

} // namespace cw
