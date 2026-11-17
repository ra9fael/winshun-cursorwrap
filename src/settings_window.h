// settings_window.h — the tuning window: the advanced settings in one place.
//
// The search bar shows the everyday switches; the numbers that need the
// measuring band, the histogram and the suggestion next to them live here.
// The window edits exactly the settings the plugin owns (the host card's keys
// are shown read-only in the summary line), and every change it makes goes
// through the same commit path a search-bar action takes — there is no second
// update path to keep in step.
//
// Design notes (the data window's, kept deliberately):
//   * One persistent window on its own thread with native controls; a change
//     posted from it is handed to the plugin's commit path, which guards the
//     settings with the same mutex the coalescer uses.
//   * Editing a field applies ONLY that field: no control here resets another.
//   * Acceleration's 0 means "off": an explicit checkbox, with the numeric
//     entry for the non-zero threshold beside it — a slider would step over
//     the values a hand can actually produce.
//   * The suggested gate is shown with its data; adopting it is a button the
//     user presses, never something the window does on its own.
#pragma once

#include "settings_store.h"
#include "wrap_logic.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <windows.h>

namespace cw {

// What the window displays; filled by the plugin on demand.
struct SettingsUiState {
    Settings settings;       // the values in effect right now
    GateAdvice advice;       // the suggested gate (adviseGate)
    std::string summary;     // the host card's keys, read-only, one line
    size_t arrivals = 0;     // trace rows behind the suggestion
    bool chinese = true;
};

class SettingsWindow {
public:
    SettingsWindow() = default;
    ~SettingsWindow() { stop(); }
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // The current state for the labels and edit boxes.
    using Provider = std::function<SettingsUiState()>;
    // A control's change, as a partial RawSettings. Called on the window's
    // thread; the implementor runs it through the plugin's commit path.
    using Apply = std::function<void(const RawSettings&)>;

    void start(Provider provider, Apply apply);
    void stop();
    bool active() const { return m_running.load(); }

    // The settings just changed elsewhere (a search action, the host card):
    // repaint from the provider. Safe from any thread.
    void settingsUpdated();

    static constexpr int kWidth = 500;
    static constexpr int kHeight = 620;

private:
    void run();
    void buildControls();
    void refresh();
    // One numeric row's apply: read the edit, clamp to the field's range, apply.
    void applyNumber(int editId, const char* key, RawSettings& raw, bool& ok);
    std::wstring editText(int id) const;

    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    std::thread m_thread;
    std::atomic<bool> m_running { false };
    DWORD m_threadId = 0;
    HWND m_hwnd = nullptr;
    Provider m_provider;
    Apply m_apply;
    HFONT m_font = nullptr;
};

} // namespace cw
