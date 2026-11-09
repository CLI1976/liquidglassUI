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

struct Context {
    DWORD magic = kContextMagic;
    HWND hwnd = nullptr;

    // Panel size in physical pixels; the window is panel + 2 * margin.
    int panelW = 0, panelH = 0;
    int margin = 0;
    int winW = 0, winH = 0;
    float scale = 1;

    // Shape (panel coordinates; radius < 0 = style default).
    int shapeKind = kShapeRoundRect;
    float shapeParam[4] = {-1, 0, 0, 0};
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
};

// Defined in api.cpp
void SetError(const wchar_t* fmt, ...);
bool EnsureGdiplus();

}  // namespace lg
