// LiquidGlass - content layer: text, icons, images, round rect fills and lines (GDI+).
// Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
//
// The content layer is a window-size premultiplied ARGB buffer (Context::content). Drawing
// coordinates are relative to the panel's top-left; the shadow margin is added here.
// Text uses TextRenderingHintAntiAliasGridFit: ClearType breaks on a transparent background.
#include "context.h"

#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>
#include <mutex>
#include <tuple>

using namespace Gdiplus;

namespace lg {
namespace {

// ---------------------------------------------------------------- graphics on the content layer
struct ContentGraphics {
    std::unique_ptr<Bitmap> bmp;
    std::unique_ptr<Graphics> g;
};

bool OpenContent(Context& c, ContentGraphics& out, const wchar_t* fn) {
    const size_t count = static_cast<size_t>(c.winW) * c.winH;
    if (c.content.size() != count) c.content.assign(count, 0);
    out.bmp = std::make_unique<Bitmap>(c.winW, c.winH, c.winW * 4, PixelFormat32bppPARGB,
                                       reinterpret_cast<BYTE*>(c.content.data()));
    if (out.bmp->GetLastStatus() != Ok) {
        SetError(L"%ls：無法建立內容層畫布", fn);
        return false;
    }
    out.g = std::make_unique<Graphics>(out.bmp.get());
    if (out.g->GetLastStatus() != Ok) {
        SetError(L"%ls：無法建立 GDI+ Graphics", fn);
        return false;
    }
    out.g->SetSmoothingMode(SmoothingModeAntiAlias);
    out.g->SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    out.g->SetPixelOffsetMode(PixelOffsetModeHalf);
    out.g->SetInterpolationMode(InterpolationModeHighQualityBicubic);
    out.g->SetCompositingQuality(CompositingQualityHighQuality);
    out.g->TranslateTransform(static_cast<REAL>(c.margin), static_cast<REAL>(c.margin));
    return true;
}

void AddRoundRect(GraphicsPath& path, const RectF& r, float radius) {
    float d = std::min({radius * 2, r.Width, r.Height});
    if (d <= 0) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

// ---------------------------------------------------------------- icons
// Converts an HICON to a 32-bit ARGB bitmap, keeping its alpha channel. Icons without alpha
// take their transparency from the AND mask; monochrome icons fall back to Bitmap::FromHICON.
std::unique_ptr<Bitmap> IconToBitmap(HICON icon) {
    ICONINFO info = {};
    if (!GetIconInfo(icon, &info)) return nullptr;
    std::unique_ptr<Bitmap> result;
    BITMAP bm = {};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        const int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        std::vector<DWORD> px(static_cast<size_t>(w) * h);
        HDC dc = GetDC(nullptr);
        bool ok = GetDIBits(dc, info.hbmColor, 0, h, px.data(), &bi, DIB_RGB_COLORS) == h;
        bool hasAlpha = false;
        if (ok) {
            for (DWORD p : px)
                if (p >> 24) {
                    hasAlpha = true;
                    break;
                }
            if (!hasAlpha && info.hbmMask) {  // no alpha: transparency from the mask (1 = transparent)
                std::vector<DWORD> mask(px.size());
                if (GetDIBits(dc, info.hbmMask, 0, h, mask.data(), &bi, DIB_RGB_COLORS) == h) {
                    for (size_t i = 0; i < px.size(); ++i) px[i] = (mask[i] & 0xFFFFFF) ? 0 : (px[i] | 0xFF000000);
                } else {
                    for (DWORD& p : px) p |= 0xFF000000;
                }
            }
        }
        ReleaseDC(nullptr, dc);
        if (ok) {
            // Icon pixels are straight (non-premultiplied) alpha.
            result = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
            BitmapData data;
            Rect rect(0, 0, w, h);
            if (result->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppARGB, &data) == Ok) {
                for (int y = 0; y < h; ++y)
                    std::memcpy(static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
                                &px[static_cast<size_t>(y) * w], static_cast<size_t>(w) * 4);
                result->UnlockBits(&data);
            } else {
                result.reset();
            }
        }
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    if (!result) {
        Bitmap* b = Bitmap::FromHICON(icon);
        if (b && b->GetLastStatus() == Ok) result.reset(b);
        else delete b;
    }
    return result;
}

bool DrawBitmap(Context& c, Image& img, float x, float y, float w, float h, const wchar_t* fn) {
    ContentGraphics cg;
    if (!OpenContent(c, cg, fn)) return false;
    Status st = cg.g->DrawImage(&img, RectF(x, y, w, h));
    if (st != Ok) {
        SetError(L"%ls：繪製失敗（GDI+ 狀態 %d）", fn, static_cast<int>(st));
        return false;
    }
    return true;
}

// Icon cache shared by all panels: (lower-case path, index, size) -> ARGB bitmap.
using IconKey = std::tuple<std::wstring, int, int>;
std::mutex g_cacheMutex;
std::map<IconKey, std::unique_ptr<Bitmap>> g_iconCache;
std::map<std::wstring, std::unique_ptr<Bitmap>> g_imageCache;
constexpr size_t kMaxCache = 512;

std::wstring Lower(const wchar_t* s) {
    std::wstring r(s ? s : L"");
    for (wchar_t& ch : r) ch = static_cast<wchar_t>(std::towlower(ch));
    return r;
}

// AHK convention: index 1 = first icon, negative = resource ID; 0 is treated as 1.
Bitmap* CachedIcon(const wchar_t* path, int index, int size) {
    IconKey key{Lower(path), index, size};
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_iconCache.find(key);
    if (it != g_iconCache.end()) return it->second.get();

    int nIndex = index > 0 ? index - 1 : (index == 0 ? 0 : index);
    // Bare names such as "imageres.dll" are looked up on the system path, like Menu.SetIcon does.
    wchar_t found[MAX_PATH];
    const wchar_t* use = path;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES &&
        SearchPathW(nullptr, path, nullptr, MAX_PATH, found, nullptr) > 0)
        use = found;
    HICON icon = nullptr;
    UINT id = 0;
    UINT got = PrivateExtractIconsW(use, nIndex, size, size, &icon, &id, 1, LR_DEFAULTCOLOR);
    if (got == 0 || got == 0xFFFFFFFF || !icon) {
        SetError(L"LG_IconFile：無法從「%ls」取得第 %d 個圖示", path, index);
        return nullptr;
    }
    std::unique_ptr<Bitmap> bmp = IconToBitmap(icon);
    DestroyIcon(icon);
    if (!bmp) {
        SetError(L"LG_IconFile：無法轉換「%ls」的圖示", path);
        return nullptr;
    }
    if (g_iconCache.size() >= kMaxCache) g_iconCache.clear();
    Bitmap* raw = bmp.get();
    g_iconCache.emplace(std::move(key), std::move(bmp));
    return raw;
}

// Loads an image into memory (no file lock), premultiplied for fast drawing.
Bitmap* CachedImage(const wchar_t* path) {
    std::wstring key = Lower(path);
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_imageCache.find(key);
    if (it != g_imageCache.end()) return it->second.get();
    Bitmap file(path);
    if (file.GetLastStatus() != Ok || file.GetWidth() == 0 || file.GetHeight() == 0) {
        SetError(L"LG_Image：無法載入圖片「%ls」", path);
        return nullptr;
    }
    const int w = static_cast<int>(file.GetWidth()), h = static_cast<int>(file.GetHeight());
    auto copy = std::make_unique<Bitmap>(w, h, PixelFormat32bppPARGB);
    {
        Graphics g(copy.get());
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(&file, 0, 0, w, h);
    }
    if (g_imageCache.size() >= kMaxCache) g_imageCache.clear();
    Bitmap* raw = copy.get();
    g_imageCache.emplace(std::move(key), std::move(copy));
    return raw;
}

// Font for LG_Text / LG_MeasureText; falls back to the generic sans serif family.
std::unique_ptr<Font> MakeFont(const wchar_t* font, float sizePx, int flags, const wchar_t* fn) {
    const wchar_t* family = (font && *font) ? font : L"Microsoft JhengHei UI";
    FontFamily ff(family);
    std::unique_ptr<FontFamily> fallback;
    const FontFamily* use = &ff;
    if (ff.GetLastStatus() != Ok) {
        fallback.reset(FontFamily::GenericSansSerif()->Clone());
        use = fallback.get();
    }
    int style = (flags & kTextBold) ? FontStyleBold : FontStyleRegular;
    if (!use->IsStyleAvailable(style)) style = FontStyleRegular;
    auto f = std::make_unique<Font>(use, sizePx > 0 ? sizePx : 14.0f, style, UnitPixel);
    if (f->GetLastStatus() != Ok) {
        SetError(L"%ls：無法建立字型「%ls」", fn, family);
        return nullptr;
    }
    return f;
}

}  // namespace

