// LiquidGlass - glass material.
// Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
//
// Adapted from DM74164/TaskbarPopup src/glass.cpp and src/util.cpp
// (Copyright (C) 2026 DM74164, AGPL-3.0). Changes:
//   - moved into namespace lg, no dependency on the TaskbarPopup app headers
//   - the material is derived from an abstract Shape (SDF) instead of a fixed round rect
//   - Shoot/Extend replaced by Capture + Reprocess (re-blur on style change)
//   - BaseLuma takes a plain rect instead of Gdiplus::RectF
//
// Algorithm (from the original): capture the screen behind the panel, blur and saturate it.
// The signed distance to the edge drives a convex-lens refraction along the rim (R/G/B
// sampled with slightly different offsets for dispersion), a light tint, a top gloss, a
// rim highlight and a soft outer shadow. All shape-only quantities are computed once in
// Prepare; each frame only fetches the background, so the panel can slide while the
// background stays still. The Win11 style is a flat acrylic: stronger blur, dense
// system-gray tint, a bit of grain and a 1 px border.
#include "glass.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace lg {
namespace {

inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Horizontal box blur of one row, edge pixels repeat.
void BoxBlurRow(const DWORD* src, DWORD* dst, int n, int r) {
    const int window = 2 * r + 1, inv = (65536 + window / 2) / window;
    int sb = 0, sg = 0, sr = 0;
    for (int i = -r; i <= r; ++i) {
        DWORD p = src[std::clamp(i, 0, n - 1)];
        sb += p & 0xFF;
        sg += (p >> 8) & 0xFF;
        sr += (p >> 16) & 0xFF;
    }
    for (int i = 0; i < n; ++i) {
        dst[i] = 0xFF000000 | (((sr * inv + 0x8000) >> 16) << 16) | (((sg * inv + 0x8000) >> 16) << 8) |
                 ((sb * inv + 0x8000) >> 16);
        DWORD add = src[std::min(i + r + 1, n - 1)], sub = src[std::max(i - r, 0)];
        sb += static_cast<int>(add & 0xFF) - static_cast<int>(sub & 0xFF);
        sg += static_cast<int>((add >> 8) & 0xFF) - static_cast<int>((sub >> 8) & 0xFF);
        sr += static_cast<int>((add >> 16) & 0xFF) - static_cast<int>((sub >> 16) & 0xFF);
    }
}

// Vertical box blur of the whole image, one running sum per column, memory order access.
void BoxBlurColumns(const DWORD* src, DWORD* dst, int w, int h, int r, std::vector<int>& sums) {
    const int window = 2 * r + 1, inv = (65536 + window / 2) / window;
    sums.assign(static_cast<size_t>(w) * 3, 0);
    int* s = sums.data();
    auto row = [&](int y) { return src + static_cast<size_t>(std::clamp(y, 0, h - 1)) * w; };
    for (int i = -r; i <= r; ++i) {
        const DWORD* p = row(i);
        for (int x = 0; x < w; ++x) {
            s[3 * x] += p[x] & 0xFF;
            s[3 * x + 1] += (p[x] >> 8) & 0xFF;
            s[3 * x + 2] += (p[x] >> 16) & 0xFF;
        }
    }
    for (int y = 0; y < h; ++y) {
        DWORD* out = dst + static_cast<size_t>(y) * w;
        const DWORD* add = row(y + r + 1);
        const DWORD* sub = row(y - r);
        for (int x = 0; x < w; ++x) {
            int* c = s + 3 * x;
            out[x] = 0xFF000000 | (((c[2] * inv + 0x8000) >> 16) << 16) | (((c[1] * inv + 0x8000) >> 16) << 8) |
                     ((c[0] * inv + 0x8000) >> 16);
            c[0] += static_cast<int>(add[x] & 0xFF) - static_cast<int>(sub[x] & 0xFF);
            c[1] += static_cast<int>((add[x] >> 8) & 0xFF) - static_cast<int>((sub[x] >> 8) & 0xFF);
            c[2] += static_cast<int>((add[x] >> 16) & 0xFF) - static_cast<int>((sub[x] >> 16) & 0xFF);
        }
    }
}

// Three box blur passes in each direction approximate a gaussian.
void Blur(std::vector<DWORD>& px, int w, int h, int r) {
    std::vector<DWORD> tmp(px.size());
    std::vector<int> sums;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) BoxBlurRow(&px[static_cast<size_t>(y) * w], &tmp[static_cast<size_t>(y) * w], w, r);
        BoxBlurColumns(tmp.data(), px.data(), w, h, r, sums);
    }
}

