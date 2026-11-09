// monitor_watcher.h — a never-shown top-level window that receives
// WM_DISPLAYCHANGE / WM_SETTINGCHANGE / WM_DEVICECHANGE and rebuilds the
// wrap topology after a 500 ms debounce (PowerToys CursorWrap behaviour:
// display and work-area changes arrive in bursts).
#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <windows.h>

namespace cw {

class MonitorWatcher {
public:
    using RebuildSink = std::function<void()>;

    MonitorWatcher() = default;
    ~MonitorWatcher() { stop(); }
    MonitorWatcher(const MonitorWatcher&) = delete;
    MonitorWatcher& operator=(const MonitorWatcher&) = delete;

    bool start(RebuildSink onRebuild);
    void stop();
    bool active() const { return m_running.load(); }

    // Called from the static WndProc shim (a free function cannot be a
    // private member caller, and a nested class cannot be a window proc).
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp);

private:
    void run();
    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);


    std::thread m_thread;
    std::atomic<bool> m_running { false };
    HWND m_window = nullptr;
    UINT_PTR m_debounceTimer = 1;
    RebuildSink m_onRebuild;
};

} // namespace cw