float CanvasMeasureText(const wchar_t* text, const wchar_t* font, float sizePx, int flags) {
    if (!text || !*text) return 0;
    std::unique_ptr<Font> f = MakeFont(font, sizePx, flags, L"LG_MeasureText");
    if (!f) return -1;
    Bitmap bmp(1, 1, PixelFormat32bppPARGB);
    Graphics g(&bmp);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    StringFormat fmt;  // same default format as LG_Text, so widths match what is drawn
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF box;
    if (g.MeasureString(text, -1, f.get(), PointF(0, 0), &fmt, &box) != Ok) {
        SetError(L"LG_MeasureText：量測失敗");
        return -1;
    }
    return box.Width;
}

// ---------------------------------------------------------------- called from api.cpp exports
bool CanvasClear(Context& c) {
    c.content.assign(static_cast<size_t>(c.winW) * c.winH, 0);
    return true;
}

bool CanvasFillRoundRect(Context& c, float x, float y, float w, float h, float r, DWORD argb) {
    if (w <= 0 || h <= 0) return true;
    ContentGraphics cg;
    if (!OpenContent(c, cg, L"LG_FillRoundRect")) return false;
    SolidBrush brush{Color(static_cast<ARGB>(argb))};
    GraphicsPath path;
    AddRoundRect(path, RectF(x, y, w, h), r);
    cg.g->FillPath(&brush, &path);
    return true;
}

