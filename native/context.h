// LiquidGlass - per-panel state shared by api.cpp and canvas.cpp.
// Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// GDI+ headers rely on min/max macros, which NOMINMAX removes.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "glass.h"
#include "shape.h"

namespace lg {

constexpr DWORD kContextMagic = 0x4C47'4C41;  // "LGLA"

struct AnimState {
    int kind = 0;  // 0 = idle, 1 = slide in, 2 = fade out, 3 = zoom in
    int ms = 0;
    double start = 0;
    BYTE fromAlpha = 255;
    bool shown = false;
    int frames = 0;
    std::vector<DWORD> rest;  // zoom: the finished frame
};

struct Context {
    DWORD magic = kContextMagic;
    HWND hwnd = nullptr;

    // Panel size in physical pixels; the window is panel + 2 * margin.
    int panelW = 0, panelH = 0;
    int margin = 0;
    int winW = 0, winH = 0;
    float scale = 1;

    // Shape (panel coordinates; radius < 0 = style default).
    //   round rect: [0] corner radius
    //   circle:     -
    //   ring:       [0] inner radius
    //   sector:     [0] cx [1] cy [2] outer [3] inner [4] start deg [5] sweep deg [6] corner (< 0 = default)
    int shapeKind = kShapeRoundRect;
    float shapeParam[8] = {-1, 0, 0, 0, 0, 0, 0, 0};
    std::unique_ptr<Shape> shape;

    Glass glass;
    bool captured = false;
    POINT capturePos = {};  // window top-left used for the last capture

    // Window-size premultiplied BGRA DIB pushed with UpdateLayeredWindow.
    HBITMAP dib = nullptr;
    HDC dc = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    DWORD* bits = nullptr;
    int dibW = 0, dibH = 0;

    // Cached glass layer (at rest) and the content layer (premultiplied ARGB, window size,
    // content drawn at panel offset `margin`).
    std::vector<DWORD> base;
    bool baseValid = false;
    POINT basePos = {};
    std::vector<DWORD> content;

    POINT pos = {};  // current window top-left on screen
    BYTE alpha = 255;
    int excludeWorks = 0;  // WDA_EXCLUDEFROMCAPTURE: 0 = unknown, 1 = works, -1 = not supported
    AnimState anim;
};

// Defined in api.cpp
void SetError(const wchar_t* fmt, ...);
bool EnsureGdiplus();

// LG_Text flags
enum TextFlags {
    kTextBold = 0x01,
    kTextCenter = 0x02,    // horizontal center
    kTextRight = 0x04,     // right aligned
    kTextVCenter = 0x08,   // vertical center
    kTextEllipsis = 0x10,  // trim with "…"
    kTextNoWrap = 0x20,    // single line
    kTextBottom = 0x40,    // bottom aligned
};

// Defined in canvas.cpp (panel-relative coordinates)
bool CanvasClear(Context& c);
bool CanvasFillRoundRect(Context& c, float x, float y, float w, float h, float r, DWORD argb);
bool CanvasFillShape(Context& c, const Shape& shape, DWORD argb);
bool CanvasLine(Context& c, float x1, float y1, float x2, float y2, float width, DWORD argb);
bool CanvasText(Context& c, const wchar_t* text, float x, float y, float w, float h, const wchar_t* font,
                float sizePx, int flags, DWORD argb);
bool CanvasIcon(Context& c, HICON icon, float x, float y, float size);
bool CanvasIconFile(Context& c, const wchar_t* path, int index, float x, float y, float size);
bool CanvasImage(Context& c, const wchar_t* path, float x, float y, float w, float h);
float CanvasMeasureText(const wchar_t* text, const wchar_t* font, float sizePx, int flags);

}  // namespace lg
