// monitor_watcher.h — a never-shown top-level window that receives
// WM_DISPLAYCHANGE / WM_SETTINGCHANGE / WM_DEVICECHANGE (they are broadcast
// to top-level windows, which a message-only window is not: the old
// HWND_MESSAGE watcher never saw one, which is why a topology survived an
// RDP disconnect until the plugin was restarted), WM_WTSSESSION_CHANGE and
// WM_POWERBROADCAST, and rebuilds the wrap topology once the enumeration
// settles (PowerToys CursorWrap behaviour: display and work-area changes
// arrive in bursts).
#pragma once

#include "topology_settle.h"

#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <windows.h>

namespace cw {

class MonitorWatcher {
public:
    // The layout source and what to do with a settled one. Both run on the
    // watcher's own thread.
    using Enumerate = std::function<std::vector<MonitorBox>()>;
    using Commit = std::function<void(const std::vector<MonitorBox>&)>;

    MonitorWatcher() = default;
    ~MonitorWatcher() { stop(); }
    MonitorWatcher(const MonitorWatcher&) = delete;
    MonitorWatcher& operator=(const MonitorWatcher&) = delete;

    bool start(Enumerate enumerate, Commit commit);
    void stop();
    bool active() const { return m_running.load(); }

    // Called from the static WndProc shim (a free function cannot be a
    // private member caller, and a nested class cannot be a window proc).
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp);

private:
    void run();
    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void sampleNow(); // one enumeration into the settle window

    std::thread m_thread;
    std::atomic<bool> m_running { false };
    HWND m_window = nullptr;
    UINT_PTR m_debounceTimer = 1;
    UINT_PTR m_settleTimer = 2;
    Enumerate m_enumerate;
    Commit m_commit;
    TopologySettle m_settle;
    int m_settleTries = 0;
};

} // namespace cw
