// LiquidGlass - exported C API.
// Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
//
// The dll only draws: AHK owns the window, its messages and hotkeys. Every export is
// wrapped so that no C++ exception ever crosses into AutoHotkey; failures return 0 and
// leave a message for LG_LastError().
#include "context.h"

#include <dwmapi.h>

#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <exception>
#include <mutex>
#include <new>
#include <set>

namespace lg {
namespace {

thread_local std::wstring t_error;
std::mutex g_mutex;
std::set<Context*> g_live;
ULONG_PTR g_gdipToken = 0;
bool g_gdipStarted = false;

std::wstring Widen(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    w.resize(static_cast<size_t>(n - 1));
    return w;
}

// Runs f, converting any exception into an error message and the `fail` value.
template <class R, class F>
R Guard(const wchar_t* fn, R fail, F&& f) noexcept {
    try {
        return f();
    } catch (const std::bad_alloc&) {
        SetError(L"%ls：記憶體不足", fn);
    } catch (const std::exception& e) {
        std::wstring what = Widen(e.what());
        SetError(L"%ls：內部錯誤（%ls）", fn, what.c_str());
    } catch (...) {
        SetError(L"%ls：未知的內部錯誤", fn);
    }
    return fail;
}

Context* Get(void* h, const wchar_t* fn) {
    auto* c = static_cast<Context*>(h);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!c || !g_live.count(c) || c->magic != kContextMagic) {
        SetError(L"%ls：無效的 handle", fn);
        return nullptr;
    }
    return c;
}

// DPI of the window, loaded dynamically (GetDpiForWindow is Win10 1607+); 1.0 when unavailable.
float DpiScale(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = [] {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        void* p = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
        return reinterpret_cast<Fn>(p);
    }();
    UINT dpi = fn && hwnd ? fn(hwnd) : 0;
    return dpi ? dpi / 96.0f : 1.0f;
}

float DefaultRadius(const Context& c) { return (c.glass.Style() == kGlassWin11 ? 8.0f : 22.0f) * c.scale; }

// Default corner radius of a sector: a little softer than a menu row.
float DefaultSectorCorner(const Context& c) { return (c.glass.Style() == kGlassWin11 ? 6.0f : 14.0f) * c.scale; }

void RebuildShape(Context& c) {
    const float m = static_cast<float>(c.margin);
    const float* p = c.shapeParam;
    const float cx = m + c.panelW / 2.0f, cy = m + c.panelH / 2.0f;
    const float outer = std::min(c.panelW, c.panelH) / 2.0f;
    switch (c.shapeKind) {
        case kShapeCircle:
            c.shape = std::make_unique<RingShape>(cx, cy, outer, 0.0f);
            break;
        case kShapeRing:
            c.shape = std::make_unique<RingShape>(cx, cy, outer, p[0]);
            break;
        case kShapeSector:
            c.shape = std::make_unique<SectorShape>(m + p[0], m + p[1], p[2], p[3], p[4], p[5],
                                                    p[6] < 0 ? DefaultSectorCorner(c) : p[6]);
            break;
        default: {
            float radius = p[0] < 0 ? DefaultRadius(c) : p[0];
            c.shape = std::make_unique<RoundRectShape>(m, m, static_cast<float>(c.panelW),
                                                       static_cast<float>(c.panelH), radius);
        }
    }
    c.glass.Invalidate();
    c.baseValid = false;
}

// Recomputes margin / window size from panel size and scale.
void Layout(Context& c) {
    c.margin = static_cast<int>(std::ceil(40 * c.scale));
    int w = c.panelW + 2 * c.margin, h = c.panelH + 2 * c.margin;
    if (w != c.winW || h != c.winH) {
        c.winW = w;
        c.winH = h;
        c.content.assign(static_cast<size_t>(w) * h, 0);
    }
    RebuildShape(c);
}

void ReleaseDib(Context& c) {
    if (c.dc) {
        if (c.oldBitmap) SelectObject(c.dc, c.oldBitmap);
        DeleteDC(c.dc);
    }
    if (c.dib) DeleteObject(c.dib);
    c.dc = nullptr;
    c.dib = nullptr;
    c.oldBitmap = nullptr;
    c.bits = nullptr;
    c.dibW = c.dibH = 0;
}

bool EnsureDib(Context& c) {
    if (c.dib && c.dibW == c.winW && c.dibH == c.winH) return true;
    ReleaseDib(c);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = c.winW;
    bi.bmiHeader.biHeight = -c.winH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    c.dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!c.dib) {
        SetError(L"無法建立 %d×%d 的繪圖緩衝區（錯誤 %lu）", c.winW, c.winH, GetLastError());
        return false;
    }
    c.dc = CreateCompatibleDC(nullptr);
    if (!c.dc) {
        DeleteObject(c.dib);
        c.dib = nullptr;
        SetError(L"無法建立繪圖 DC（錯誤 %lu）", GetLastError());
        return false;
    }
    c.oldBitmap = SelectObject(c.dc, c.dib);
    c.bits = static_cast<DWORD*>(bits);
    c.dibW = c.winW;
    c.dibH = c.winH;
    return true;
}

