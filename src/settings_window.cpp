// settings_window.cpp — see settings_window.h.
#include "settings_window.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef WC_STATICW
#define WC_STATICW L"Static"
#endif

namespace cw {
namespace {

constexpr wchar_t kClassName[] = L"WinShunCursorWrapSettings";
constexpr int kTimerId = 0xC1DE;
constexpr int kRefreshMs = 1000;
constexpr UINT kAppRefresh = WM_APP + 7;

// Control ids. A numeric row is label + edit + unit + button; the id of the
// row's button is the edit's id + 1, so applyNumber can pair them by table.
enum {
    IDC_SUMMARY = 200,
    IDC_SPEED = 210,
    IDC_BAND = 220,
    IDC_GUARD = 230,
    IDC_EDGE = 240,
    IDC_TOL = 250,
    IDC_ACCEL = 260,
    IDC_ACCEL_OFF,   // 261: the "off" checkbox of the acceleration row
    IDC_OVERLAY = 270,
    IDC_FLASH,
    IDC_DATA,
    IDC_ADVICE = 280,
    IDC_ADVICE_APPLY,
    IDC_RESET,
};

struct RowSpec {
    int editId;
    const char* key; // the range table's key and the RawSettings field
    const wchar_t* label;
    const wchar_t* unit;
};

constexpr RowSpec kRows[] = {
    { IDC_SPEED, "impactSpeedPps", L"撞击速度 / Impact speed", L"px/s" },
    { IDC_BAND, "impactBandPx", L"测量带宽 / Measuring band", L"px" },
    { IDC_GUARD, "wrapGuardPx", L"防震半径 / Wrap guard", L"px" },
    { IDC_EDGE, "edgeThresholdPx", L"边缘阈值 / Edge threshold", L"px" },
    { IDC_TOL, "adjacencyTolerancePx", L"邻接容差 / Adjacency tolerance", L"px" },
    { IDC_ACCEL, "impactAccelPps2", L"撞击加速度 / Acceleration", L"px/s²" },
};

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                        need);
    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                        need, nullptr, nullptr);
    return out;
}

HWND control(HWND hwnd, int id)
{
    return GetDlgItem(hwnd, id);
}

void setWindowText(HWND hwnd, int id, const std::wstring& text)
{
    SetWindowTextW(control(hwnd, id), text.c_str());
}

bool chineseDefault()
{
    // The controls' own labels are bilingual; the summary line and the advice
    // come from the provider in the plugin's locale.
    return true;
}

std::wstring boolText(bool on, bool chinese)
{
    return on ? (chinese ? L"开" : L"on") : (chinese ? L"关" : L"off");
}

} // namespace

void SettingsWindow::start(Provider provider, Apply apply)
{
    if (m_running.exchange(true))
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_provider = std::move(provider);
    m_apply = std::move(apply);
    m_thread = std::thread([this] { run(); });
}

void SettingsWindow::stop()
{
    m_running.store(false);
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (m_thread.joinable())
        m_thread.join();
    m_threadId = 0;
    m_hwnd = nullptr;
}

void SettingsWindow::settingsUpdated()
{
    if (m_hwnd)
        PostMessageW(m_hwnd, kAppRefresh, 0, 0);
}

