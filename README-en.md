English | [繁體中文](README.md)

# LiquidGlass UI for AutoHotkey v2

A liquid-glass UI library for AutoHotkey v2 (x64): `LiquidGlass.dll` draws, `LiquidGlass.ahk` wraps it.
Windows, events and hotkeys stay in AHK; the dll never runs a message loop.

See [SPEC.md](SPEC.md) (Traditional Chinese) for the full specification.

## Origin and license

- The glass material algorithm is **adapted from `src/glass.cpp` of [DM74164/TaskbarPopup](https://github.com/DM74164/TaskbarPopup)**,
  with reference to the `Glass` class in `src/common.h` and `CaptureScreen` / `ColorLuma` in `src/util.cpp`.
  Original work Copyright (C) 2026 DM74164, licensed under AGPL-3.0.
- This project is licensed under **AGPL-3.0** as well; see [LICENSE](LICENSE) (the original LICENSE file).

### Changes from the original

- Moved out of TaskbarPopup's `app` namespace into a standalone `lg` namespace.
- Shapes are abstracted as signed distance fields (`Shape` in `native/shape.h`); the material only uses
  `Distance()` / `Bounds()`. Round rectangles today; circles, rings and sectors can follow.
- `Glass::Shoot` / `Extend` simplified to `Capture` and `Reprocess` (re-blur the raw capture on style change).
- `BaseLuma` takes a plain rectangle instead of `Gdiplus::RectF`.
- Added the `extern "C"` API (`native/api.cpp`), content-layer compositing, exception guards and error messages.
- The original `SetFlip` (text inversion guard) code is kept but not wired up in v1.
- TaskbarPopup's taskbar features (window list, thumbnails, tray, volume, ...) are not included.

## Build

```sh
./build-mingw.sh     # MinGW-w64 x64 (Linux cross or MSYS2 / w64devkit on Windows)
```

Produces `bin/x64/LiquidGlass.dll` with a static CRT, depending only on user32, gdi32, gdiplus and dwmapi.
An MSVC CMake setup follows in stage 5.
