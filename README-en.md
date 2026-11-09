English | [繁體中文](README.md)

# LiquidGlass UI for AutoHotkey v2

A liquid-glass UI library for AutoHotkey v2 (x64). `LiquidGlass.dll` draws (background blur, rim refraction with
chromatic dispersion, highlights, shadow), `LiquidGlass.ahk` wraps it, and `GlassMenu.ahk` provides a glass popup
menu with an interface modelled on AHK's `Menu()`. Windows, events and hotkeys stay in AHK; the dll never runs a
message loop.

See [SPEC.md](SPEC.md) (Traditional Chinese) for the full specification.

## Quick start

1. Windows 10 2004+ / Windows 11 and the **64-bit** [AutoHotkey v2](https://www.autohotkey.com/) (`AutoHotkey64.exe`).
2. Download this repo (or the zip from Releases); `bin\x64\LiquidGlass.dll` is prebuilt.
3. Run `examples\simple_menu.ahk` and press **Ctrl + right click** anywhere.

```ahk
#Include ahk\GlassMenu.ahk

folders := GlassMenu({accent: 0xF8D86E})            ; submenu with its own glass tint
folders.Add("Documents", (*) => Run(A_MyDocuments))
folders.SetIcon("Documents", "imageres.dll", 108)

mainMenu := GlassMenu({accent: 0xC0F3A3})
mainMenu.Add("Folders", folders)                    ; submenu
mainMenu.Add()                                      ; separator
mainMenu.Add("Notepad", (*) => Run("notepad.exe"))  ; callback (ItemName, ItemPos, MenuObj), as in Menu()
mainMenu.SetIcon("Notepad", A_WinDir "\notepad.exe")

<^RButton:: mainMenu.Show()                         ; at the mouse by default
```

## Examples

| File | Shows |
| --- | --- |
| `examples/simple_menu.ahk` | the smallest glass menu (with one submenu) |
| `examples/demo_menu.ahk` | a full menu: three tints, shortcut text, a disabled item |
| `examples/demo_panel.ahk` | an empty panel; F1–F4 switch dark / light / liquid / acrylic |
| `examples/demo_content.ahk` | content layer: text, .ico / exe / dll icons, PNG, hover |
| `examples/demo_anim.ahk` | slide, zoom and fade animations; recapture while shown |

## Using it in your own scripts

No need to copy anything into the AutoHotkey folder or `Lib`. Keep `ahk\` and `bin\x64\` in the same relative layout:

```
YourScriptFolder\
├─ MyScript.ahk            ← #Include LiquidGlass\ahk\GlassMenu.ahk
└─ LiquidGlass\
   ├─ LICENSE
   ├─ ahk\LiquidGlass.ahk
   ├─ ahk\GlassMenu.ahk
   └─ bin\x64\LiquidGlass.dll
```

The dll is looked up at `ahk\..\bin\x64\LiquidGlass.dll`; override with `LiquidGlass.DllPath := "..."`.
The CRT is linked statically, so no VC++ redistributable is needed.

## GlassMenu

`Add`, `SetIcon`, `Delete`, `Disable` / `Enable`, `Show(x?, y?)`, `Close()`; items by name or `"N&"` position;
text after `` `t `` in a name is shown right-aligned as shortcut text.

- Submenus open after hovering `subDelay` (250 ms); items run on left button release, after a quick fade
  (`invokeMs` 200 ms).
- Keyboard: ↑↓ move, → / Enter open a submenu, ← back, Enter run, Esc close one level (only while open).
- Outside clicks, the Win key and foreground changes (Alt+Tab) close the menu (`cancelMs` 800 ms fade).
- Never takes focus; submenus flip to the left at the right screen edge and move up at the bottom.
- The opening animation (`showMs` 850 ms) is non-blocking: hover and clicks work while it plays.
- Options: `light`, `style` (liquid / acrylic), `transparency`, `accent`, `iconSize`, `rowHeight`, `fontSize`, `font`,
  `minWidth`, `maxWidth`, `radius`, `anim` (slide / zoom), `showMs`, `invokeMs`, `cancelMs`, `subDelay`, `textColor`, `scale`.

## DPI

AutoHotkey v2 is system-DPI aware: sizes and effects follow `A_ScreenDPI` (layout verified at 100 / 125 / 150 %).
On a secondary monitor with a different scale, Windows stretches the window (correct size, slightly soft).
`{scale: 1.5}` forces a scale for previews.

## Build

```sh
./build-mingw.sh     # MinGW-w64 x64 (Linux cross, or MSYS2 / w64devkit on Windows) — used for bin\x64
```

Static CRT, depends only on user32, gdi32, gdiplus and dwmapi.

MSVC (optional, e.g. if an antivirus flags the MinGW build; needs a Windows 10/11 SDK and CMake):

```bat
cmake -S native -B build\msvc -A x64
cmake --build build\msvc --config Release
```

Static CRT, `/guard:cf`, full version resource, no packing.

## Contributors

- [CLI1976](https://github.com/CLI1976): author, specification, testing
- [Claude](https://claude.com/claude-code) (Anthropic): co-implemented the native dll, the AHK wrappers and the examples via Claude Code

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
- `BaseLuma` takes a plain rectangle and includes the readability guard, so automatic text colors match the screen.
- Added the `extern "C"` API (`native/api.cpp`), the content layer (`native/canvas.cpp`), non-blocking animation,
  recapture while shown, exception guards and error messages.
- The original `SetFlip` (text inversion guard) code is kept but not wired up in v1.
- TaskbarPopup's taskbar features (window list, thumbnails, tray, volume, ...) are not included.
