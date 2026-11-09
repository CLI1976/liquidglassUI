[English](README-en.md) | 繁體中文

# LiquidGlass UI for AutoHotkey v2

給 AutoHotkey v2（x64）用的液態玻璃 UI library：`LiquidGlass.dll` 負責繪製（背景模糊、邊緣折射與色散、高光、陰影），
`LiquidGlass.ahk` 包裝成 AHK 類別，`GlassMenu.ahk` 提供介面仿 `Menu()` 的玻璃選單。
視窗、事件、熱鍵都留在 AHK，dll 不碰訊息迴圈。

需求規格見 [SPEC.md](SPEC.md)。

## 快速開始

1. 需要 Windows 10 2004+ / Windows 11，以及 [AutoHotkey v2](https://www.autohotkey.com/) 的 **64-bit** 版（`AutoHotkey64.exe`）。
2. 下載本 repo（或 Releases 裡的 zip），`bin\x64\LiquidGlass.dll` 已經編好。
3. 執行 `examples\simple_menu.ahk`，在任何地方按 **Ctrl + 右鍵**。

```ahk
#Include ahk\GlassMenu.ahk

folders := GlassMenu({accent: 0xF8D86E})          ; 子選單，有自己的玻璃底色
folders.Add("文件", (*) => Run(A_MyDocuments))
folders.SetIcon("文件", "imageres.dll", 108)

mainMenu := GlassMenu({accent: 0xC0F3A3})
mainMenu.Add("資料夾", folders)                    ; 子選單
mainMenu.Add()                                     ; 分隔線
mainMenu.Add("記事本", (*) => Run("notepad.exe"))  ; 回呼參數同 Menu()：(ItemName, ItemPos, MenuObj)
mainMenu.SetIcon("記事本", A_WinDir "\notepad.exe")

<^RButton:: mainMenu.Show()                        ; 預設在滑鼠位置
```

## 範例

| 檔案 | 內容 |
| --- | --- |
| `examples/simple_menu.ahk` | 最小的玻璃選單（含一個子選單） |
| `examples/demo_menu.ahk` | 完整選單：三種底色、快捷鍵文字、停用項目 |
| `examples/demo_panel.ahk` | 空的玻璃面板，F1–F4 切換深淺色 / 液態 / Acrylic |
| `examples/demo_content.ahk` | 內容層：文字、.ico / exe / dll 圖示、PNG、hover |
| `examples/demo_anim.ahk` | 滑入、縮放、淡出動畫與顯示中重截 |

## 放進自己的腳本

不用放進 AutoHotkey 的程式資料夾，也不用放進 `Lib`。把 `ahk\` 與 `bin\x64\` 照原本的相對位置帶著走即可：

```
你的腳本資料夾\
├─ MyScript.ahk            ← #Include LiquidGlass\ahk\GlassMenu.ahk
└─ LiquidGlass\
   ├─ LICENSE
   ├─ ahk\LiquidGlass.ahk
   ├─ ahk\GlassMenu.ahk
   └─ bin\x64\LiquidGlass.dll
```

dll 依 `ahk\..\bin\x64\LiquidGlass.dll` 尋找，也可以在建立選單前用 `LiquidGlass.DllPath := "..."` 指定。
dll 靜態連結執行階段，不需要安裝 VC++ 可轉散發套件。

## GlassMenu

介面仿 AHK 內建 `Menu()`：`Add`、`SetIcon`、`Delete`、`Disable` / `Enable`、`Show(x?, y?)`、`Close()`，
回呼參數同樣是 `(ItemName, ItemPos, MenuObj)`，項目可用名稱或 `"N&"` 位置指定，名稱裡的 `` `t `` 後面當成快捷鍵文字靠右顯示。

- 滑鼠停在子選單項目 `subDelay`（250 ms）後展開；左鍵放開才執行，先淡出（`invokeMs` 200 ms）再呼叫回呼。
- 鍵盤：↑↓ 移動、→ / Enter 展開子選單、← 收回、Enter 執行、Esc 關閉一層；只在選單開著時攔截。
- 點選單外、按 Win、切換前景視窗（Alt+Tab）都會關閉（`cancelMs` 800 ms 淡出）。
- 彈出時不搶焦點；子選單在螢幕右緣自動開到左側，下緣超出就往上推。
- 彈出動畫（`showMs` 850 ms）不阻塞，播放中就能 hover、點選。
- 選項：`light`、`style`（liquid / acrylic）、`transparency`、`accent`、`iconSize`、`rowHeight`、`fontSize`、`font`、
  `minWidth`、`maxWidth`、`radius`、`anim`（slide / zoom）、`showMs`、`invokeMs`、`cancelMs`、`subDelay`、`textColor`、`scale`。

## LiquidGlass（低階面板）

座標與尺寸一律是實體像素，內容座標相對面板左上角，顏色 `0xAARRGGBB`。

```ahk
#Include ahk\LiquidGlass.ahk
lg := LiquidGlass(300, 200, {radius: 22, light: "auto", style: "liquid", transparency: 60})
lg.Capture(x, y)                                   ; 先截背景，"auto" 文字顏色才判斷得準
lg.Clear()
lg.Rect(8, 8, 284, 40, 10, 0x33FFFFFF)             ; 半透明圓角色塊
lg.Icon("imageres.dll", 194, 16, 12, 32)           ; .ico / exe / dll，索引同 AHK（1 起算，負數 = 資源 ID）
lg.Image("logo.png", 16, 60, 32, 32)               ; PNG 等 GDI+ 支援的圖片
lg.Line(16, 56, 284, 56)                           ; 分隔線，預設顏色跟著深淺色
lg.Text("開啟", 56, 8, 200, 40, {size: 15, valign: "center", color: "auto"})
lg.Show(x, y, "slide", 850)                        ; 滑入 + 淡入（阻塞）；"zoom" = 從中心縮放；"" = 直接顯示
lg.ShowAsync(x, y, "slide", 850)                   ; 同上，但不阻塞
lg.Capture(lg.X, lg.Y)                             ; 顯示中重截（WDA_EXCLUDEFROMCAPTURE 排除自己）
lg.Hide("fade", 800)                               ; 淡出後隱藏；HideAsync 為不阻塞版
```

`Text` 選項：`font`（預設 Microsoft JhengHei UI）、`size`、`bold`、`align`（left / center / right）、
`valign`（top / center / bottom）、`ellipsis`（預設 true）、`wrap`（預設 false）、`color`（`"auto"` 依玻璃實際亮度選黑或白）。

## DPI

- AutoHotkey v2 是「系統 DPI 感知」，`A_ScreenDPI` 即主螢幕倍率；選單尺寸與玻璃效果（邊緣、陰影）都依此放大，
  單螢幕 100%–200% 外觀一致（已在 100% / 125% / 150% 驗證版面）。
- 多螢幕且倍率不同時，Windows 會把整個視窗點陣縮放到另一台螢幕（大小正確、略糊）。
- `GlassMenu({scale: 1.5})` / `LiquidGlass(..., {scale: 1.5})` 可強制倍率，方便在 100% 螢幕上預覽。

## 編譯

### MinGW-w64（預設，`bin\x64` 內的 dll 即以此編譯）

Linux 交叉編譯（需 `g++-mingw-w64-x86-64-posix`），或 Windows 上的 MSYS2 / [w64devkit](https://github.com/skeeto/w64devkit)
（已用 w64devkit 2.11.0、GCC 16.2 驗證）。Windows 上把 w64devkit 的 `bin` 加到 PATH 後執行：

```sh
./build-mingw.sh
```

輸出 `bin/x64/LiquidGlass.dll`，靜態連結 CRT，只依賴 user32、gdi32、gdiplus、dwmapi。

### MSVC（選用：防毒軟體誤判 MinGW 版時使用）

功能與 MinGW 版相同；部分防毒的啟發式掃描對 MSVC 編譯的檔案較寬鬆。需要 Visual Studio 的
「使用 C++ 的桌面開發」工作負載（**含 Windows 10/11 SDK** 與 CMake）：

```bat
cmake -S native -B build\msvc -A x64
cmake --build build\msvc --config Release
```

同樣輸出到 `bin\x64\LiquidGlass.dll`，靜態連結 CRT（`/MT`），開啟 `/guard:cf`、`/DYNAMICBASE`、`/NXCOMPAT`，
並嵌入完整的版本資訊資源；不加殼、不壓縮。

## 目錄

```
native/     C++ 原始碼（glass、shape、canvas、api）、.def、版本資源、CMakeLists.txt
ahk/        LiquidGlass.ahk（低階包裝類）、GlassMenu.ahk（玻璃選單）
examples/   通用範例
bin/x64/    編譯好的 LiquidGlass.dll
```

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
- `BaseLuma` 改收一般矩形參數，並把文字可讀性保底算進去，讓自動文字顏色與實際畫面一致。
- 新增 `extern "C"` 匯出 API（`native/api.cpp`）、內容圖層（`native/canvas.cpp`）、非阻塞動畫、
  顯示中重截、例外防護與錯誤訊息。
- 原作的 `SetFlip`（文字反色保底）程式碼保留，第一版未接上。
- 未包含 TaskbarPopup 的任務欄功能（視窗列表、縮圖、托盤、音量等）。
