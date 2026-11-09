[English](README-en.md) | 繁體中文

# LiquidGlass UI for AutoHotkey v2

給 AutoHotkey v2（x64）用的液態玻璃 UI library：`LiquidGlass.dll` 負責繪製，`LiquidGlass.ahk` 包裝成 AHK 類別。
視窗、事件、熱鍵都留在 AHK，dll 不碰訊息迴圈。

需求規格見 [SPEC.md](SPEC.md)。

## 來源與授權

- 玻璃材質演算法**改自 [DM74164/TaskbarPopup](https://github.com/DM74164/TaskbarPopup) 的 `src/glass.cpp`**，
  並參考 `src/common.h` 的 `Glass` 類別、`src/util.cpp` 的 `CaptureScreen` 與 `ColorLuma`。
  原作 Copyright (C) 2026 DM74164，以 AGPL-3.0 授權。
- 本專案同樣以 **AGPL-3.0** 授權，全文見 [LICENSE](LICENSE)（沿用原作的 LICENSE 檔）。

### 相對原作的修改

- 移出 TaskbarPopup 的 `app` 命名空間，改為獨立的 `lg` 命名空間，不依賴原作其他標頭。
- 形狀抽象成距離場介面（`native/shape.h` 的 `Shape`），材質只依 `Distance()` / `Bounds()` 計算；
  目前實作圓角矩形，之後可加圓形、圓環、扇形。
- `Glass::Shoot` / `Extend` 簡化為 `Capture` 與 `Reprocess`（切換風格時用原始截圖重新模糊）。
- `BaseLuma` 改收一般矩形參數，不再用 `Gdiplus::RectF`。
- 新增 `extern "C"` 匯出 API（`native/api.cpp`）、內容圖層合成、例外防護與錯誤訊息。
- 原作的 `SetFlip`（文字反色保底）程式碼保留，第一版未接上。
- 未包含 TaskbarPopup 的任務欄功能（視窗列表、縮圖、托盤、音量等）。

## 目錄

```
native/     C++ 原始碼（glass、shape、api）、.def、版本資源
ahk/        LiquidGlass.ahk（低階包裝類）
examples/   通用範例
bin/x64/    編譯出的 LiquidGlass.dll
```

## 編譯

### MinGW-w64（目前）

Linux 交叉編譯（需 `g++-mingw-w64-x86-64-posix`）或 Windows 上的 MSYS2 / [w64devkit](https://github.com/skeeto/w64devkit)（已用 w64devkit 2.11.0、GCC 16.2 驗證）。Windows 上把 w64devkit 的 `bin` 加到 PATH 後執行：

```sh
./build-mingw.sh
```

輸出 `bin/x64/LiquidGlass.dll`，靜態連結 CRT，只依賴 user32、gdi32、gdiplus、dwmapi。

### MSVC

第 5 階段補上 CMake 設定。

## 使用

```ahk
#Include ahk\LiquidGlass.ahk
lg := LiquidGlass(360, 220, {radius: 22, light: "auto", style: "liquid", transparency: 60})
lg.Show(x, y)      ; 在 (x, y) 截背景並顯示
lg.SetStyle("acrylic"), lg.Update()
lg.Hide()
```

座標與尺寸一律是實體像素。dll 預設路徑為 `ahk\..\bin\x64\LiquidGlass.dll`，可用 `LiquidGlass.DllPath` 覆寫。

## 進度

- [x] 第 1 階段：dll 骨架（Create / Capture / Present / Destroy、深淺色、Acrylic）
- [ ] 第 2 階段：內容層（文字、圖示、圖片、色塊、分隔線）
- [ ] 第 3 階段：動畫與重截（`WDA_EXCLUDEFROMCAPTURE`）
- [ ] 第 4 階段：GlassMenu
- [ ] 第 5 階段：MSVC、發布準備