// Rasterizes any SDF shape (window coordinates) with antialiased coverage, source-over.
bool CanvasFillShape(Context& c, const Shape& shape, DWORD argb) {
    const size_t count = static_cast<size_t>(c.winW) * c.winH;
    if (c.content.size() != count) c.content.assign(count, 0);
    const DWORD a = argb >> 24;
    if (!a) return true;
    const DWORD r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    ShapeBounds bb = shape.Bounds();
    int x0 = std::max(0, static_cast<int>(std::floor(bb.x)) - 1);
    int y0 = std::max(0, static_cast<int>(std::floor(bb.y)) - 1);
    int x1 = std::min(c.winW, static_cast<int>(std::ceil(bb.x + bb.w)) + 1);
    int y1 = std::min(c.winH, static_cast<int>(std::ceil(bb.y + bb.h)) + 1);
    for (int y = y0; y < y1; ++y) {
        DWORD* row = c.content.data() + static_cast<size_t>(y) * c.winW;
        for (int x = x0; x < x1; ++x) {
            float cover = std::clamp(0.5f - shape.Distance(x + 0.5f, y + 0.5f), 0.0f, 1.0f);
            if (cover <= 0) continue;
            DWORD sa = static_cast<DWORD>(std::lround(a * cover));
            if (!sa) continue;
            // premultiplied source over premultiplied destination
            DWORD src = (sa << 24) | (((r * sa + 127) / 255) << 16) | (((g * sa + 127) / 255) << 8) | ((b * sa + 127) / 255);
            DWORD dst = row[x], k = 255 - sa;
            DWORD out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                DWORD s = (src >> sh) & 0xFF, d = (dst >> sh) & 0xFF;
                out |= std::min<DWORD>(255, s + (d * k + 127) / 255) << sh;
            }
            row[x] = out;
        }
    }
    return true;
}