// Premultiplied "over": dst = src + dst * (1 - src.a)
inline DWORD Over(DWORD src, DWORD dst) {
    DWORD a = src >> 24;
    if (a == 0) return dst;
    if (a == 255) return src;
    DWORD k = 255 - a;
    DWORD rb = (dst & 0x00FF00FF) * k, ag = ((dst >> 8) & 0x00FF00FF) * k;
    rb = ((rb + 0x00800080 + ((rb >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
    ag = (ag + 0x00800080 + ((ag >> 8) & 0x00FF00FF)) & 0xFF00FF00;
    return src + (rb | ag);
}

// Glass layer (cached while at rest) + content layer, both shifted by (dx, dy) while the
// background stays fixed.
void Compose(Context& c, int dx, int dy) {
    const size_t count = static_cast<size_t>(c.winW) * c.winH;
    const bool rest = dx == 0 && dy == 0;
    if (rest && c.baseValid && c.base.size() == count && c.basePos.x == c.pos.x && c.basePos.y == c.pos.y) {
        std::memcpy(c.bits, c.base.data(), count * sizeof(DWORD));
    } else {
        c.glass.Prepare(c.winW, c.winH, *c.shape);
        c.glass.Render(c.bits, c.pos, dy, dx);
        if (rest) {
            c.base.assign(c.bits, c.bits + count);
            c.baseValid = true;
            c.basePos = c.pos;
        }
    }
    if (c.content.size() != count) return;
    for (int y = 0; y < c.winH; ++y) {
        int sy = y - dy;
        if (sy < 0 || sy >= c.winH) continue;
        DWORD* dst = c.bits + static_cast<size_t>(y) * c.winW;
        const DWORD* src = c.content.data() + static_cast<size_t>(sy) * c.winW;
        int x0 = std::max(0, dx), x1 = std::min(c.winW, c.winW + dx);
        for (int x = x0; x < x1; ++x) {
            DWORD s = src[x - dx];
            if (s >> 24) dst[x] = Over(s, dst[x]);
        }
    }
}

bool Push(Context& c) {
    SIZE size = {c.winW, c.winH};
    POINT src = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, c.alpha, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(c.hwnd, nullptr, &c.pos, &size, c.dc, &src, 0, &blend, ULW_ALPHA)) {
        SetError(L"UpdateLayeredWindow 失敗（錯誤 %lu），視窗是否有 WS_EX_LAYERED 樣式？", GetLastError());
        return false;
    }
    return true;
}

void ShowNoActivate(HWND hwnd) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ---------------------------------------------------------------- capture while shown
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

DWORD WindowsBuild() {
    using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static DWORD build = [] {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        void* p = ntdll ? reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        OSVERSIONINFOW vi = {sizeof(vi)};
        return p && reinterpret_cast<Fn>(p)(&vi) == 0 ? vi.dwBuildNumber : 0UL;
    }();
    return build;
}

// Turns on "exclude from capture" (Win10 2004+). Before 19041 the call succeeds but acts as
// WDA_MONITOR (captured as black), so it is rejected by build number.
bool ExcludeOn(Context& c) {
    if (c.excludeWorks < 0) return false;
    if (DWORD build = WindowsBuild(); build && build < 19041) {
        c.excludeWorks = -1;
        return false;
    }
    if (!SetWindowDisplayAffinity(c.hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        c.excludeWorks = -1;
        return false;
    }
    DwmFlush();  // exclusion takes effect after the compositor has run
    DwmFlush();
    return true;
}

// First time only: if a small block at the panel center captures exactly what we drew,
// exclusion is not working on this system.
bool ExcludeVerified(Context& c) {
    if (c.excludeWorks) return c.excludeWorks > 0;
    if (!c.bits || c.alpha != 255) return true;  // cannot tell yet
    int size = std::max(8, static_cast<int>(24 * c.scale));
    int x0 = c.margin + c.panelW / 2 - size / 2, y0 = c.margin + c.panelH / 2 - size / 2;
    if (x0 < 0 || y0 < 0 || x0 + size > c.dibW || y0 + size > c.dibH) return true;
    RECT r = {c.pos.x + x0, c.pos.y + y0, c.pos.x + x0 + size, c.pos.y + y0 + size};
    std::vector<DWORD> shot;
    if (!CaptureScreen(r, shot)) return true;
    int same = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if ((shot[static_cast<size_t>(y) * size + x] & 0xFFFFFF) ==
                (c.bits[static_cast<size_t>(y0 + y) * c.dibW + x0 + x] & 0xFFFFFF))
                ++same;
    c.excludeWorks = same * 10 >= size * size * 9 ? -1 : 1;
    return c.excludeWorks > 0;
}

bool Present(Context& c, int panelX, int panelY);

// Captures the background behind the panel's future position. When the panel is on screen,
// it is excluded from the capture (WDA_EXCLUDEFROMCAPTURE, only during the capture so normal
// screenshots still include it); on older systems it is hidden for a moment instead.
bool CaptureAt(Context& c, int panelX, int panelY) {
    POINT wp = {panelX - c.margin, panelY - c.margin};
    RECT window = {wp.x, wp.y, wp.x + c.winW, wp.y + c.winH};
    const bool visible = IsWindowVisible(c.hwnd) != FALSE;
    bool done = false;
    if (visible && ExcludeOn(c)) {
        // the overlap check needs the window where we last drew it
        bool overlap = c.pos.x == wp.x && c.pos.y == wp.y;
        if (!overlap || ExcludeVerified(c)) {
            c.glass.Capture(window, c.scale);
            done = true;
        }
        SetWindowDisplayAffinity(c.hwnd, WDA_NONE);
    }
    bool hidden = false;
    if (!done) {
        if (visible) {
            ShowWindow(c.hwnd, SW_HIDE);
            DwmFlush();
            hidden = true;
        }
        c.glass.Capture(window, c.scale);
    }
    c.captured = true;
    c.capturePos = wp;
    c.baseValid = false;
    // Still on screen at the same place: show the new background right away.
    if (visible && c.pos.x == wp.x && c.pos.y == wp.y && c.alpha == 255) return Present(c, panelX, panelY);
    if (hidden) ShowNoActivate(c.hwnd);
    return true;
}

// ---------------------------------------------------------------- animation
double NowMs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

bool AnimationsEnabled() {
    BOOL on = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) return true;
    return on != FALSE;
}

// Overshoots a little and settles back, like a liquid (same curve as the original popup).
double EaseOutBack(double t) {
    const double c1 = 1.1, c3 = c1 + 1;
    double u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

double EaseOutCubic(double t) {
    double u = 1 - t;
    return 1 - u * u * u;
}

bool PushAlphaOnly(Context& c) {
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, c.alpha, AC_SRC_ALPHA};
    return UpdateLayeredWindow(c.hwnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA) != FALSE;
}

// ---- Animation state machine: AnimBegin sets up, AnimStep draws the frame for "now".
// AHK can drive AnimStep from a timer (non-blocking, the menu stays interactive), and
// LG_Animate simply loops it with DwmFlush (blocking).
//   kind 1: slide down into place + fade in. Material and content move together; the
//           background is sampled at fixed screen coordinates, so it stays put.
//   kind 2: fade out, then hide. Only the window alpha changes.
//   kind 3: grow from the panel center (0.92 -> 1) + fade in. The finished frame is scaled
//           as a whole, so the background inside zooms slightly until it ends exact.
// Step returns 1 = more frames, 2 = finished, 0 = error.

void FinishShown(Context& c) {
    c.anim.kind = 0;
    c.anim.rest.clear();
    c.alpha = 255;
}

int AnimStep(Context& c) {
    AnimState& a = c.anim;
    if (!a.kind) return 2;
    double t = a.ms > 0 ? std::min(1.0, (NowMs() - a.start) / a.ms) : 1.0;
    ++a.frames;
    if (a.kind == 2) {
        c.alpha = static_cast<BYTE>(std::lround(a.fromAlpha * (1 - t * t)));  // ease-in: lingers, then goes
        if (t >= 1) {
            ShowWindow(c.hwnd, SW_HIDE);
            a.kind = 0;
            c.alpha = 255;
            return 2;
        }
        if (!PushAlphaOnly(c)) {
            SetError(L"LG_AnimStep：UpdateLayeredWindow 失敗（錯誤 %lu）", GetLastError());
            return 0;
        }
        return 1;
    }
    if (a.kind == 1) {
        int dy = static_cast<int>(std::lround(-(1 - EaseOutBack(t)) * 14 * c.scale));
        c.alpha = static_cast<BYTE>(std::lround(255 * std::min(1.0, t * 2.5)));
        if (t >= 1) dy = 0, c.alpha = 255;
        Compose(c, 0, dy);
    } else {  // kind 3
        const size_t count = static_cast<size_t>(c.winW) * c.winH;
        if (t >= 1 || a.rest.size() != count) {
            t = 1;
            Compose(c, 0, 0);  // content may have changed during the animation
            c.alpha = 255;
        } else {
            float s = static_cast<float>(0.92 + 0.08 * EaseOutCubic(t));
            Gdiplus::Bitmap src(c.winW, c.winH, c.winW * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(a.rest.data()));
            Gdiplus::Bitmap dst(c.winW, c.winH, c.winW * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(c.bits));
            Gdiplus::Graphics g(&dst);
            g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            float cx, cy;
            c.shape->Origin(cx, cy);  // grows from the shape's center (a sector: its apex)
            std::fill(c.bits, c.bits + count, 0);
            g.DrawImage(&src, Gdiplus::RectF(cx - cx * s, cy - cy * s, c.winW * s, c.winH * s));
            c.alpha = static_cast<BYTE>(std::lround(255 * std::min(1.0, t * 2)));
        }
    }
    if (!Push(c)) return 0;
    if (!a.shown) {
        ShowNoActivate(c.hwnd);
        a.shown = true;
    }
    if (t >= 1) {
        FinishShown(c);
        return 2;
    }
    return 1;
}

// Returns 1 = started (call AnimStep), 2 = already finished (no animation needed), 0 = error.
int AnimBegin(Context& c, int kind, int ms) {
    if (kind < 1 || kind > 3) {
        SetError(L"LG_AnimBegin：未知的動畫種類 %d（1 = 滑入，2 = 淡出，3 = 縮放）", kind);
        return 0;
    }
    ms = std::clamp(ms, 0, 5000);
    const bool on = ms > 0 && AnimationsEnabled();
    AnimState& a = c.anim;
    a = AnimState{};
    if (kind == 2) {
        if (!IsWindowVisible(c.hwnd)) return 2;
        if (!on) {
            ShowWindow(c.hwnd, SW_HIDE);
            c.alpha = 255;
            return 2;
        }
        a.kind = 2;
        a.ms = ms;
        a.fromAlpha = c.alpha;
        a.start = NowMs();
        return 1;
    }
    if (!c.captured) {
        SetError(L"LG_AnimBegin：尚未截取背景，請先呼叫 LG_Capture");
        return 0;
    }
    c.pos = c.capturePos;  // shows where the background was captured
    if (!EnsureDib(c)) return 0;
    if (!on) {
        c.alpha = 255;
        Compose(c, 0, 0);
        if (!Push(c)) return 0;
        ShowNoActivate(c.hwnd);
        return 2;
    }
    a.kind = kind;
    a.ms = ms;
    if (kind == 3) {
        c.alpha = 255;
        Compose(c, 0, 0);
        a.rest.assign(c.bits, c.bits + static_cast<size_t>(c.winW) * c.winH);
    }
    a.start = NowMs();
    return 1;
}

// Blocking; returns the number of frames drawn (0 = failure).
int Animate(Context& c, int kind, int ms) {
    int r = AnimBegin(c, kind, ms);
    if (r != 1) return r ? 1 : 0;
    for (;;) {
        r = AnimStep(c);
        if (r != 1) break;
        DwmFlush();  // one frame per compositor refresh
    }
    return r == 2 ? std::max(1, c.anim.frames) : 0;
}

bool Present(Context& c, int panelX, int panelY) {
    c.anim = AnimState{};  // an immediate present ends any running animation
    if (!c.captured) CaptureAt(c, panelX, panelY);
    c.pos = {panelX - c.margin, panelY - c.margin};
    if (!EnsureDib(c)) return false;
    Compose(c, 0, 0);
    if (!Push(c)) return false;
    ShowNoActivate(c.hwnd);
    return true;
}

}  // namespace

void SetError(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vswprintf(buf, sizeof(buf) / sizeof(buf[0]), fmt, ap);
    va_end(ap);
    if (n < 0) buf[sizeof(buf) / sizeof(buf[0]) - 1] = L'\0';
    try {
        t_error = buf;
    } catch (...) {
    }
}

// GDI+ is started on the first LG_Create, never in DllMain (loader lock).
bool EnsureGdiplus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_gdipStarted) return true;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&g_gdipToken, &input, nullptr);
    if (st != Gdiplus::Ok) {
        SetError(L"GDI+ 啟動失敗（狀態 %d）", static_cast<int>(st));
        return false;
    }
    g_gdipStarted = true;
    return true;
}

}  // namespace lg