// Half size: average of each 2x2 block, odd edges repeat.
std::vector<DWORD> HalfSize(const std::vector<DWORD>& px, int w, int h, int hw, int hh) {
    std::vector<DWORD> out(static_cast<size_t>(hw) * hh);
    for (int y = 0; y < hh; ++y) {
        const DWORD* r0 = &px[static_cast<size_t>(2 * y) * w];
        const DWORD* r1 = &px[static_cast<size_t>(std::min(2 * y + 1, h - 1)) * w];
        DWORD* o = &out[static_cast<size_t>(y) * hw];
        for (int x = 0; x < hw; ++x) {
            int x0 = 2 * x, x1 = std::min(2 * x + 1, w - 1);
            DWORD a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
            DWORD lo = ((a & 0xFF00FF) + (b & 0xFF00FF) + (c & 0xFF00FF) + (d & 0xFF00FF) + 0x020002) >> 2;
            DWORD mid = ((a & 0xFF00) + (b & 0xFF00) + (c & 0xFF00) + (d & 0xFF00) + 0x200) >> 2;
            o[x] = 0xFF000000 | (lo & 0xFF00FF) | (mid & 0xFF00);
        }
    }
    return out;
}

// Back to w x h with bilinear interpolation; half-size pixel centers sit at 2x + 0.5.
void DoubleSize(const std::vector<DWORD>& half, int hw, int hh, std::vector<DWORD>& out, int w, int h) {
    out.resize(static_cast<size_t>(w) * h);
    auto taps = [](int n, int hn, std::vector<int>& i0, std::vector<int>& i1) {
        i0.resize(n);
        i1.resize(n);
        for (int i = 0; i < n; ++i) {
            int k = std::min(i / 2, hn - 1);
            i1[i] = k;
            i0[i] = i % 2 == 0 ? std::max(k - 1, 0) : std::min(k + 1, hn - 1);
        }
    };
    std::vector<int> x0, x1, y0, y1;
    taps(w, hw, x0, x1);
    taps(h, hh, y0, y1);
    for (int y = 0; y < h; ++y) {
        const DWORD* ra = &half[static_cast<size_t>(y0[y]) * hw];
        const DWORD* rb = &half[static_cast<size_t>(y1[y]) * hw];
        DWORD* o = &out[static_cast<size_t>(y) * w];
        for (int x = 0; x < w; ++x) {
            DWORD p00 = rb[x1[x]], p01 = rb[x0[x]], p10 = ra[x1[x]], p11 = ra[x0[x]];
            DWORD lo = (9 * (p00 & 0xFF00FF) + 3 * (p01 & 0xFF00FF) + 3 * (p10 & 0xFF00FF) + (p11 & 0xFF00FF) + 0x080008) >> 4;
            DWORD mid = (9 * (p00 & 0xFF00) + 3 * (p01 & 0xFF00) + 3 * (p10 & 0xFF00) + (p11 & 0xFF00) + 0x800) >> 4;
            o[x] = 0xFF000000 | (lo & 0xFF00FF) | (mid & 0xFF00);
        }
    }
}

