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

void RebuildShape(Context& c) {
    float radius = c.shapeParam[0] < 0 ? DefaultRadius(c) : c.shapeParam[0];
    c.shape = std::make_unique<RoundRectShape>(static_cast<float>(c.margin), static_cast<float>(c.margin),
                                               static_cast<float>(c.panelW), static_cast<float>(c.panelH), radius);
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

bool CaptureAt(Context& c, int panelX, int panelY) {
    POINT wp = {panelX - c.margin, panelY - c.margin};
    RECT window = {wp.x, wp.y, wp.x + c.winW, wp.y + c.winH};
    // v1: if the panel is on screen, hide it first so it is not captured.
    // (Stage 3 replaces this with WDA_EXCLUDEFROMCAPTURE.)
    if (IsWindowVisible(c.hwnd)) {
        ShowWindow(c.hwnd, SW_HIDE);
        DwmFlush();
    }
    c.glass.Capture(window, c.scale);
    c.captured = true;
    c.capturePos = wp;
    c.baseValid = false;
    return true;
}

bool Present(Context& c, int panelX, int panelY) {
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

// kind 0 = round rect (p1 = corner radius in px, < 0 = style default). 1 - 3 reserved.
int LG_SetShape(void* h, int kind, float p1, float p2, float p3, float p4) {
    return Guard(L"LG_SetShape", 0, [&] {
        Context* c = Get(h, L"LG_SetShape");
        if (!c) return 0;
        if (kind != kShapeRoundRect) {
            SetError(L"LG_SetShape：形狀 %d 尚未支援（目前只有 0 = 圓角矩形）", kind);
            return 0;
        }
        c->shapeKind = kind;
        c->shapeParam[0] = p1;
        c->shapeParam[1] = p2;
        c->shapeParam[2] = p3;
        c->shapeParam[3] = p4;
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

// Re-composes at the current position without recapturing.
int LG_Update(void* h) {
    return Guard(L"LG_Update", 0, [&] {
        Context* c = Get(h, L"LG_Update");
        if (!c) return 0;
        if (!c->captured) {
            SetError(L"LG_Update：尚未截取背景，請先呼叫 LG_Capture 或 LG_Present");
            return 0;
        }
        if (!EnsureDib(*c)) return 0;
        Compose(*c, 0, 0);
        return Push(*c) ? 1 : 0;
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

const wchar_t* LG_LastError() { return t_error.c_str(); }

}  // extern "C"