using namespace lg;

extern "C" {

// flags: bit0 = light, bit1 = Win11 acrylic style
void* LG_Create(HWND hwnd, int panelW, int panelH, int flags) {
    return Guard<void*>(L"LG_Create", nullptr, [&]() -> void* {
        if (!hwnd || !IsWindow(hwnd)) {
            SetError(L"LG_Create：hwnd 不是有效的視窗");
            return nullptr;
        }
        if (panelW <= 0 || panelH <= 0 || panelW > 16384 || panelH > 16384) {
            SetError(L"LG_Create：面板大小 %d×%d 不合理", panelW, panelH);
            return nullptr;
        }
        if (!EnsureGdiplus()) return nullptr;
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);

        auto c = std::make_unique<Context>();
        c->hwnd = hwnd;
        c->panelW = panelW;
        c->panelH = panelH;
        c->scale = DpiScale(hwnd);
        c->glass.SetScale(c->scale);
        c->glass.SetLight((flags & 1) != 0);
        c->glass.SetStyle((flags & 2) ? kGlassWin11 : kGlassLiquid);
        Layout(*c);
        Context* raw = c.release();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_live.insert(raw);
        return raw;
    });
}

int LG_Destroy(void* h) {
    return Guard(L"LG_Destroy", 0, [&] {
        Context* c = Get(h, L"LG_Destroy");
        if (!c) return 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_live.erase(c);
        }
        ReleaseDib(*c);
        c->magic = 0;
        delete c;
        return 1;
    });
}

