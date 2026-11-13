// overlay.cpp — see overlay.h.
//
// What is drawn, and what is deliberately not:
//   * Each OUTER edge gets a strip `impactBandPx` wide (min 8px) along that
//     edge, running the full length of the edge. The strip is a translucent
//     tint with a crisp 2px line on its outer side — this is the "线 / 比较透明
//     的遮罩" the user asked for.
//   * Inner edges (two monitors touching) get nothing: nothing wraps there,
//     and covering them was most of the old "whole screen is tinted" effect.
//   * The middle of every monitor stays completely untouched.
//   * One small floating panel near whichever edge was hit last shows the
//     measured approach speed against the gate; it hides again after a
//     couple of seconds.
//
// Why per-pixel alpha and not a colour key: a colour-keyed window can only be
// fully transparent or fully opaque per pixel, so a translucent band is
// impossible — the old version had to paint an opaque sheet. Per-pixel alpha
// needs UpdateLayeredWindow, which needs a DIB. The trap that cost a crash
// before: allocating a monitor-sized (33 MB on 4K) DIB per frame blew the
// plugin's 64 MB job cap. Here every strip owns ONE small DIB (a 150px band on
// a 2560x1440 panel is ~1.5 MB) that is painted once and presented once; the
// readout panel is 208x74 and only repaints when its numbers change.
#include "overlay.h"

#include "qpc_time.h"
#include "readout_layout.h"

namespace {

// Left/Right edges run vertically; Top/Bottom run horizontally. Local because
// wrap_logic's helper is in an anonymous namespace there.
bool edgeIsVertical(cw::EdgeType t)
{
    return t == cw::EdgeType::Left || t == cw::EdgeType::Right;
}

} // namespace

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cw {
namespace {

constexpr wchar_t kClassName[] = L"WinShunCursorWrapTuner";
constexpr UINT_PTR kTimerId = 7;
constexpr UINT kRepaintMs = 60;
constexpr int kMinBandPx = 8;
constexpr int kReadoutW = 196;

// The readout height is DERIVED from the rows that will be painted, never a
// constant: see readout_layout.h for why (a hard-coded 60 px window with the
// acceleration line drawn from y=46 put the glyphs past the border).
// A band alpha this low reads as a tint over any wallpaper instead of a panel.
constexpr uint8_t kBandAlpha = 24;
constexpr uint8_t kLineAlpha = 235;
constexpr uint8_t kPanelAlpha = 240;
// The readout fades out this long after the last edge arrival.
constexpr long long kReadoutTtlUs = 2'000'000;

std::atomic<Overlay*> g_overlay = nullptr;

COLORREF dim(COLORREF c, int percent)
{
    return RGB(GetRValue(c) * percent / 100, GetGValue(c) * percent / 100,
               GetBValue(c) * percent / 100);
}

long long nowUs()
{
    static const long long freq = [] {
        LARGE_INTEGER f {};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 0;
    }();
    if (freq <= 0)
        return 0;
    LARGE_INTEGER c {};
    QueryPerformanceCounter(&c);
    // Splitting the division avoids the int64 overflow the naive
    // `c * 1'000'000 / freq` hits after ~10.7 days of uptime.
    return counterToUs(c.QuadPart, freq);
}

} // namespace

void Overlay::setColors(const Colors& colors)
{
    std::lock_guard lock(m_colorsMutex);
    m_colors = colors;
}

void Overlay::start(const std::vector<MonitorBox>& monitors)
{
    if (m_running.load())
        stop();
    // stop() joins, but be defensive: assigning over a joinable std::thread is
    // std::terminate, and this method is called again whenever the settings or
    // the layout change.
    if (m_thread.joinable())
        m_thread.join();
    m_monitors = monitors;
    if (m_monitors.empty())
        return;
    m_running.store(true);
    m_thread = std::thread([this] { run(); });
}

void Overlay::stop()
{
    // Join whatever is joinable, never depend on m_running: run() can clear it
    // on its own, and a joinable std::thread reaches std::terminate in its
    // destructor.
    m_running.store(false);
    if (m_threadId)
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);
    if (m_thread.joinable())
        m_thread.join();
    m_threadId = 0;
}

