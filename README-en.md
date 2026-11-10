English | [繁體中文](README.md)

# LiquidGlass UI for AutoHotkey v2

A liquid-glass UI library for AutoHotkey v2 (x64). `LiquidGlass.dll` draws (background blur, rim refraction with
chromatic dispersion, highlights, shadow), `LiquidGlass.ahk` wraps it, and `GlassMenu.ahk` provides a glass popup
menu with an interface modelled on AHK's `Menu()`. Windows, events and hotkeys stay in AHK; the dll never runs a
message loop.

See [SPEC.md](SPEC.md) (Traditional Chinese) for the full specification.

## Quick start

1. Windows 10 2004+ / Windows 11 and the **64-bit** [AutoHotkey v2](https://www.autohotkey.com/) (`AutoHotkey64.exe`).
2. Download this repo (or the zip from Releases); `lib\LiquidGlass.dll` is prebuilt.
3. Run `examples\simple_menu.ahk` and press **Ctrl + right click** anywhere.

```ahk
#Include <GlassMenu>                                ; from the Lib\ folder next to the script, see below

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
| `examples/demo_radial.ahk` | circle, ring and sector menus (F1 / F2 / F3) with a submenu |

## Using it in your own scripts

Copy the files in `lib\` into a `Lib\` folder next to your script (AHK's [library folder](https://www.autohotkey.com/docs/v2/Scripts.htm#lib))
and load them with `#Include <GlassMenu>` (or `<GlassRadial>`):

```
YourScriptFolder\
├─ MyScript.ahk            ← #Include <GlassMenu>
└─ Lib\
   ├─ LiquidGlass.ahk
   ├─ GlassMenu.ahk
   ├─ GlassRadial.ahk       ← only for circle / sector menus
   └─ LiquidGlass.dll
```

To share them between scripts, use `%A_MyDocuments%\AutoHotkey\Lib\` instead. Keep the files together:
`GlassMenu.ahk` includes `LiquidGlass.ahk` from its own folder and the dll is looked up next to `LiquidGlass.ahk`
(override with `LiquidGlass.DllPath := "..."`). Ship `LICENSE` (AGPL-3.0) with them.
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

## GlassRadial (circle, ring and sector menus)

Same API as `GlassMenu`; `Show(x?, y?)` takes the center (a sector's apex), the mouse by default.

```ahk
#Include <GlassRadial>
r := GlassRadial({shape: "circle", radius: 150, title: "Tools"})   ; "circle" | "ring" | "sector"
r.Add("Notepad", (*) => Run("notepad.exe"))
r.Add("More", GlassRadial({shape: "ring"}))                       ; opens in place; its center goes back
<^RButton:: r.Show()
```

- Items run clockwise, the first at 12 o'clock (circle / ring) or from `start` over `sweep` (sector).
- Click an item to run it; the center (or Esc / Backspace) goes back or closes; clicking elsewhere closes.
  Keys: ←↑ / →↓ / Tab move, Enter runs.
- Options: `shape`, `radius` (140), `innerRadius` (52 / sector 44), `start` / `sweep` (sector, -60 / 120), `corner`,
  `iconSize`, `fontSize`, `font`, `labels` ("center" / "item" / "none"), `title`, `gap`, `dividers`, plus `light`, `style`,
  `transparency`, `accent`, `textColor`, `anim` (zoom), `showMs`, `invokeMs`, `cancelMs`, `scale` as in `GlassMenu`.
- Low level: `lg.SetCircle()`, `lg.SetRing(inner)`, `lg.SetSector(cx, cy, outer, inner, start, sweep, corner?)`,
  `lg.FillSector(...)` (degrees, 0 = 12 o'clock, clockwise).

## DPI

AutoHotkey v2 is system-DPI aware: sizes and effects follow `A_ScreenDPI` (layout verified at 100 / 125 / 150 %).
On a secondary monitor with a different scale, Windows stretches the window (correct size, slightly soft).
`{scale: 1.5}` forces a scale for previews.

## Build

Both compilers produce the same dll (pixel-identical output) at `lib\LiquidGlass.dll`, with a static CRT and
only user32, gdi32, gdiplus and dwmapi as dependencies.

**MSVC** (used for `lib`; antivirus heuristics tend to be milder on it). Needs Visual Studio 2019/2022 (Build
Tools are enough) with the C++ workload, a Windows 10/11 SDK and the C++ CMake tools:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
build-msvc.bat       # finds Visual Studio itself; no developer prompt needed
```

`/MT`, `/guard:cf`, `/DYNAMICBASE`, `/NXCOMPAT`, full version resource, no packing.

**MinGW-w64** (alternative; Linux cross, or MSYS2 / w64devkit on Windows):

```sh
./build-mingw.sh
```

## Contributors

- [CLI1976](https://github.com/CLI1976): author, specification, testing
- [Claude](https://claude.com/claude-code) (Anthropic): co-implemented the native dll, the AHK wrappers, the radial menus and the examples via Claude Code

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