int LG_Margin(void* h) {
    return Guard(L"LG_Margin", 0, [&] {
        Context* c = Get(h, L"LG_Margin");
        return c ? c->margin : 0;
    });
}

float LG_Scale(void* h) {
    return Guard(L"LG_Scale", 0.0f, [&] {
        Context* c = Get(h, L"LG_Scale");
        return c ? c->scale : 0.0f;
    });
}

// Overrides the DPI scale used for the shadow margin and effect sizes (1.0 = 96 dpi).
// The window size changes with the margin; capture again afterwards.
int LG_SetScale(void* h, float scale) {
    return Guard(L"LG_SetScale", 0, [&] {
        Context* c = Get(h, L"LG_SetScale");
        if (!c) return 0;
        if (!(scale >= 0.5f && scale <= 4.0f)) {
            SetError(L"LG_SetScale：倍率 %.2f 超出範圍（0.5 – 4）", static_cast<double>(scale));
            return 0;
        }
        if (scale == c->scale) return 1;
        c->scale = scale;
        c->glass.SetScale(scale);
        Layout(*c);
        c->captured = false;
        return 1;
    });
}

// kind 0 = round rect (p1 = corner radius in px, < 0 = style default)
//      1 = circle inscribed in the panel
//      2 = ring inscribed in the panel (p1 = inner radius in px)
//      3 = sector: use LG_SetSector
int LG_SetShape(void* h, int kind, float p1, float p2, float p3, float p4) {
    return Guard(L"LG_SetShape", 0, [&] {
        Context* c = Get(h, L"LG_SetShape");
        if (!c) return 0;
        if (kind < kShapeRoundRect || kind > kShapeRing) {
            SetError(L"LG_SetShape：未知的形狀 %d（0 = 圓角矩形，1 = 圓形，2 = 圓環；扇形請用 LG_SetSector）", kind);
            return 0;
        }
        c->shapeKind = kind;
        std::fill(std::begin(c->shapeParam), std::end(c->shapeParam), 0.0f);
        c->shapeParam[0] = p1;
        c->shapeParam[1] = p2;
        c->shapeParam[2] = p3;
        c->shapeParam[3] = p4;
        RebuildShape(*c);
        return 1;
    });
}