void Overlay::run()
{
    g_overlay.store(this);
    m_threadId = GetCurrentThreadId();

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &Overlay::wndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // Windows unregisters it at process exit

    Colors colors;
    {
        std::lock_guard lock(m_colorsMutex);
        colors = m_colors;
    }
    const Settings settings = m_engine ? m_engine->settings() : Settings {};
    rebuildStrips(settings, colors);

    // The repaint timer lives on its own offscreen anchor window. It used to
    // sit on the first strip's window, which meant: no strips (a fresh start
    // before the engine had a layout, or every monitor inner) → no timer → the
    // glow and the readout never painted, no matter what the settings said.
    m_anchor.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName,
                                    L"cursorwrap timer", WS_POPUP, -32000, -32000, 1, 1,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (m_anchor.hwnd)
        SetTimer(m_anchor.hwnd, kTimerId, kRepaintMs, nullptr);

    MSG msg;
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (Strip& strip : m_strips) {
        if (strip.layer.hwnd)
            DestroyWindow(strip.layer.hwnd);
        destroyLayer(strip.layer);
    }
    m_strips.clear();
    if (m_anchor.hwnd) {
        KillTimer(m_anchor.hwnd, kTimerId);
        DestroyWindow(m_anchor.hwnd);
    }
    m_anchor = {};
    if (m_readout.hwnd) {
        DestroyWindow(m_readout.hwnd);
        destroyLayer(m_readout);
    }
    m_readout = {};
    m_readoutShown = false;
    if (m_flash.hwnd) {
        DestroyWindow(m_flash.hwnd);
        destroyLayer(m_flash);
    }
    m_flash = {};
    m_flashState.cancel();
    m_drawnFlashSeq = 0;
    g_overlay.store(nullptr);
}