// Saturation boost (amount in 1/256 fixed point).
void Saturate(std::vector<DWORD>& px, float amount) {
    const int k = static_cast<int>(std::lround(amount * 256));
    for (DWORD& p : px) {
        int b = p & 0xFF, g = (p >> 8) & 0xFF, r = (p >> 16) & 0xFF;
        int l = (77 * r + 150 * g + 29 * b) >> 8;
        auto adj = [&](int c) { return static_cast<DWORD>(std::clamp(l + (((c - l) * k) >> 8), 0, 255)); };
        p = 0xFF000000 | (adj(r) << 16) | (adj(g) << 8) | adj(b);
    }
}

// Bilinear sample of one channel; x, y continuous (pixel centers at .5), edges clamp.
float SampleChannel(const std::vector<DWORD>& px, int w, int h, float x, float y, int shift) {
    float fx = std::clamp(x - 0.5f, 0.0f, static_cast<float>(w - 1));
    float fy = std::clamp(y - 0.5f, 0.0f, static_cast<float>(h - 1));
    int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float tx = fx - x0, ty = fy - y0;
    const DWORD* r0 = &px[static_cast<size_t>(y0) * w];
    const DWORD* r1 = &px[static_cast<size_t>(y1) * w];
    float a = static_cast<float>((r0[x0] >> shift) & 0xFF), b = static_cast<float>((r0[x1] >> shift) & 0xFF);
    float c = static_cast<float>((r1[x0] >> shift) & 0xFF), d = static_cast<float>((r1[x1] >> shift) & 0xFF);
    float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

// Capture area: one extra ring so blur and rim refraction never hit the edge;
// clipped to the monitor the window is on.
RECT ShotArea(const RECT& window, float scale) {
    int pad = static_cast<int>(std::lround(28 * scale));
    RECT area = {window.left - pad, window.top - pad, window.right + pad, window.bottom + pad};
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromRect(&window, MONITOR_DEFAULTTONEAREST), &mi)) IntersectRect(&area, &area, &mi.rcMonitor);
    return area;
}

// Raw -> background: half size, blur, saturate, scale back (blurred enough that the
// downsampling is invisible, several times faster).
void Process(const std::vector<DWORD>& raw, int w, int h, std::vector<DWORD>& blur, float scale, int style) {
    if (w <= 0 || h <= 0 || raw.size() != static_cast<size_t>(w) * h) {
        blur.clear();
        return;
    }
    int hw = (w + 1) / 2, hh = (h + 1) / 2;
    std::vector<DWORD> half = HalfSize(raw, w, h, hw, hh);
    float radius = style == kGlassWin11 ? 9.0f : 3.5f;
    Blur(half, hw, hh, std::max(1, static_cast<int>(std::lround(radius * scale))));
    Saturate(half, style == kGlassWin11 ? 1.2f : 1.45f);
    DoubleSize(half, hw, hh, blur, w, h);
}

}  // namespace

