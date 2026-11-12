// monitor_watcher.cpp — see monitor_watcher.h.
#include "monitor_watcher.h"

namespace cw {
namespace {

constexpr UINT WM_APP_REBUILD_NOW = WM_APP + 2;
constexpr UINT DEBOUNCE_MS = 500;

MonitorWatcher* g_watcher = nullptr;

} // namespace

// Out-of-line: it references the private wndProc through the shim, and a
// free function inside an anonymous namespace cannot see private members.
LRESULT CALLBACK MonitorWatcher::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE && lp) {
        auto* self = static_cast<MonitorWatcher*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        g_watcher = self;
        return TRUE;
    }
    auto* self = reinterpret_cast<MonitorWatcher*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self)
        return self->wndProc(msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

bool MonitorWatcher::start(RebuildSink onRebuild)
{
    if (m_running.load())
        return true;
    // A previous start() that failed leaves a joinable thread behind; joining it
    // here is the only safe way to reuse this object (see stop()).
    if (m_thread.joinable())
        m_thread.join();
    m_onRebuild = std::move(onRebuild);
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
    g_watcher = nullptr;
}

void MonitorWatcher::run()
{
    WNDCLASSEX wc {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndProcStatic;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"ra9fael.cursorwrap.watcher";
    RegisterClassEx(&wc);

    // HWND_MESSAGE: a message-only window never appears anywhere and cannot
    // be enumerated as a normal top-level window.
    m_window = CreateWindowEx(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                              nullptr, wc.hInstance, this);
    if (!m_window) {
        m_running.store(false);
        return;
    }

    MSG msg;
    while (m_running.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (m_window) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    g_watcher = nullptr;
}

LRESULT MonitorWatcher::wndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: // SPI_SETWORKAREA (taskbar) also moves edges
    case WM_DEVICECHANGE:
        if (m_running.load())
            SetTimer(m_window, m_debounceTimer, DEBOUNCE_MS, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == m_debounceTimer) {
            KillTimer(m_window, m_debounceTimer);
            if (m_onRebuild)
                m_onRebuild();
        }
        return 0;
    case WM_APP_REBUILD_NOW:
        if (m_onRebuild)
            m_onRebuild();
        return 0;
    case WM_CLOSE:
        KillTimer(m_window, m_debounceTimer);
        return 0;
    default:
        return DefWindowProc(m_window, msg, wp, lp);
    }
}

} // namespace cw