void SettingsWindow::run()
{
    m_threadId = GetCurrentThreadId();

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &SettingsWindow::wndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect { 0, 0, kWidth, kHeight };
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    m_hwnd = CreateWindowExW(0, kClassName,
                             L"回绕调参 / Cursor wrap tuning", style,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             static_cast<int>(rect.right - rect.left),
                             static_cast<int>(rect.bottom - rect.top),
                             nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) {
        m_running.store(false);
        return;
    }

    NONCLIENTMETRICSW ncm {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    m_font = CreateFontIndirectW(&ncm.lfMessageFont);
    buildControls();
    refresh();

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    SetTimer(m_hwnd, kTimerId, kRefreshMs, nullptr);
    MSG msg;
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(m_hwnd, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(m_hwnd, kTimerId);
    if (m_hwnd)
        DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
}

void SettingsWindow::buildControls()
{
    auto make = [this](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y,
                       int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                 m_hwnd, reinterpret_cast<HMENU>(
                                     static_cast<intptr_t>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
        if (m_font)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        return c;
    };

    make(WC_STATICW, L"", 0, 16, 14, kWidth - 32, 20, IDC_SUMMARY);

    int y = 48;
    for (const RowSpec& row : kRows) {
        make(WC_STATICW, row.label, 0, 16, y + 4, 200, 20, 0);
        make(L"EDIT", L"", WS_BORDER | ES_NUMBER, 220, y, 80, 24, row.editId);
        make(WC_STATICW, row.unit, 0, 306, y + 4, 60, 20, 0);
        make(L"BUTTON", chineseDefault() ? L"应用" : L"Apply",
             BS_PUSHBUTTON, 372, y, 110, 26, row.editId + 1);
        y += 38;
    }

    // The acceleration row's "off" switch: 0 is a real choice, not a missing
    // number, so it gets its own checkbox instead of being implied by an empty
    // edit box.
    make(L"BUTTON", chineseDefault() ? L"关闭" : L"Off", BS_AUTOCHECKBOX, 306, y - 38 + 4,
         60, 20, IDC_ACCEL_OFF);

    y += 12;
    make(L"BUTTON", chineseDefault() ? L"调参可视化" : L"Tuning overlay", BS_AUTOCHECKBOX,
         16, y, kWidth - 32, 24, IDC_OVERLAY);
    y += 30;
    make(L"BUTTON", chineseDefault() ? L"撞击光效" : L"Impact glow", BS_AUTOCHECKBOX,
         16, y, kWidth - 32, 24, IDC_FLASH);
    y += 30;
    make(L"BUTTON", chineseDefault() ? L"数据窗口" : L"Data window", BS_AUTOCHECKBOX,
         16, y, kWidth - 32, 24, IDC_DATA);

    y += 44;
    make(WC_STATICW, L"", 0, 16, y, kWidth - 32, 40, IDC_ADVICE);
    y += 46;
    make(L"BUTTON", chineseDefault() ? L"采用建议值" : L"Use the suggested gate",
         BS_PUSHBUTTON, 16, y, 220, 30, IDC_ADVICE_APPLY);
    make(L"BUTTON", chineseDefault() ? L"恢复默认调参" : L"Reset tuning",
         BS_PUSHBUTTON, 248, y, 220, 30, IDC_RESET);
}std::wstring SettingsWindow::editText(int id) const
{
    wchar_t buf[32];
    const int n = GetWindowTextW(control(m_hwnd, id), buf, 32);
    return std::wstring(buf, buf + std::max(0, n));
}

void SettingsWindow::applyNumber(int editId, const char* key, RawSettings& raw, bool& ok)
{
    const std::wstring text = editText(editId);
    if (text.empty()) {
        ok = false;
        return;
    }
    long long v = 0;
    try {
        v = std::stoll(text);
    } catch (...) {
        ok = false;
        return;
    }
    SettingRange range;
    if (!settingRange(key, range)) {
        ok = false;
        return;
    }
    if (v < range.lo)
        v = range.lo;
    if (v > range.hi)
        v = range.hi;
    ok = true;
    // The RawSettings mirrors Settings member by member; the key names the
    // member, and this switch is the same one the JSON path uses.
    const std::string k = key;
    if (k == "impactSpeedPps") {
        raw.impactSpeedPps = static_cast<double>(v);
        raw.hasImpactSpeed = true;
    } else if (k == "impactBandPx") {
        raw.impactBandPx = static_cast<double>(v);
        raw.hasImpactBand = true;
    } else if (k == "wrapGuardPx") {
        raw.wrapGuardPx = static_cast<double>(v);
        raw.hasWrapGuard = true;
    } else if (k == "edgeThresholdPx") {
        raw.edgeThresholdPx = static_cast<double>(v);
        raw.hasEdgeThreshold = true;
    } else if (k == "adjacencyTolerancePx") {
        raw.adjacencyTolerancePx = static_cast<double>(v);
        raw.hasAdjacency = true;
    } else if (k == "impactAccelPps2") {
        raw.impactAccelPps2 = static_cast<double>(v);
        raw.hasImpactAccel = true;
    }
}

void SettingsWindow::refresh()
{
    if (!m_provider || !m_hwnd)
        return;
    const SettingsUiState s = m_provider();
    const bool chinese = s.chinese;
    const Settings& cur = s.settings;

    setWindowText(m_hwnd, IDC_SUMMARY, widen(s.summary));

    for (const RowSpec& row : kRows) {
        long long v = 0;
        const std::string k = row.key;
        if (k == "impactSpeedPps") v = cur.impactSpeedPps;
        else if (k == "impactBandPx") v = cur.impactBandPx;
        else if (k == "wrapGuardPx") v = cur.wrapGuardPx;
        else if (k == "edgeThresholdPx") v = cur.edgeThresholdPx;
        else if (k == "adjacencyTolerancePx") v = cur.adjacencyTolerancePx;
        else if (k == "impactAccelPps2") v = cur.impactAccelPps2;
        // While the user edits this box, their typing wins: only fill it when
        // the focus is elsewhere and the text differs from the value in effect.
        const HWND edit = control(m_hwnd, row.editId);
        if (GetFocus() != edit && editText(row.editId) != std::to_wstring(v))
            setWindowText(m_hwnd, row.editId, std::to_wstring(v));
    }
    const bool accelOff = cur.impactAccelPps2 == 0;
    SendMessageW(control(m_hwnd, IDC_ACCEL_OFF), BM_SETCHECK,
                 accelOff ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(control(m_hwnd, IDC_ACCEL), accelOff ? FALSE : TRUE);

    SendMessageW(control(m_hwnd, IDC_OVERLAY), BM_SETCHECK,
                 cur.tuningOverlay ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(control(m_hwnd, IDC_FLASH), BM_SETCHECK,
                 cur.impactFlash ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(control(m_hwnd, IDC_DATA), BM_SETCHECK,
                 cur.dataWindow ? BST_CHECKED : BST_UNCHECKED, 0);

    std::wstring advice;
    if (s.advice.fits)
        advice = chinese ? L"当前门槛合适：慢移会被拦住，快甩能越过。"
                         : L"The current gate fits: slow drifts are refused, flicks go through.";
    else if (s.advice.pps <= 0)
        advice = chinese ? L"还没有足够的数据：向边缘甩几次，再慢移几次。"
                         : L"Not enough data yet: flick to an edge a few times, then drift to it.";
    else
        advice = (chinese ? L"建议门槛 " : L"Suggested gate ") + std::to_wstring(s.advice.pps)
            + (chinese ? L" px/s（当前 " : L" px/s (current ")
            + std::to_wstring(cur.impactSpeedPps)
            + (chinese ? L"），来自 " : L"), from ") + std::to_wstring(s.arrivals)
            + (chinese ? L" 条到达记录" : L" arrivals");
    setWindowText(m_hwnd, IDC_ADVICE, advice);
    EnableWindow(control(m_hwnd, IDC_ADVICE_APPLY),
                 (!s.advice.fits && s.advice.pps > 0
                  && s.advice.pps != cur.impactSpeedPps) ? TRUE : FALSE);
}

LRESULT CALLBACK SettingsWindow::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SettingsWindow*>(
            reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);
    return self->wndProc(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        // Closing the window changes nothing: the settings stay as they are,
        // and the wrap state is untouched.
        m_running.store(false);
        PostQuitMessage(0);
        return 0;
    case kAppRefresh:
        refresh();
        return 0;
    case WM_TIMER:
        if (wp == kTimerId)
            refresh();
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        const int code = HIWORD(wp);
        if (code != BN_CLICKED)
            break;
        if (!m_apply)
            break;
        RawSettings raw;
        bool ok = true;
        if (id == IDC_ADVICE_APPLY) {
            if (m_provider) {
                const SettingsUiState s = m_provider();
                if (s.advice.pps > 0 && !s.advice.fits) {
                    raw.impactSpeedPps = static_cast<double>(s.advice.pps);
                    raw.hasImpactSpeed = true;
                    m_apply(raw);
                }
            }
            return 0;
        }
        if (id == IDC_RESET) {
            const Settings defaults;
            raw.impactSpeedPps = defaults.impactSpeedPps;
            raw.hasImpactSpeed = true;
            raw.impactBandPx = defaults.impactBandPx;
            raw.hasImpactBand = true;
            raw.impactAccelPps2 = defaults.impactAccelPps2;
            raw.hasImpactAccel = true;
            raw.wrapGuardPx = defaults.wrapGuardPx;
            raw.hasWrapGuard = true;
            raw.edgeThresholdPx = defaults.edgeThresholdPx;
            raw.hasEdgeThreshold = true;
            raw.adjacencyTolerancePx = defaults.adjacencyTolerancePx;
            raw.hasAdjacency = true;
            m_apply(raw);
            return 0;
        }
        if (id == IDC_OVERLAY) {
            raw.tuningOverlay = IsDlgButtonChecked(hwnd, IDC_OVERLAY) == BST_CHECKED;
            raw.hasTuningOverlay = true;
            m_apply(raw);
            return 0;
        }
        if (id == IDC_FLASH) {
            raw.impactFlash = IsDlgButtonChecked(hwnd, IDC_FLASH) == BST_CHECKED;
            raw.hasImpactFlash = true;
            m_apply(raw);
            return 0;
        }
        if (id == IDC_DATA) {
            raw.dataWindow = IsDlgButtonChecked(hwnd, IDC_DATA) == BST_CHECKED;
            raw.hasDataWindow = true;
            m_apply(raw);
            return 0;
        }
        if (id == IDC_ACCEL_OFF) {
            // The switch IS the zero: off applies 0, on (checked off) lets the
            // numeric entry stand. Turning the floor off keeps the number in
            // the box for the next enable, so it is not lost to the checkbox.
            const bool checked = IsDlgButtonChecked(hwnd, IDC_ACCEL_OFF) == BST_CHECKED;
            EnableWindow(control(hwnd, IDC_ACCEL), checked ? FALSE : TRUE);
            if (checked) {
                raw.impactAccelPps2 = 0;
                raw.hasImpactAccel = true;
                m_apply(raw);
            }
            return 0;
        }
        for (const RowSpec& row : kRows) {
            if (id == row.editId + 1) {
                applyNumber(row.editId, row.key, raw, ok);
                if (ok)
                    m_apply(raw);
                else
                    MessageBeep(MB_ICONEXCLAMATION);
                return 0;
            }
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace cw