bool CaptureScreen(const RECT& area, std::vector<DWORD>& pixels) {
    int w = area.right - area.left, h = area.bottom - area.top;
    if (w <= 0 || h <= 0) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP dib = mem ? CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool ok = false;
    if (dib) {
        HGDIOBJ old = SelectObject(mem, dib);
        // CAPTUREBLT: include layered windows
        ok = BitBlt(mem, 0, 0, w, h, screen, area.left, area.top, SRCCOPY | CAPTUREBLT) != FALSE;
        GdiFlush();
        if (ok) {
            const DWORD* src = static_cast<const DWORD*>(bits);
            pixels.assign(src, src + static_cast<size_t>(w) * h);
            for (DWORD& p : pixels) p |= 0xFF000000;  // BitBlt leaves alpha empty
        }
        SelectObject(mem, old);
        DeleteObject(dib);
    }
    if (mem) DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

void Glass::Capture(const RECT& window, float scale) {
    SetScale(scale);
    m_area = ShotArea(window, scale);
    m_w = std::max(0L, m_area.right - m_area.left);
    m_h = std::max(0L, m_area.bottom - m_area.top);
    m_raw.clear();
    m_blur.clear();
    ++m_version;
    if (m_w <= 0 || m_h <= 0) {
        m_w = m_h = 0;
        return;
    }
    if (!CaptureScreen(m_area, m_raw)) m_raw.assign(static_cast<size_t>(m_w) * m_h, kFallbackColor);
    Process(m_raw, m_w, m_h, m_blur, m_scale, m_style);
}

void Glass::Reprocess() {
    if (m_raw.empty()) return;
    Process(m_raw, m_w, m_h, m_blur, m_scale, m_style);
    ++m_version;
}

bool Glass::SetFlip(std::vector<float> flip, std::vector<float> nearby, int gw, int gh, float cell) {
    cell = std::max(1.0f, cell);
    if (flip == m_flip && nearby == m_near && (flip.empty() || (gw == m_flipW && gh == m_flipH && cell == m_flipCell)))
        return false;
    m_flip.swap(flip);
    m_near.swap(nearby);
    m_flipW = gw;
    m_flipH = gh;
    m_flipCell = cell;
    return true;
}

void Glass::Prepare(int width, int height, const Shape& shape) {
    width = std::max(0, width);
    height = std::max(0, height);
    if (!m_texDirty && !m_texels.empty() && width == m_texW && height == m_texH) return;
    m_texDirty = false;
    m_texW = width;
    m_texH = height;
    m_texBounds = shape.Bounds();
    m_texels.assign(static_cast<size_t>(m_texW) * m_texH, Texel{});

    const float s = m_scale;
    const float bevel = 18 * s;        // width of the bent rim
    const float refraction = 14 * s;   // refraction offset at the outer edge
    const float rimWidth = 1.3f * s;   // width of the highlight line
    const float border = std::max(1.0f, std::round(s));  // Win11 style border width
    const float shadowSigma = 11 * s, shadowDy = 6 * s;
    const float lx = -0.5f, ly = -0.866f;  // light comes from the top left
    const float shadowReach = 3 * shadowSigma + shadowDy;
    const float twoSigma2 = 2 * shadowSigma * shadowSigma;
    const ShapeBounds b = m_texBounds;
    const float bh = b.h > 0 ? b.h : 1;

    for (int y = 0; y < m_texH; ++y) {
        Texel* row = &m_texels[static_cast<size_t>(y) * m_texW];
        float py = y + 0.5f;
        for (int x = 0; x < m_texW; ++x) {
            float px = x + 0.5f;
            float nx, ny;
            float d = shape.Distance(px, py, nx, ny);
            float cover = Clamp01(0.5f - d);

            float shadow = 0;
            if (cover < 1) {
                float ds = shape.Distance(px, py - shadowDy);
                if (ds < shadowReach) {
                    float q = std::max(0.0f, ds + 2 * s);
                    shadow = std::exp(-q * q / twoSigma2);
                    if (shadow < 0.008f) shadow = 0;
                }
            }
            Texel& t = row[x];
            t.cover = static_cast<BYTE>(std::lround(cover * 255));
            t.shadow = static_cast<BYTE>(std::lround(shadow * 255));
            if (!t.cover) continue;

            float depth = std::max(0.0f, -d);
            if (m_style == kGlassWin11) {  // flat, thin outer border
                t.light = static_cast<BYTE>(std::lround(Clamp01(border + 0.5f - depth) * 255));
                continue;
            }
            if (depth < bevel) {
                // sample inward along the normal: the rim magnifies like a convex lens
                float k = 1 - depth / bevel;
                float disp = refraction * k * k;
                t.dx = -nx * disp;
                t.dy = -ny * disp;
                t.bevel = 1;
            }

            // top gloss; bright rim on the lit side, weaker reflection opposite; soft sheen on the bevel
            float v = Clamp01((py - b.y) / bh);
            float ndl = nx * lx + ny * ly;
            float facing = std::max(0.0f, ndl), away = std::max(0.0f, -ndl);
            float spec = facing * facing * facing + 0.5f * away * away * away;
            float rim = depth < 8 * rimWidth ? std::exp(-depth / rimWidth) : 0;
            float edge = depth < bevel ? (1 - depth / bevel) * (1 - depth / bevel) : 0;
            float light = Clamp01(spec * (0.85f * rim + 0.25f * edge) + 0.16f * rim);
            t.light = static_cast<BYTE>(std::lround(light * 255));
            t.gloss = static_cast<BYTE>(std::lround((1 - v) * (1 - v) * 255));
        }
    }
}

DWORD Glass::Fetch(int x, int y) const {
    if (m_blur.empty()) return kFallbackColor;
    x = std::clamp(x, 0, m_w - 1);
    y = std::clamp(y, 0, m_h - 1);
    return m_blur[static_cast<size_t>(y) * m_w + x];
}

// Rim: sample the three channels with slightly different offsets for dispersion.
void Glass::SampleBevel(float x, float y, const Texel& t, float rgb[3]) const {
    if (m_blur.empty()) {
        rgb[0] = rgb[1] = rgb[2] = 32;
        return;
    }
    rgb[0] = SampleChannel(m_blur, m_w, m_h, x + t.dx * 0.9f, y + t.dy * 0.9f, 16);
    rgb[1] = SampleChannel(m_blur, m_w, m_h, x + t.dx, y + t.dy, 8);
    rgb[2] = SampleChannel(m_blur, m_w, m_h, x + t.dx * 1.1f, y + t.dy * 1.1f, 0);
}

void Glass::Render(DWORD* pixels, POINT origin, int slide, int slideX) const {
    // Rows are independent: split across a few threads (every animation frame redraws everything).
    const int h = m_texH;
    int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 4);
    if (static_cast<long long>(m_texW) * h < 200000) threads = 1;
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) {
        try {
            pool.emplace_back([=] { RenderRows(pixels, origin, slide, slideX, h * i / threads, h * (i + 1) / threads); });
        } catch (...) {  // thread creation failed: render that part here
            RenderRows(pixels, origin, slide, slideX, h * i / threads, h * (i + 1) / threads);
        }
    }
    RenderRows(pixels, origin, slide, slideX, 0, h / threads);
    for (std::thread& t : pool) t.join();
}