bool CanvasLine(Context& c, float x1, float y1, float x2, float y2, float width, DWORD argb) {
    ContentGraphics cg;
    if (!OpenContent(c, cg, L"LG_Line")) return false;
    Pen pen(Color(static_cast<ARGB>(argb)), std::max(0.1f, width));
    // Axis-aligned lines with integer width stay crisp without antialiasing.
    bool straight = (x1 == x2 || y1 == y2) && std::fabs(width - std::round(width)) < 0.01f;
    if (straight) {
        cg.g->SetSmoothingMode(SmoothingModeNone);
        // pixel centers sit on integers in PixelOffsetModeHalf; odd widths are centered on the given row/column
    }
    cg.g->DrawLine(&pen, x1, y1, x2, y2);
    return true;
}

bool CanvasText(Context& c, const wchar_t* text, float x, float y, float w, float h, const wchar_t* font,
                float sizePx, int flags, DWORD argb) {
    if (!text || !*text || w <= 0 || h <= 0) return true;
    ContentGraphics cg;
    if (!OpenContent(c, cg, L"LG_Text")) return false;
    std::unique_ptr<Font> fp = MakeFont(font, sizePx, flags, L"LG_Text");
    if (!fp) return false;
    Font& f = *fp;
    StringFormat fmt;
    fmt.SetAlignment((flags & kTextCenter) ? StringAlignmentCenter
                                          : (flags & kTextRight) ? StringAlignmentFar : StringAlignmentNear);
    fmt.SetLineAlignment((flags & kTextVCenter) ? StringAlignmentCenter
                                                : (flags & kTextBottom) ? StringAlignmentFar : StringAlignmentNear);
    if (flags & kTextEllipsis) fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    INT ff2 = 0;
    if (flags & kTextNoWrap) ff2 |= StringFormatFlagsNoWrap;
    if (ff2) fmt.SetFormatFlags(ff2);
    SolidBrush brush{Color(static_cast<ARGB>(argb))};
    Status st = cg.g->DrawString(text, -1, &f, RectF(x, y, w, h), &fmt, &brush);
    if (st != Ok) {
        SetError(L"LG_Text：繪製文字失敗（GDI+ 狀態 %d）", static_cast<int>(st));
        return false;
    }
    return true;
}

bool CanvasIcon(Context& c, HICON icon, float x, float y, float size) {
    if (!icon) {
        SetError(L"LG_Icon：hIcon 是空的");
        return false;
    }
    std::unique_ptr<Bitmap> bmp = IconToBitmap(icon);
    if (!bmp) {
        SetError(L"LG_Icon：無法讀取圖示");
        return false;
    }
    return DrawBitmap(c, *bmp, x, y, size, size, L"LG_Icon");
}

bool CanvasIconFile(Context& c, const wchar_t* path, int index, float x, float y, float size) {
    if (!path || !*path) {
        SetError(L"LG_IconFile：路徑是空的");
        return false;
    }
    int px = std::max(1, static_cast<int>(std::lround(size)));
    Bitmap* bmp = CachedIcon(path, index, px);
    return bmp && DrawBitmap(c, *bmp, x, y, size, size, L"LG_IconFile");
}

bool CanvasImage(Context& c, const wchar_t* path, float x, float y, float w, float h) {
    if (!path || !*path) {
        SetError(L"LG_Image：路徑是空的");
        return false;
    }
    Bitmap* bmp = CachedImage(path);
    if (!bmp) return false;
    // w / h <= 0: natural size
    float iw = static_cast<float>(bmp->GetWidth()), ih = static_cast<float>(bmp->GetHeight());
    if (w <= 0 && h <= 0) {
        w = iw;
        h = ih;
    } else if (w <= 0) {
        w = h * iw / ih;
    } else if (h <= 0) {
        h = w * ih / iw;
    }
    return DrawBitmap(c, *bmp, x, y, w, h, L"LG_Image");
}

}  // namespace lg