// Annular sector in panel px: center (cx, cy), radii outer / inner (0 = pie slice),
// startDeg (0 = 12 o'clock, clockwise) and sweepDeg (360 = full ring), corner radius
// (< 0 = style default). The sector may extend past the panel's edges into the margin.
int LG_SetSector(void* h, float cx, float cy, float outer, float inner, float startDeg, float sweepDeg,
                 float corner) {
    return Guard(L"LG_SetSector", 0, [&] {
        Context* c = Get(h, L"LG_SetSector");
        if (!c) return 0;
        if (!(outer > 0) || !(inner >= 0) || inner >= outer || !(sweepDeg > 0)) {
            SetError(L"LG_SetSector：參數不正確（外徑 %g、內徑 %g、角度 %g）", outer, inner, sweepDeg);
            return 0;
        }
        c->shapeKind = kShapeSector;
        float p[8] = {cx, cy, outer, inner, startDeg, sweepDeg, corner, 0};
        std::copy(std::begin(p), std::end(p), std::begin(c->shapeParam));
        RebuildShape(*c);
        return 1;
    });
}

int LG_SetLight(void* h, int light) {
    return Guard(L"LG_SetLight", 0, [&] {
        Context* c = Get(h, L"LG_SetLight");
        if (!c) return 0;
        c->glass.SetLight(light != 0);
        c->baseValid = false;
        return 1;
    });
}