LRESULT CALLBACK Overlay::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Overlay* self = g_overlay.load();
    if (self)
        return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Overlay::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId && m_engine) {
            Colors colors;
            {
                std::lock_guard lock(m_colorsMutex);
                colors = m_colors;
            }
            repaint(m_engine->settings(), colors);
            SetTimer(hwnd, kTimerId, kRepaintMs, nullptr);
        }
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT; // clicks fall straight through
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool Overlay::createLayer(Layer& layer, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (!layer.hwnd) {
        const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
            | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        layer.hwnd = CreateWindowExW(exStyle, kClassName, L"cursorwrap tuner", WS_POPUP,
                                     x, y, w, h, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        if (!layer.hwnd)
            return false;
    }
    layer.x = x;
    layer.y = y;
    layer.w = w;
    layer.h = h;

    // Reuse the surface when the size did not change.
    if (layer.dib && layer.dc && layer.screenDc)
        return true;

    layer.screenDc = GetDC(nullptr);
    layer.dc = CreateCompatibleDC(layer.screenDc);
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    layer.dib = CreateDIBSection(layer.screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!layer.dib || !bits) {
        destroyLayer(layer);
        return false;
    }
    layer.bits = static_cast<uint32_t*>(bits);
    layer.oldBitmap = SelectObject(layer.dc, layer.dib);
    std::memset(layer.bits, 0, static_cast<size_t>(w) * h * 4);
    return true;
}

void Overlay::destroyLayer(Layer& layer)
{
    if (layer.dc) {
        if (layer.oldBitmap)
            SelectObject(layer.dc, layer.oldBitmap);
        DeleteDC(layer.dc);
        layer.dc = nullptr;
    }
    if (layer.dib) {
        DeleteObject(layer.dib);
        layer.dib = nullptr;
    }
    if (layer.screenDc) {
        ReleaseDC(nullptr, layer.screenDc);
        layer.screenDc = nullptr;
    }
    layer.oldBitmap = nullptr;
    layer.bits = nullptr;
    layer.w = layer.h = 0;
    layer.shown = false;
}

void Overlay::present(Layer& layer)
{
    if (!layer.hwnd || !layer.dc || !layer.bits)
        return;
    // GDI left 0x00RRGGBB; the paint step stored the coverage in the top
    // byte. UpdateLayeredWindow wants premultiplied BGRA.
    const size_t count = static_cast<size_t>(layer.w) * layer.h;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t v = layer.bits[i];
        const uint32_t a = v >> 24;
        if (a == 0) {
            layer.bits[i] = 0;
            continue;
        }
        if (a == 255) {
            layer.bits[i] = v | 0xFF000000u;
            continue;
        }
        const uint32_t r = (v >> 16) & 0xFF;
        const uint32_t g = (v >> 8) & 0xFF;
        const uint32_t b = v & 0xFF;
        layer.bits[i] = (a << 24) | (((r * a) / 255) << 16) | (((g * a) / 255) << 8)
            | ((b * a) / 255);
    }
    POINT src { 0, 0 };
    SIZE size { layer.w, layer.h };
    POINT dst { layer.x, layer.y };
    BLENDFUNCTION blend {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(layer.hwnd, layer.screenDc, &dst, &size, layer.dc, &src, 0,
                        &blend, ULW_ALPHA);
    if (!layer.shown) {
        ShowWindow(layer.hwnd, SW_SHOWNOACTIVATE);
        layer.shown = true;
    }
}

void Overlay::setAlpha(const Layer& layer, int x0, int y0, int x1, int y1, uint8_t alpha)
{
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(layer.w, x1);
    y1 = std::min(layer.h, y1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t& v = layer.bits[static_cast<size_t>(y) * layer.w + x];
            v = (v & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
        }
    }
}

void Overlay::rebuildStrips(const Settings& settings, const Colors& colors)
{
    for (Strip& strip : m_strips) {
        if (strip.layer.hwnd) {
            KillTimer(strip.layer.hwnd, kTimerId);
            DestroyWindow(strip.layer.hwnd);
        }
        destroyLayer(strip.layer);
    }
    m_strips.clear();

    if (m_engine) {
        m_monitors = m_engine->monitorsSnapshot();
        m_edges = m_engine->outerEdgesSnapshot();
    }
    m_bandPx = std::max(kMinBandPx, settings.impactBandPx);
    m_tuningOverlayBuilt = settings.tuningOverlay;

    const COLORREF tint = dim(colors.accent, 70);
    const uint8_t bandAlpha = settings.tuningOverlay ? kBandAlpha : 0;
    const uint8_t lineAlpha = settings.tuningOverlay ? kLineAlpha : 0;
    for (const MonitorEdge& edge : m_edges) {
        if (edge.monitorIndex < 0
            || static_cast<size_t>(edge.monitorIndex) >= m_monitors.size())
            continue;
        const MonitorBox& box = m_monitors[static_cast<size_t>(edge.monitorIndex)];
        Strip strip;
        strip.monitorIndex = edge.monitorIndex;
        strip.edge = edge.type;

        int x = static_cast<int>(box.left);
        int y = static_cast<int>(box.top);
        int w = static_cast<int>(box.width());
        int h = static_cast<int>(box.height());
        switch (edge.type) {
        case EdgeType::Left: w = m_bandPx; break;
        case EdgeType::Right: x = static_cast<int>(box.right) - m_bandPx; w = m_bandPx; break;
        case EdgeType::Top: h = m_bandPx; break;
        case EdgeType::Bottom: y = static_cast<int>(box.bottom) - m_bandPx; h = m_bandPx; break;
        }
        if (!createLayer(strip.layer, x, y, w, h)) {
            destroyLayer(strip.layer);
            continue;
        }

        // The tint across the whole strip, then the crisp line on the outer
        // side; the alpha ramp is applied after, per region.
        HBRUSH brush = CreateSolidBrush(tint);
        RECT all { 0, 0, w, h };
        FillRect(strip.layer.dc, &all, brush);
        DeleteObject(brush);
        brush = CreateSolidBrush(colors.accent);
        RECT line { 0, 0, w, h };
        switch (edge.type) {
        case EdgeType::Left: line = { 0, 0, 2, h }; break;
        case EdgeType::Right: line = { w - 2, 0, w, h }; break;
        case EdgeType::Top: line = { 0, 0, w, 2 }; break;
        case EdgeType::Bottom: line = { 0, h - 2, w, h }; break;
        }
        FillRect(strip.layer.dc, &line, brush);
        DeleteObject(brush);

        // Translucent tint, opaque line: two ranges instead of per-pixel work.
        setAlpha(strip.layer, 0, 0, w, h, bandAlpha);
        switch (edge.type) {
        case EdgeType::Left: setAlpha(strip.layer, 0, 0, 2, h, lineAlpha); break;
        case EdgeType::Right: setAlpha(strip.layer, w - 2, 0, w, h, lineAlpha); break;
        case EdgeType::Top: setAlpha(strip.layer, 0, 0, w, 2, lineAlpha); break;
        case EdgeType::Bottom: setAlpha(strip.layer, 0, h - 2, w, h, lineAlpha); break;
        }
        present(strip.layer);
        m_strips.push_back(std::move(strip));
    }
}

void Overlay::buildReadout(const EdgeVerdict& verdict, const Settings& settings,
                           const Colors& colors)
{
    if (verdict.monitorIndex < 0
        || static_cast<size_t>(verdict.monitorIndex) >= m_monitors.size())
        return;
    const MonitorBox& box = m_monitors[static_cast<size_t>(verdict.monitorIndex)];
    const int w = kReadoutW;
    // The acceleration line is drawn only when the user enabled that condition,
    // so the window is sized for what will actually be painted.
    const bool withAccel = cw::showsAccelLine(verdict);
    const int h = cw::readoutHeight(verdict);
    const int margin = 20;
    int x = static_cast<int>(verdict.x) + margin;
    int y = static_cast<int>(verdict.y) - h / 2;
    switch (verdict.edge) {
    case EdgeType::Left: x = static_cast<int>(box.left) + m_bandPx + margin; break;
    case EdgeType::Right: x = static_cast<int>(box.right) - m_bandPx - margin - w; break;
    case EdgeType::Top: y = static_cast<int>(box.top) + m_bandPx + margin; break;
    case EdgeType::Bottom: y = static_cast<int>(box.bottom) - m_bandPx - margin - h; break;
    }
    x = std::max<int>(static_cast<int>(box.left) + 4,
                      std::min<int>(x, static_cast<int>(box.right) - w - 4));
    y = std::max<int>(static_cast<int>(box.top) + 4,
                      std::min<int>(y, static_cast<int>(box.bottom) - h - 4));

    if (!createLayer(m_readout, x, y, w, h))
        return;
    Layer& layer = m_readout;
    SetWindowPos(layer.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

    const bool ok = verdict.wrapped;
    const COLORREF state = ok ? RGB(0x30, 0xc0, 0x60) : RGB(0xe0, 0xa0, 0x20);

    HBRUSH bg = CreateSolidBrush(dim(colors.background, 92));
    RECT all { 0, 0, w, h };
    FillRect(layer.dc, &all, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(state);
    RECT edges[4] = { { 0, 0, w, 2 }, { 0, h - 2, w, h }, { 0, 0, 2, h }, { w - 2, 0, w, h } };
    for (const RECT& r : edges)
        FillRect(layer.dc, &r, border);
    DeleteObject(border);

    SetBkMode(layer.dc, TRANSPARENT);
    HFONT small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT big = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    SelectObject(layer.dc, small);
    SetTextColor(layer.dc, colors.text);
    // The caption comes from readout_layout so the tested wording and the drawn
    // wording cannot drift apart (the drawn copy used to say "approach speed"
    // while the tested one said "not accelerating" for the same verdict).
    const std::string captionUtf8 = cw::readoutCaption(verdict);
    const int captionNeed = MultiByteToWideChar(CP_UTF8, 0, captionUtf8.c_str(), -1,
                                                nullptr, 0);
    std::wstring captionW(static_cast<size_t>(captionNeed > 0 ? captionNeed - 1 : 0), L'\0');
    if (captionNeed > 1)
        MultiByteToWideChar(CP_UTF8, 0, captionUtf8.c_str(), -1, captionW.data(), captionNeed);
    TextOutW(layer.dc, cw::ReadoutMetrics::padX, cw::ReadoutMetrics::padTop, captionW.c_str(),
             static_cast<int>(captionW.size()));

    const int bigY = cw::readoutBigY();
    SelectObject(layer.dc, big);
    SetTextColor(layer.dc, state);
    wchar_t line[96];
    if (verdict.gateOff || verdict.guardBlocked || verdict.thresholdPps <= 0) {
        const wchar_t* word = verdict.wrapped ? L"wrap" : L"—";
        TextOutW(layer.dc, cw::ReadoutMetrics::padX, bigY, word, static_cast<int>(wcslen(word)));
    } else {
        swprintf(line, 96, L"%lld / %lld px/s", verdict.measuredPps, verdict.thresholdPps);
        TextOutW(layer.dc, cw::ReadoutMetrics::padX, bigY, line, static_cast<int>(wcslen(line)));
        // The acceleration line: only when the user turned that condition on, so
        // the panel does not show a meaningless 0 for everyone else. A negative
        // value means the hand was braking — the reason it did not wrap.
        //
        // "accel 測不到" is spelled out rather than an em dash: the dash made
        // users read the line as "the acceleration is 1600" (the threshold),
        // when it actually means the slope could not be measured at all.
        if (withAccel) {
            SelectObject(layer.dc, small);
            SetTextColor(layer.dc, verdict.accelBlocked ? state : colors.text);
            if (verdict.accelMeasured)
                swprintf(line, 96, L"accel %+lld / %lld px/s²", verdict.measuredAccelPps2,
                         verdict.thresholdAccelPps2);
            else
                swprintf(line, 96, L"accel n/a / %lld px/s²", verdict.thresholdAccelPps2);
            TextOutW(layer.dc, cw::ReadoutMetrics::padX, cw::readoutAccelY(verdict), line,
                     static_cast<int>(wcslen(line)));
        }
    }
    DeleteObject(small);
    DeleteObject(big);

    setAlpha(layer, 0, 0, w, h, kPanelAlpha);
    present(layer);
    (void)settings;
    m_readoutShown = true;
}

void Overlay::hideReadout()
{
    if (m_readout.hwnd && m_readout.shown) {
        ShowWindow(m_readout.hwnd, SW_HIDE);
        m_readout.shown = false;
    }
    m_readoutShown = false;
}

void Overlay::repaint(const Settings& settings, const Colors& colors)
{
    if (!m_engine)
        return;

    // The band width is a setting, and the monitor layout can change: rebuild
    // the static strips when either moves.
    const int band = std::max(kMinBandPx, settings.impactBandPx);
    const std::vector<MonitorEdge> edges = m_engine->outerEdgesSnapshot();
    bool stale = band != m_bandPx || settings.tuningOverlay != m_tuningOverlayBuilt
        || edges.size() != m_edges.size();
    if (!stale) {
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].monitorIndex != m_edges[i].monitorIndex
                || edges[i].type != m_edges[i].type
                || edges[i].position != m_edges[i].position) {
                stale = true;
                break;
            }
        }
    }
    if (stale) {
        rebuildStrips(settings, colors);
        // The timer used to be re-armed here on the new first strip; it now
        // lives on the anchor window and survives a rebuild untouched.
    }

    const EdgeVerdict verdict = m_engine->lastVerdict();

    // The impact glow. Triggered from the engine's LATCHED wrap event: a
    // successful wrap lives for one sample in the per-sample verdict, so the
    // 60 ms poll below missed it essentially every time (the shell never saw
    // this because its scripted hand stops after the wrap). The latch survives
    // the guard-blocked samples that follow, and `onWrapFailed` drops it when
    // the OS refused the landing, so a glow can never mean "that one wrapped".
    const WrapEvent wrapEvent = m_engine->lastWrapEvent();
    if (wrapEvent.seq > 0 && wrapEvent.seq != m_drawnFlashSeq) {
        m_drawnFlashSeq = wrapEvent.seq;
        // The clock the glow fades on is nowUs(), not wrapEvent.tUs: on a real
        // machine they are the same counter, but a caller that drives the engine
        // with its own timestamps must not end up with a glow that is already
        // "expired" the moment it is created.
        const long long startedUs = nowUs();
        const bool wants = settings.impactFlash && settings.impactGate;
        if (!wants)
            m_flashState.cancel();
        else
            m_flashState.trigger(wrapEvent.edge, wrapEvent.edgeCoord, wrapEvent.along,
                                 edgeLengthOf(wrapEvent.monitorIndex, wrapEvent.edge),
                                 startedUs);
    }
    paintImpactFlash(settings, colors, wrapEvent.monitorIndex);

    // The readout is fed from the LATCHED arrival verdict (it stays until the
    // next arrival), with the live verdict only overriding for a guard block —
    // "guard · blocked" is a statement about THIS moment, the measured numbers
    // are a statement about the arrival.
    EdgeVerdict shown = m_engine->lastArrivalVerdict();
    if (verdict.guardBlocked) {
        shown = verdict;
    } else if (shown.tUs > 0) {
        // The live gate-off path (impact mode off) sets wrapped/gateOff per
        // sample; the latched verdict already describes the arrival it came
        // from. Nothing to merge — keep the latch.
    }

    const bool fresh = shown.tUs > 0 && nowUs() - shown.tUs < kReadoutTtlUs;
    if (!settings.tuningOverlay || !fresh) {
        if (m_readoutShown)
            hideReadout();
        return;
    }
    if (shown.tUs == m_drawnVerdictUs && shown.measuredPps == m_drawnPps
        && shown.measuredAccelPps2 == m_drawnAccel && m_readoutShown)
        return;
    m_drawnVerdictUs = shown.tUs;
    m_drawnPps = shown.measuredPps;
    m_drawnAccel = shown.measuredAccelPps2;
    buildReadout(shown, settings, colors);
}