Glass::Tint Glass::TintNow() const {
    const bool win11 = m_style == kGlassWin11;
    Tint t;
    // Win11 style uses the system acrylic gray: dark #202020, light #F3F3F3, dense tint
    t.luma = win11 ? (m_light ? 243.0f : 32.0f) : (m_light ? 252.0f : 16.0f);
    t.rgb[0] = t.rgb[1] = t.luma;
    t.rgb[2] = win11 ? t.luma : (m_light ? 255.0f : 20.0f);
    if (m_customTint) {
        t.rgb[0] = GetRValue(m_tint);
        t.rgb[1] = GetGValue(m_tint);
        t.rgb[2] = GetBValue(m_tint);
        t.luma = ColorLuma(m_tint);
    }
    t.alpha = win11 ? (m_light ? 0.9f : 0.93f) : (m_light ? 0.36f : 0.38f);
    // Liquid transparency: k = 1 at the default 60 %; more transparent = lighter tint and looser guard
    t.guard = 1;
    if (m_style == kGlassLiquid) {
        float k = (100 - std::clamp(m_transparency, 0, 100)) / static_cast<float>(100 - kDefaultGlassTransparency);
        t.alpha = std::min(0.94f, t.alpha * k);
        t.guard = std::min(1.0f, k);
    }
    // When the glass and background disagree (light glass on dark background or vice versa),
    // the tint gets denser so text on top stays readable. Win11 style is stricter.
    t.bound = win11 ? (m_light ? 0.82f * 255 : 0.2f * 255) : (m_light ? 0.72f * 255 : 0.30f * 255);
    if (m_customTint) t.bound = m_light ? std::min(t.bound, t.luma - 20) : std::max(t.bound, t.luma + 20);
    return t;
}