// 0 = liquid glass, 1 = Win11 acrylic
int LG_SetStyle(void* h, int style) {
    return Guard(L"LG_SetStyle", 0, [&] {
        Context* c = Get(h, L"LG_SetStyle");
        if (!c) return 0;
        if (style != kGlassLiquid && style != kGlassWin11) {
            SetError(L"LG_SetStyle：未知的風格 %d（0 = 液態玻璃，1 = Acrylic）", style);
            return 0;
        }
        if (style == c->glass.Style()) return 1;
        c->glass.SetStyle(style);
        c->glass.Reprocess();  // blur strength depends on the style
        RebuildShape(*c);      // default radius depends on the style
        return 1;
    });
}

// rgb = 0xRRGGBB; custom = 0 restores the default tint
int LG_SetTint(void* h, int custom, UINT rgb) {
    return Guard(L"LG_SetTint", 0, [&] {
        Context* c = Get(h, L"LG_SetTint");
        if (!c) return 0;
        c->glass.SetTint(custom != 0, RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
        c->baseValid = false;
        return 1;
    });
}

int LG_SetTransparency(void* h, int pct) {
    return Guard(L"LG_SetTransparency", 0, [&] {
        Context* c = Get(h, L"LG_SetTransparency");
        if (!c) return 0;
        c->glass.SetTransparency(std::clamp(pct, 0, 100));
        c->baseValid = false;
        return 1;
    });
}

int LG_Resize(void* h, int panelW, int panelH) {
    return Guard(L"LG_Resize", 0, [&] {
        Context* c = Get(h, L"LG_Resize");
        if (!c) return 0;
        if (panelW <= 0 || panelH <= 0 || panelW > 16384 || panelH > 16384) {
            SetError(L"LG_Resize：面板大小 %d×%d 不合理", panelW, panelH);
            return 0;
        }
        if (panelW == c->panelW && panelH == c->panelH) return 1;
        c->panelW = panelW;
        c->panelH = panelH;
        Layout(*c);
        c->captured = false;  // the capture area no longer covers the window
        return 1;
    });
}

// Captures and processes the background where the panel (top-left = panelX, panelY) will appear.
int LG_Capture(void* h, int panelX, int panelY) {
    return Guard(L"LG_Capture", 0, [&] {
        Context* c = Get(h, L"LG_Capture");
        if (!c) return 0;
        return CaptureAt(*c, panelX, panelY) ? 1 : 0;
    });
}

// Composes glass + content and shows the panel with its top-left at (panelX, panelY), no animation.
int LG_Present(void* h, int panelX, int panelY) {
    return Guard(L"LG_Present", 0, [&] {
        Context* c = Get(h, L"LG_Present");
        if (!c) return 0;
        c->alpha = 255;
        return Present(*c, panelX, panelY) ? 1 : 0;
    });
}

// kind 1 = slide in + fade in, 2 = fade out (then hide), 3 = zoom from center.
// 1 / 3 show the panel where LG_Capture was last called. Blocking; returns frames drawn.
int LG_Animate(void* h, int kind, int ms) {
    return Guard(L"LG_Animate", 0, [&] {
        Context* c = Get(h, L"LG_Animate");
        return c ? Animate(*c, kind, ms) : 0;
    });
}

// Re-composes at the current position without recapturing.
int LG_Update(void* h) {
    return Guard(L"LG_Update", 0, [&] {
        Context* c = Get(h, L"LG_Update");
        if (!c) return 0;
        if (!c->captured) {
            SetError(L"LG_Update：尚未截取背景，請先呼叫 LG_Capture 或 LG_Present");
            return 0;
        }
        // Showing animation running: the next LG_AnimStep picks up the new content.
        if (c->anim.kind == 1 || c->anim.kind == 3) return 1;
        if (!EnsureDib(*c)) return 0;
        Compose(*c, 0, 0);
        return Push(*c) ? 1 : 0;
    });
}

// Non-blocking animation: LG_AnimBegin, then LG_AnimStep from a timer until it returns 2.
// Begin: 1 = started, 2 = nothing to animate (already in the final state), 0 = error.
int LG_AnimBegin(void* h, int kind, int ms) {
    return Guard(L"LG_AnimBegin", 0, [&] {
        Context* c = Get(h, L"LG_AnimBegin");
        return c ? AnimBegin(*c, kind, ms) : 0;
    });
}

// Draws the frame for the current time: 1 = more frames, 2 = finished, 0 = error.
int LG_AnimStep(void* h) {
    return Guard(L"LG_AnimStep", 0, [&] {
        Context* c = Get(h, L"LG_AnimStep");
        return c ? AnimStep(*c) : 0;
    });
}

// Width in px of single-line text (same font handling as LG_Text; flags 0x01 = bold). -1 on error.
int LG_MeasureText(const wchar_t* text, const wchar_t* font, int sizePx, int flags) {
    return Guard(L"LG_MeasureText", -1, [&] {
        if (!EnsureGdiplus()) return -1;
        float w = CanvasMeasureText(text, font, static_cast<float>(sizePx), flags);
        return w < 0 ? -1 : static_cast<int>(std::ceil(w));
    });
}

// Average luma (0 - 255) of the tinted glass inside a panel-relative rect; -1 on error.
float LG_BaseLuma(void* h, int x, int y, int w, int hgt) {
    return Guard(L"LG_BaseLuma", -1.0f, [&] {
        Context* c = Get(h, L"LG_BaseLuma");
        if (!c) return -1.0f;
        POINT origin = c->captured ? c->capturePos : c->pos;
        return c->glass.BaseLuma(static_cast<float>(x + c->margin), static_cast<float>(y + c->margin),
                                 static_cast<float>(w), static_cast<float>(hgt), origin);
    });
}

// ---------------------------------------------------------------- content layer
// Coordinates are panel-relative physical pixels; colors are 0xAARRGGBB.
// Drawing only changes the content layer: call LG_Update / LG_Present to show it.

int LG_Clear(void* h) {
    return Guard(L"LG_Clear", 0, [&] {
        Context* c = Get(h, L"LG_Clear");
        return c && CanvasClear(*c) ? 1 : 0;
    });
}

int LG_FillRoundRect(void* h, int x, int y, int w, int hgt, int r, UINT argb) {
    return Guard(L"LG_FillRoundRect", 0, [&] {
        Context* c = Get(h, L"LG_FillRoundRect");
        return c && CanvasFillRoundRect(*c, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                        static_cast<float>(hgt), static_cast<float>(r), argb)
                   ? 1
                   : 0;
    });
}

// Filled annular sector on the content layer (panel px, same parameters as LG_SetSector;
// sweep 360 = ring or disc). Antialiased from the same distance field as the glass.
int LG_FillSector(void* h, float cx, float cy, float outer, float inner, float startDeg, float sweepDeg,
                  float corner, UINT argb) {
    return Guard(L"LG_FillSector", 0, [&] {
        Context* c = Get(h, L"LG_FillSector");
        if (!c) return 0;
        if (!(outer > 0) || !(sweepDeg > 0)) return 1;
        const float m = static_cast<float>(c->margin);
        SectorShape shape(m + cx, m + cy, outer, std::clamp(inner, 0.0f, outer), startDeg, sweepDeg,
                          std::max(0.0f, corner));
        return CanvasFillShape(*c, shape, argb) ? 1 : 0;
    });
}

int LG_Line(void* h, int x1, int y1, int x2, int y2, int width, UINT argb) {
    return Guard(L"LG_Line", 0, [&] {
        Context* c = Get(h, L"LG_Line");
        return c && CanvasLine(*c, static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2),
                               static_cast<float>(y2), static_cast<float>(width), argb)
                   ? 1
                   : 0;
    });
}

