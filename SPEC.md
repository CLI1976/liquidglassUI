# LiquidGlass UI for AHK v2 — 需求規格

Nov 9, 2026 · @CLI1976

## 目標與範圍

做一個可重複使用的 AHK v2 液態玻璃 UI library（`LiquidGlass.dll` 加上 `LiquidGlass.ahk` 包裝類）。第一個應用是把現有的 `Quickmenu.ahk`（Ctrl+右鍵彈出的兩層選單）改用這套 UI 重做。

**要做**

- 液態玻璃材質：沿用 [DM74164/TaskbarPopup](https://github.com/DM74164/TaskbarPopup) 的 `src/glass.cpp` 演算法，包含背景模糊、飽和度、邊緣折射與色散、高光、陰影，以及 Win11 Acrylic 風格。
- 形狀：圓角矩形為主；距離場（SDF）介面先抽象好，之後可加圓形、圓環、扇形。
- 內容層：文字、圖示（.ico、exe 或 dll 的索引）、PNG、半透明圓角色塊（hover 用）、分隔線。
- 動畫：滑入或淡入、淡出；內容跟著玻璃一起移動，背景不動。
- 一個通用的「玻璃選單」元件（`GlassMenu`），API 盡量貼近 AHK 內建 `Menu()`，讓 Quickmenu 幾乎可以照搬。

**不做（第一版）**

- TaskbarPopup 的任務欄功能（視窗列表、縮圖、托盤、音量等）。
- 玻璃即時跟著背景影片更新。
- 環狀或扇形選單：等第二階段再做，第一版只把 SDF 介面留好。
- x86（32-bit）版本：只出 x64，搭配 `AutoHotkey64.exe`。

## 架構

dll 只負責「畫」，視窗、事件、熱鍵都留在 AHK。這樣 `OnEvent`、`OnMessage`、`SetTimer` 都能照常用，dll 也不必管訊息迴圈。

&#91;embedded content: LiquidGlass 架構 · AHK 三層、dll 三個模組\]

玻璃與內容都在 dll 裡合成成一張圖貼上視窗；滑鼠事件直接回到 AHK，dll 不碰訊息迴圈。

- **視窗**：AHK 建立 `Gui("-Caption +ToolWindow +AlwaysOnTop +E0x80000 +E0x08000000")`（分層、不搭焦點），把 hwnd 交給 dll。
- **繪製管線**：截背景 → 模糊與飽和度 → 依形狀算材質（texel）→ 每幀合成玻璃層與內容層 → `UpdateLayeredWindow`。
- **兩個圖層**：玻璃層由 dll 算；內容層是一張預乘 ARGB 的畫布，AHK 透過 dll 的繪圖函式（GDI+）往上畫。動畫時內容層跟面板一起位移。
- **重新截圖**：視窗設 `WDA_EXCLUDEFROMCAPTURE`（Win10 2004+），顯示中也能重截而不會截到自己。舊版 Windows 則先隱藏再截。
- **互動**：AHK 用 `OnMessage` 接 `WM_MOUSEMOVE`、`WM_LBUTTONUP`、`WM_MOUSELEAVE`；命中判斷由 `GlassMenu` 在 AHK 端做（項目都是矩形）。

**檔案結構**

```
LiquidGlass/
  LICENSE                 ← AGPL-3.0（沿用原作）
  README.md               ← 來源、修改說明、編譯方式
  native/
    CMakeLists.txt        ← MSVC x64 為主，MinGW 為備援
    glass.h / glass.cpp   ← 改自 TaskbarPopup src/glass.cpp
    shape.h               ← SDF 形狀介面（RoundRect 先做）
    canvas.cpp            ← 內容層：GDI+ 文字、圖示、色塊
    api.cpp               ← extern "C" 匯出函式
    LiquidGlass.def
  lib/
    LiquidGlass.ahk       ← 低階包裝類（對應 dll）
    GlassMenu.ahk         ← 選單元件（介面仿 Menu()）
    LiquidGlass.dll
  examples/
    demo_panel.ahk
    Quickmenu_glass.ahk   ← 重做的 Quickmenu
```

## DLL C API

全部是 `extern "C"` 匯出，x64 下沒有呼叫慣例問題。座標一律是實體像素；內容座標以面板左上角為原點（不含陰影邊距）。字串是 UTF-16（AHK `"WStr"`），顏色是 `0xAARRGGBB`。回傳值 `int` 為 0 表示失敗，錯誤訊息用 `LG_LastError()` 取得。

| 函式 | 用途 |
| --- | --- |
| `LG_Create(hwnd, panelW, panelH, flags) → handle` | 綁定 AHK 視窗，建立玻璃物件。flags：bit0 淺色、bit1 Win11 Acrylic 風格 |
| `LG_Destroy(h)` | 釋放資源（不動 AHK 的視窗） |
| `LG_Margin(h) → int` | 陰影邊距，約 40×DPI 倍率，讓 AHK 算視窗實際大小 |
| `LG_SetShape(h, kind, p1, p2, p3, p4)` | kind 0 = 圓角矩形（p1 = 圓角半徑）；1–3 留給圓、環、扇形 |
| `LG_SetLight(h, light)` / `LG_SetStyle(h, style)` | 深淺色 / 液態玻璃或 Acrylic |
| `LG_SetTint(h, custom, rgb)` / `LG_SetTransparency(h, pct)` | 自訂底色 / 透明度 0–100，預設 60 |
| `LG_Resize(h, panelW, panelH)` | 改變面板大小（材質重算） |
| `LG_Capture(h, panelX, panelY)` | 在面板將出現的螢幕位置截背景並處理 |
| `LG_Clear(h)` | 清空內容層 |
| `LG_FillRoundRect(h, x, y, w, h, r, argb)` | 半透明圓角色塊（hover、選取） |
| `LG_Line(h, x1, y1, x2, y2, width, argb)` | 分隔線 |
| `LG_Text(h, text, x, y, w, h, font, sizePx, flags, argb)` | flags：粗體、左中右對齊、垂直置中、過長省略號 |
| `LG_Icon(h, hIcon, x, y, size)` | 畫 HICON，保留 32-bit alpha |
| `LG_IconFile(h, path, index, x, y, size)` | 從 .ico、exe、dll 取圖示的便利函式（內部快取） |
| `LG_Image(h, path, x, y, w, h)` | PNG 或其他 GDI+ 支援的圖片 |
| `LG_Present(h, x, y)` | 合成後在螢幕 (x, y) 顯示，無動畫 |
| `LG_Animate(h, kind, ms)` | 1 = 滑入加淡入、2 = 淡出、3 = 從中心縮放；阻塞式，結束才返回 |
| `LG_Update(h)` | 只重合成不重截（hover 時用） |
| `LG_BaseLuma(h, x, y, w, h) → float` | 某區塊玻璃的平均亮度，給 AHK 決定文字用黑或白 |
| `LG_LastError() → WStr` | 最後的錯誤訊息 |

**實作重點**

- GDI+ 在第一次 `LG_Create` 時才啟動，不在 `DllMain` 裡啟動（會碰到 loader lock）。
- 圖示：用 `GetIconInfo` 加 `GetDIBits` 讀出 alpha，有 alpha 就當作 ARGB 畫，沒有才退回 `Bitmap::FromHICON`。
- 文字用 `TextRenderingHintAntiAliasGridFit`：ClearType 在透明底上會破。
- 每幀 `Render` 沿用原作的多執行緒分區；`LG_Update` 目標在 400×600 面板上低於 8 ms。
- DPI 倍率用 `GetDpiForWindow`（動態載入，取不到就是 1.0），只用來放大邊緣、陰影這些效果常數。
- 原作的 `SetFlip`（文字反色保底）第一版不接，程式碼保留。

## AHK v2 包裝類

分兩層：`LiquidGlass` 是低階的玻璃面板，對應 dll；`GlassMenu` 建在它上面，介面仿 AHK 內建 `Menu()`。

**LiquidGlass（低階）**

```ahk
lg := LiquidGlass(300, 200, {radius: 22, light: "auto", style: "liquid", transparency: 60})
lg.Capture(x, y)                  ; 在將出現的位置截背景
lg.Clear()
lg.Rect(8, 8, 284, 40, 10, 0x33FFFFFF)
lg.Icon("imageres.dll", 194, 16, 12, 32)
lg.Text("Open", 56, 8, 200, 40, {size: 15, color: "auto", valign: "center"})
lg.Show(x, y, "slide", 180)
lg.Update()                       ; hover 重畫後呼叫
lg.Hide("fade", 120)
lg.Hwnd                           ; 給 OnMessage 用
```

- `light: "auto"`：讀 `HKCU\...\Themes\Personalize\AppsUseLightTheme` 跟隨系統深淺色。
- `color: "auto"`：淺色玻璃用深色字，深色玻璃用白字。
- dll 路徑預設找 `A_LineFile\..\LiquidGlass.dll`，可用 `LiquidGlass.DllPath` 覆寫。載入失敗時丟出明確錯誤，不要靜默。
- `__Delete` 時呼叫 `LG_Destroy` 並銷毀 Gui。

**GlassMenu（選單元件）**

```ahk
m := GlassMenu({light: "auto", iconSize: 32, accent: 0xFFC0F3A3})
m.Add("Open", subMenu)                          ; 子選單
m.Add("Notepad++", Runsoft01)                   ; 回呼和 Menu() 一樣 (ItemName, ItemPos, MenuObj)
m.Add()                                          ; 分隔線
m.SetIcon("Notepad++", path "\notepad++.exe", 1)
m.Show()                                         ; 預設在滑鼠位置
```

- 方法：`Add`、`SetIcon`、`Delete`、`Disable` / `Enable`、`Show(x?, y?)`、`Close()`。名稱和參數順序跟 `Menu()` 一致。
- `accent` 取代原本 `SetColor`：作為玻璃底色，每個子選單可以各自不同，保留原本三層三色的區分。
- 子選單在父項目右側展開；右側空間不夠就開在左側。下緣超出螢幕就往上推。

## 個人選單移植（不在 repo 內）

把現有以 `Menu()` 寫成的個人快捷選單（Ctrl+右鍵彈出、兩層子選單）改用 `GlassMenu` 重做，只替換外觀：
所有回呼函式、路徑變數、其他熱鍵與托盤圖示都不動。個人版本放在 `local/`，不進 repo；
repo 內的 `examples/demo_menu.ahk` 用通用項目示範同樣的結構。

**選單結構**

- 主選單與兩個子選單，各自保留原本 `SetColor` 的底色（綠、藍、黃），改由 `accent` 參數控制。
- 圖示來源有三種，都要支援：資料夾下的 .ico、exe 本身、`imageres.dll` 加索引。

**互動需求**

- `<^RButton` 在滑鼠位置彈出主選單，滑入加淡入。
- 滑鼠停在項目上：圓角半透明高亮；停在子選單項目上約 250 ms 就展開，子選單是另一塊玻璃面板（用自己的底色）。
- 左鍵放開才觸發。先收起所有選單、動畫結束後再呼叫回呼，避免新開的視窗被玻璃蓋住。
- 鍵盤：↑↓ 移動、→ 或 Enter 展開子選單、← 收回、Enter 執行、Esc 關閉。彈出期間用 `HotIf` 條件熱鍵攔截，關閉後失效。
- 點選單外任何地方、或按下 Win、Alt+Tab，整組選單淡出關閉。視窗不搭焦點，所以用 `~LButton` / `~RButton` 熱鍵判斷點擊在不在選單內。
- 彈出不搭焦點：原本的前景視窗不會失去焦點。
- 圖示大小可設定。原本是 48，建議玻璃版預設 32，列高約 44 px（以 100% DPI 計）。

**開放問題**

- 是否保留三層三色的底色，還是統一成系統深淺色？預設保留，由 `accent` 參數控制。

## 非功能需求

| 項目 | 要求 |
| --- | --- |
| 環境 | Windows 10 2004+ 與 Windows 11；AutoHotkey v2.0+ x64 |
| 效能 | 熱鍵到選單開始出現 < 60 ms；動畫 ≥ 60 fps；hover 重畫 < 8 ms |
| DPI | 100%–200% 外觀一致；多螢幕且倍率不同時，依選單所在螢幕計算 |
| 穩定性 | dll 內不得有未處理的例外傳到 AHK；截圖失敗時退回純色底（原作用 `0xFF202024`） |
| 依賴 | 只連結系統 dll（user32、gdi32、gdiplus、dwmapi）；靜態連結 CRT，不需要 VC++ 執行階段 |
| 防毒 | 不加殼、不壓縮、不從記憶體載入；優先用 MSVC 編譯；版本資訊資源填完整；發布前丟 VirusTotal |
| 授權 | AGPL-3.0；保留原作版權與 LICENSE；README 註明「改自 DM74164/TaskbarPopup src/glass.cpp」及修改內容 |
| 分層原則 | library repo 不得含任何私人路徑或流程；個人選單的移植版另外保存（`local/`），repo 內的範例改用通用項目（記事本、小算盤） |

## 實作階段與驗收

分五個階段，每個階段結束都要能跑、能看到效果，再進下一步。

1. **dll 骨架**：沿用 `glass.cpp`，加上 `LG_Create` / `LG_Capture` / `LG_Present` / `LG_Destroy`，先用 MinGW 編譯一版。
   - [ ] `demo_panel.ahk` 顯示一塊空的圓角液態玻璃，邊緣看得到折射與高光
   - [ ] 深色、淺色、Acrylic 三種風格都正確
2. **內容層**：文字、圖示、圖片、色塊、分隔線。
   - [ ] 中文文字邊緣乾淨，沒有黑邊
   - [ ] .ico、exe、`imageres.dll` 的圖示都保留透明度
3. **動畫與重截**：`LG_Animate`、`LG_Update`、`WDA_EXCLUDEFROMCAPTURE`。
   - [ ] 滑入時背景不動、面板與內容一起移動，視覺上無掉幀
   - [ ] 顯示中重截不會截到自己
4. **GlassMenu**：項目、分隔線、子選單、hover、鍵盤、點外部關閉。
   - [ ] API 與 `Menu()` 的 `Add` / `SetIcon` 用法一致
   - [ ] 子選單在螢幕右緣、下緣會自動換邊
   - [ ] 彈出後前景視窗不失去焦點
5. **個人選單移植與發布準備**：MSVC 編譯設定、README、LICENSE。
   - [ ] 原本選單的所有項目（含子選單入口）全部可用，行為跟舊版一樣
   - [ ] 125% 與 150% DPI 下外觀正確
   - [ ] 在目標電腦上試跑，防毒沒有擋下

## 給 Claude Code 的開工指示

開一個空的 repo，把這份文件匯出成 `SPEC.md` 放在根目錄，再貼上下面這段：

```
請依 SPEC.md 實作 LiquidGlass library。

1. git clone https://github.com/DM74164/TaskbarPopup 到 third_party/（只讀），
   參考 src/glass.cpp、src/common.h 的 Glass 類別、src/util.cpp 的 CaptureScreen 與 ColorLuma。
2. 依 SPEC 的「實作階段」一次做一個階段；每個階段結束就 commit，
   並告訴我要在 Windows 上跑哪個範例、看什麼。
3. 編譯：先用 MinGW x64 交叉編譯（-static -O2），第 5 階段再補 MSVC 的 CMake 設定。
4. AHK 程式碼一律 AHK v2 語法，每個 DllCall 都標明型別（"Ptr"、"Int"、"WStr"、"UInt"）。
5. 保留 AGPL-3.0 LICENSE 與原作版權，README 寫明來源與修改。
6. examples/ 只放通用範例；我的 Quickmenu 移植版放在 local/（加入 .gitignore）。
```

移植 Quickmenu 時，把原本的 `Quickmenu.ahk` 一起放進 `local/`，讓 Claude Code 對照著改。Windows 上的實際測試要你自己跑，因為他的環境是 Linux，只能編譯不能執行。