float Glass::BaseLuma(float rx, float ry, float rw, float rh, POINT origin, bool guarded) const {
    Tint t = TintNow();
    if (m_blur.empty() || rw <= 0 || rh <= 0) return t.luma;
    const int offX = origin.x - m_area.left, offY = origin.y - m_area.top;
    float step = std::max(2.0f, std::max(rw, rh) / 24);
    double sum = 0;
    int n = 0;
    for (float y = ry + step / 2; y < ry + rh; y += step) {
        for (float x = rx + step / 2; x < rx + rw; x += step) {
            DWORD p = Fetch(static_cast<int>(x) + offX, static_cast<int>(y) + offY);
            float c[3] = {static_cast<float>((p >> 16) & 0xFF), static_cast<float>((p >> 8) & 0xFF),
                          static_cast<float>(p & 0xFF)};
            float ta = t.alpha;
            if (guarded) {  // same readability guard as RenderRows, so the result matches what is drawn
                float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
                if (m_light ? lum < t.bound : lum > t.bound)
                    ta = std::min(0.94f, std::max(ta, (t.bound - lum) / (t.luma - lum) * t.guard));
            }
            for (int k = 0; k < 3; ++k) c[k] += (t.rgb[k] - c[k]) * ta;
            sum += 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            ++n;
        }
    }
    return n ? static_cast<float>(sum / n) : t.luma;
}

void Glass::FlipAt(int tx, int ty, float& flip, float& nearby) const {
    float fx = (tx + 0.5f) / m_flipCell - 0.5f, fy = (ty + 0.5f) / m_flipCell - 0.5f;
    fx = std::clamp(fx, 0.0f, static_cast<float>(m_flipW - 1));
    fy = std::clamp(fy, 0.0f, static_cast<float>(m_flipH - 1));
    int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, m_flipW - 1), y1 = std::min(y0 + 1, m_flipH - 1);
    float ax = fx - x0, ay = fy - y0;
    auto sample = [&](const std::vector<float>& grid) {
        const float* r0 = &grid[static_cast<size_t>(y0) * m_flipW];
        const float* r1 = &grid[static_cast<size_t>(y1) * m_flipW];
        float top = r0[x0] + (r0[x1] - r0[x0]) * ax, bottom = r1[x0] + (r1[x1] - r1[x0]) * ax;
        return top + (bottom - top) * ay;
    };
    flip = sample(m_flip);
    nearby = sample(m_near);
}