// flags: 0x01 bold, 0x02 center, 0x04 right, 0x08 vcenter, 0x10 ellipsis, 0x20 no wrap, 0x40 bottom
int LG_Text(void* h, const wchar_t* text, int x, int y, int w, int hgt, const wchar_t* font, int sizePx, int flags,
            UINT argb) {
    return Guard(L"LG_Text", 0, [&] {
        Context* c = Get(h, L"LG_Text");
        return c && CanvasText(*c, text, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                               static_cast<float>(hgt), font, static_cast<float>(sizePx), flags, argb)
                   ? 1
                   : 0;
    });
}

int LG_Icon(void* h, HICON icon, int x, int y, int size) {
    return Guard(L"LG_Icon", 0, [&] {
        Context* c = Get(h, L"LG_Icon");
        return c && CanvasIcon(*c, icon, static_cast<float>(x), static_cast<float>(y), static_cast<float>(size)) ? 1 : 0;
    });
}

// index follows AHK: 1 = first icon, negative = resource ID
int LG_IconFile(void* h, const wchar_t* path, int index, int x, int y, int size) {
    return Guard(L"LG_IconFile", 0, [&] {
        Context* c = Get(h, L"LG_IconFile");
        return c && CanvasIconFile(*c, path, index, static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(size))
                   ? 1
                   : 0;
    });
}

// w / h <= 0 keeps the aspect ratio (both <= 0 = natural size)
int LG_Image(void* h, const wchar_t* path, int x, int y, int w, int hgt) {
    return Guard(L"LG_Image", 0, [&] {
        Context* c = Get(h, L"LG_Image");
        return c && CanvasImage(*c, path, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                static_cast<float>(hgt))
                   ? 1
                   : 0;
    });
}

const wchar_t* LG_LastError() { return t_error.c_str(); }

}  // extern "C"