bool Overlay::impactFlashVisible() const
{
    return m_flashState.active() && m_flashState.alphaAt(nowUs()) > 0.0;
}

void Overlay::hideImpactFlash()
{
    if (m_flash.hwnd && m_flash.shown) {
        ShowWindow(m_flash.hwnd, SW_HIDE);
        m_flash.shown = false;
    }
}

// The length of one edge of one monitor, in pixels. The glow is sized as a
// fraction of it, so a 4K display does not get a kilometre of light.
int Overlay::edgeLengthOf(int monitorIndex, EdgeType edge) const
{
    if (monitorIndex < 0 || static_cast<size_t>(monitorIndex) >= m_monitors.size())
        return 0;
    const MonitorBox& box = m_monitors[static_cast<size_t>(monitorIndex)];
    return static_cast<int>(edgeIsVertical(edge) ? box.bottom - box.top
                                             : box.right - box.left);
}

void Overlay::paintImpactFlash(const Settings& settings, const Colors& colors,
                               int monitorIndex)
{
    // Deliberately unconditional about the clock: ImpactFlash owns the timing,
    // and a stale or backwards timestamp makes it report 0 so the glow goes away
    // instead of sticking.
    const long long t = nowUs();
    const double alpha = m_flashState.alphaAt(t);
    if (alpha <= 0.0 || !m_flashState.active()) {
        hideImpactFlash();
        return;
    }
    if (!settings.impactFlash) {
        hideImpactFlash();
        return;
    }

    const EdgeType edge = m_flashState.edge();
    const bool horiz = edgeIsVertical(edge);
    const int thickness = ImpactFlash::kThickness;
    const int length = static_cast<int>(m_flashState.lengthAlong());
    const int start = static_cast<int>(m_flashState.startAlong());
    const int64_t coord = m_flashState.edgeCoord();

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    if (horiz) {
        // A vertical bar hugging the left/right edge.
        x = static_cast<int>(edge == EdgeType::Left ? coord : coord - thickness + 1);
        w = thickness;
        h = length;
        // `start` is measured along the edge from the monitor's top.
        y = monitorIndex >= 0 && static_cast<size_t>(monitorIndex) < m_monitors.size()
                ? static_cast<int>(m_monitors[static_cast<size_t>(monitorIndex)].top) + start
                : start;
    } else {
        const int monitorLeft = monitorIndex >= 0
                && static_cast<size_t>(monitorIndex) < m_monitors.size()
            ? static_cast<int>(m_monitors[static_cast<size_t>(monitorIndex)].left)
            : 0;
        x = monitorLeft + start;
        y = static_cast<int>(edge == EdgeType::Top ? coord : coord - thickness + 1);
        w = length;
        h = thickness;
    }
    if (w <= 0 || h <= 0)
        return;
    if (!createLayer(m_flash, x, y, w, h))
        return;
    Layer& layer = m_flash;
    SetWindowPos(layer.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

    // A soft bar: brightest in the middle, transparent at both ends, so it reads
    // as light rather than as a rectangle.
    const uint8_t peak = static_cast<uint8_t>(std::min(255.0, alpha * 245.0));
    HBRUSH brush = CreateSolidBrush(dim(colors.accent, 40));
    RECT all { 0, 0, w, h };
    FillRect(layer.dc, &all, brush);
    DeleteObject(brush);
    const int span = horiz ? h : w;
    for (int i = 0; i < span; ++i) {
        const double d = span <= 1 ? 0.0
            : std::abs(static_cast<double>(i) - (span - 1) / 2.0) / ((span - 1) / 2.0);
        const double edgeFade = 1.0 - d * d; // 1 in the middle, 0 at the ends
        const uint8_t a = static_cast<uint8_t>(peak * std::max(0.0, edgeFade));
        if (horiz)
            setAlpha(layer, 0, i, w, i + 1, a);
        else
            setAlpha(layer, i, 0, i + 1, h, a);
    }
    present(layer);
    m_flash.shown = true;
}

} // namespace cw