void Glass::RenderRows(DWORD* pixels, POINT origin, int slide, int slideX, int rowBegin, int rowEnd) const {
    const int w = m_texW, h = m_texH;
    const bool win11 = m_style == kGlassWin11;
    const Tint tn = TintNow();
    const float* tint = tn.rgb;
    const float tintL = tn.luma, tintAlpha = tn.alpha, guard = tn.guard, bound = tn.bound;
    const float shadowAlpha = win11 ? (m_light ? 0.16f : 0.3f) : (m_light ? 0.22f : 0.32f);
    // Win11 border: white on dark, black on light
    const float borderTo = m_light ? 0.0f : 255.0f, borderAlpha = (m_light ? 0.1f : 0.12f) / 255.0f;
    const float sheen = (m_light ? 0.12f : 0.07f) / 255.0f;
    // Text-inversion guard (see SetFlip); inactive while m_flip is empty.
    const bool flips = !m_flip.empty() && m_flipW > 0 && m_flipH > 0;
    const float flipTo[3] = {m_light ? 16.0f : 252.0f, m_light ? 16.0f : 252.0f, m_light ? 20.0f : 255.0f};
    const float flipL = m_light ? 16.5f : 252.3f, flipBound = m_light ? 110.0f : 140.0f;
    float looseBound = m_light ? 140.0f : 110.0f;
    if (m_customTint) looseBound = m_light ? std::min(looseBound, tintL - 20) : std::max(looseBound, tintL + 20);
    looseBound = m_light ? std::min(looseBound, bound) : std::max(looseBound, bound);
    // window pixel -> capture coordinates
    const int offX = origin.x - m_area.left, offY = origin.y - m_area.top;

    for (int y = rowBegin; y < rowEnd; ++y) {
        DWORD* row = pixels + static_cast<size_t>(y) * w;
        int ty = y - slide;  // material moves with the panel, background stays
        if (ty < 0 || ty >= h) {
            std::fill(row, row + w, 0);
            continue;
        }
        const Texel* tex = &m_texels[static_cast<size_t>(ty) * w];
        for (int x = 0; x < w; ++x) {
            int tx = x - slideX;
            if (tx < 0 || tx >= w) {
                row[x] = 0;
                continue;
            }
            const Texel& t = tex[tx];
            if (!t.cover) {
                row[x] = static_cast<DWORD>(t.shadow * shadowAlpha + 0.5f) << 24;
                continue;
            }
            float c[3];
            if (t.bevel) {
                SampleBevel(static_cast<float>(x + offX) + 0.5f, static_cast<float>(y + offY) + 0.5f, t, c);
            } else {
                DWORD p = Fetch(x + offX, y + offY);
                c[0] = static_cast<float>((p >> 16) & 0xFF);
                c[1] = static_cast<float>((p >> 8) & 0xFF);
                c[2] = static_cast<float>(p & 0xFF);
            }
            float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            float flip = 0, nearby = 0;
            if (flips) FlipAt(tx, ty, flip, nearby);
            float bnd = nearby > 0 ? bound + (looseBound - bound) * nearby : bound;
            float ta = tintAlpha;
            if (m_light ? lum < bnd : lum > bnd) ta = std::min(0.94f, std::max(ta, (bnd - lum) / (tintL - lum) * guard));
            float light = std::min(1.0f, t.light / 255.0f + t.gloss * sheen), cover = t.cover / 255.0f;
            float a = cover + t.shadow * shadowAlpha / 255.0f * (1 - cover);
            DWORD out = static_cast<DWORD>(a * 255 + 0.5f) << 24;
            float grain = 0;
            if (win11) {  // acrylic grain, fixed to the panel so it moves with it
                unsigned n = static_cast<unsigned>(tx) * 374761393u + static_cast<unsigned>(ty) * 668265263u;
                n = (n ^ (n >> 13)) * 1274126177u;
                grain = static_cast<float>(static_cast<int>((n ^ (n >> 16)) & 255u) - 128) * (2.5f / 128);
                light = 0;
            }
            float alt[3] = {};
            if (flip > 0.001f) {
                for (int k = 0; k < 3; ++k) alt[k] = c[k] + (tint[k] - c[k]) * tintAlpha;
                float lumB = 0.299f * alt[0] + 0.587f * alt[1] + 0.114f * alt[2];
                if (m_light ? lumB > flipBound : lumB < flipBound) {
                    float g = std::min(0.94f, (flipBound - lumB) / (flipL - lumB) * guard);
                    for (int k = 0; k < 3; ++k) alt[k] += (flipTo[k] - alt[k]) * g;
                }
            }
            for (int k = 0; k < 3; ++k) {
                float col = c[k] + (tint[k] - c[k]) * ta;
                if (flip > 0.001f) col += (alt[k] - col) * flip;
                col += (255 - col) * light;
                if (win11) col += (borderTo - col) * (t.light * borderAlpha) + grain;
                out |= static_cast<DWORD>(std::clamp(col * cover, 0.0f, 255.0f) + 0.5f) << (16 - 8 * k);
            }
            row[x] = out;
        }
    }
}

}  // namespace lg
