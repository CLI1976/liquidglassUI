// LiquidGlass - glass material.
// Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
// Derived from DM74164/TaskbarPopup src/common.h (class Glass) and src/util.cpp
// (CaptureScreen, ColorLuma), Copyright (C) 2026 DM74164, AGPL-3.0.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

#include "shape.h"

namespace lg {

enum GlassStyle { kGlassLiquid = 0, kGlassWin11 = 1 };
constexpr int kDefaultGlassTransparency = 60;  // 0 - 100 %, 60 matches the original look
constexpr DWORD kFallbackColor = 0xFF202024;   // used when the screen capture fails

// Screen capture, BGRA, opaque.
bool CaptureScreen(const RECT& area, std::vector<DWORD>& pixels);
// 0 - 255
inline float ColorLuma(COLORREF c) { return 0.299f * GetRValue(c) + 0.587f * GetGValue(c) + 0.114f * GetBValue(c); }

// Captures the screen behind the panel, blurs and saturates it, and derives refraction,
// chromatic dispersion, highlights and shadow from the shape's distance field.
// The rendered glass is opaque (background is baked in), so it must be shown at the
// screen position used for the capture.
class Glass {
public:
    // Captures the screen behind `window` (plus a margin) and processes it with the current style.
    // Falls back to a solid color when the capture fails.
    void Capture(const RECT& window, float scale);
    // Re-runs blur / saturation on the last raw capture (style changed).
    void Reprocess();
    bool HasBackground() const { return !m_blur.empty(); }
    const RECT& Area() const { return m_area; }

    void SetLight(bool light) {
        m_light = light;
        ++m_version;
    }
    bool Light() const { return m_light; }
    void SetScale(float scale) {
        if (scale != m_scale) m_texDirty = true;
        m_scale = scale;
    }
    float Scale() const { return m_scale; }
    void SetStyle(int style) {
        if (style != m_style) m_texDirty = true;
        m_style = style;
        ++m_version;
    }
    int Style() const { return m_style; }
    void SetTransparency(int percent) {
        m_transparency = percent;
        ++m_version;
    }
    void SetTint(bool custom, COLORREF color) {
        m_customTint = custom;
        m_tint = color;
        ++m_version;
    }
    unsigned Version() const { return m_version; }

    // Average luma (0 - 255) of the tinted glass inside rect (window coordinates).
    // origin: window top-left on screen. guarded = include the readability guard (what is
    // actually drawn); false = tint only, as in the original (used for SetFlip).
    float BaseLuma(float x, float y, float w, float h, POINT origin, bool guarded = true) const;

    // Text-inversion guard from the original (not wired up in v1, kept for later).
    bool SetFlip(std::vector<float> flip, std::vector<float> nearby, int gw, int gh, float cell);

    // Marks the per-pixel material as stale (shape changed).
    void Invalidate() { m_texDirty = true; }
    // Precomputes the material for a width x height window. No-op when nothing changed.
    void Prepare(int width, int height, const Shape& shape);
    // Draws shadow + glass into premultiplied BGRA pixels (width x height from Prepare).
    // origin: window top-left on screen. slide / slideX: panel offset in pixels while the
    // background stays put (used by animations).
    void Render(DWORD* pixels, POINT origin, int slide, int slideX = 0) const;

private:
    struct Texel {
        float dx = 0, dy = 0;  // refraction offset (green channel)
        BYTE cover = 0;        // panel coverage
        BYTE shadow = 0;       // shadow opacity
        BYTE light = 0;        // added white light (rim); Win11 style: border strength
        BYTE gloss = 0;        // top gloss weight
        BYTE bevel = 0;        // 1 = inside the bent rim, needs refracted sampling
    };
    struct Tint {
        float rgb[3];
        float luma, alpha, guard, bound;
    };
    void RenderRows(DWORD* pixels, POINT origin, int slide, int slideX, int rowBegin, int rowEnd) const;
    Tint TintNow() const;
    void FlipAt(int tx, int ty, float& flip, float& nearby) const;
    void SampleBevel(float x, float y, const Texel& t, float rgb[3]) const;
    DWORD Fetch(int x, int y) const;

    std::vector<DWORD> m_raw;   // raw capture
    std::vector<DWORD> m_blur;  // blurred + saturated background
    RECT m_area = {};           // capture area on screen
    int m_w = 0, m_h = 0;
    float m_scale = 1;
    bool m_light = false;
    int m_style = kGlassLiquid;
    int m_transparency = kDefaultGlassTransparency;
    bool m_customTint = false;
    COLORREF m_tint = 0;
    unsigned m_version = 0;
    std::vector<float> m_flip, m_near;
    int m_flipW = 0, m_flipH = 0;
    float m_flipCell = 1;

    std::vector<Texel> m_texels;
    int m_texW = 0, m_texH = 0;
    bool m_texDirty = true;
    ShapeBounds m_texBounds;
};

}  // namespace lg
