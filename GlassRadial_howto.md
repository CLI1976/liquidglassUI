# GlassRadial 使用說明

`GlassRadial` 是圓形、圓環、扇形的液態玻璃選單，用法和 `GlassMenu` 一樣（介面仿 AHK 內建 `Menu()`）。

```ahk
#Include <GlassRadial>

r := GlassRadial({shape: "circle", radius: 150, title: "工具"})
r.Add("記事本", (*) => Run("notepad.exe"))
r.SetIcon("記事本", A_WinDir "\notepad.exe")
<^RButton:: r.Show()
```

## 三種形狀

| `shape` | 外觀 | 名稱顯示（`labels: "auto"`） |
|---|---|---|
| `"circle"`（預設） | 實心圓，中央是返回 / 關閉區 | hover 項目的名稱顯示在中央 |
| `"ring"` | 中空圓環，中央透明 | 名稱畫在圖示下方 |
| `"sector"` | 扇形，從頂點（滑鼠位置）展開 | 名稱畫在圖示下方 |

別名：`"pie"` = circle、`"donut"` = ring、`"fan"` = sector。

## 方法

| 方法 | 說明 |
|---|---|
| `Add(name, callback)` | 加入項目，回呼參數同 `Menu()`：`(ItemName, ItemPos, MenuObj)` |
| `Add(name, glassRadial)` | 子選單：在原位置開啟，點中央返回 |
| `Add()` | 空格，佔一個位置但不放項目 |
| `Add(name, target, "Disabled")` | 停用的項目 |
| `SetIcon(item, file, index := 1)` | .ico、exe 或 dll 的圖示，index 規則同 AHK；`file` 給 `""` 移除圖示 |
| `Delete(item?)` | 刪除一個項目，不給參數就全部刪除 |
| `Disable(item)` / `Enable(item)` / `ToggleEnable(item)` | 停用、啟用 |
| `Show(x?, y?)` | 在螢幕座標顯示，預設在滑鼠位置。座標是**圓心**（扇形是**頂點**） |
| `Close()` / `GlassRadial.CancelAll()` | 關閉整組選單 |
| `GlassRadial.IsOpen` | 有選單開著時為 true |

`item` 可以是名稱（不分大小寫），也可以是 `"N&"` 這種位置寫法，和 `Menu()` 相同。

## 操作方式

- **滑鼠**：移到環帶上的項目會亮起，左鍵放開時執行。點中央會返回上一層，在最上層則關閉。點選單外面也會關閉。
- **鍵盤**：`←` `↑` `Shift+Tab` 選上一個，`→` `↓` `Tab` 選下一個，`Enter` 執行，`Esc` / `Backspace` 返回或關閉。
- 按 Win 鍵或切換前景視窗（Alt+Tab）都會關閉選單。
- 選單不搶焦點，原本的視窗保持在前景。

## 選項

尺寸類選項一律是 96 dpi 下的邏輯像素，程式會自動乘上 DPI 縮放。角度單位是度，**0 = 12 點鐘方向，順時針增加**。

### 形狀與尺寸

| 選項 | 預設 | 說明 |
|---|---|---|
| `shape` | `"circle"` | `"circle"`、`"ring"`、`"sector"` |
| `radius` | `140` | 外徑 |
| `innerRadius` | 圓形 / 圓環 `52`、扇形 `44` | 項目的內緣。圓形：中央區大小；圓環：中間洞的大小；扇形：頂點附近的空白。最大到 `radius - 24` |
| `start` | `-60` | 扇形起始角度（只對 sector 有效） |
| `sweep` | `120` | 扇形張開的角度，10–360（只對 sector 有效） |
| `corner` | `-1` | 扇形玻璃的圓角半徑，`-1` 表示依風格自動決定（液態 14、Acrylic 6） |
| `gap` | `4` | hover 色塊之間的間距 |
| `dividers` | `true` | 項目之間是否畫分隔線 |

- 圓形、圓環會把項目平均分配在 360° 上，**第一個項目的正中央在 12 點鐘方向**。
- 扇形從 `start` 開始，順時針平均分配到 `start + sweep`。例如 `start: -60, sweep: 120` 是往上張開的 120° 扇形；`start: 90, sweep: 90` 則從 3 點鐘方向往 6 點鐘方向張開。
- 扇形的 `innerRadius` 小於 `radius` 的 15% 時，玻璃會變成完整的扇形（沒有內缺口）。

### 外觀

| 選項 | 預設 | 說明 |
|---|---|---|
| `light` | `"auto"` | `true` 亮色、`false` 暗色。設成 `"auto"` 時：有 `accent` 就依它的亮度決定，沒有就跟隨系統主題 |
| `style` | `"liquid"` | `"liquid"` 液態玻璃，或 `"acrylic"`（Win11 壓克力） |
| `transparency` | `60` | 0–100，越大越透明。**只對 `liquid` 有效** |
| `accent` | `""`（預設底色） | 玻璃底色 `0xRRGGBB` |
| `textColor` | `"auto"` | 文字顏色 `0xAARRGGBB`。`"auto"` 依文字底下的玻璃亮度自動選深色或白色 |

### 圖示與文字

| 選項 | 預設 | 說明 |
|---|---|---|
| `iconSize` | `32` | 圖示大小 |
| `fontSize` | `13` | 字級 |
| `font` | `""` | 字型名稱，空字串就用預設字型 |
| `labels` | `"auto"` | `"center"`：hover 項目的名稱顯示在中央（只有圓形有中央區）；`"item"`：名稱畫在圖示下方；`"none"`：不顯示名稱；`"auto"`：圓形用 center，其他用 item |
| `title` | `""` | 圓形選單在沒有 hover 時，中央顯示的標題 |

- 沒有圖示的項目一定會顯示名稱（除非 `labels: "none"`）。
- 子選單的中央在沒有 hover 時會顯示 `‹`，表示點這裡返回。
- 子選單項目的外緣會有一小段弧線標記。

### 動畫與時間（毫秒）

| 選項 | 預設 | 說明 |
|---|---|---|
| `anim` | `"zoom"` | 開啟動畫：`"zoom"` 從圓心（扇形是頂點）放大、`"slide"` 滑入、`""` 不要動畫 |
| `showMs` | `500` | 開啟動畫時間 |
| `invokeMs` | `200` | 點選項目後，先淡出這段時間再執行 callback |
| `cancelMs` | `400` | 取消時的淡出時間（點外面、Esc、Win 鍵、切換視窗） |
| `scale` | `A_ScreenDPI / 96` | 自訂 DPI 縮放，例如 `1.5` 可預覽 150 % 的效果 |

## 注意事項

- **子選單不會繼承父選單的選項**。每個 `GlassRadial` 只讀自己的 options，形狀、大小、顏色可以和父選單完全不同。
- **`cancelMs` 和 `invokeMs` 看的是最上層選單的設定**。
- 進入或返回子選單時，舊的選單固定用 150 ms 淡出，新的選單用自己的 `anim` / `showMs` 開啟。
- 選單靠近螢幕邊緣時會整個往內推，這時圓心就不會剛好在滑鼠位置。
- 只有滑鼠在環帶上才會選到項目。滑鼠移到選單外面時，不會依方向自動選取。
- 停用的項目只有文字變淡，圖示仍是全彩。
- `GlassRadial` 和 `GlassMenu` 可以在同一個腳本裡並用，兩者各自管理自己開啟的選單。

## 完整範例

```ahk
#Requires AutoHotkey v2.0 64-bit
#Include <GlassRadial>

sys := A_WinDir "\System32"

; 子選單：圓環
folders := GlassRadial({shape: "ring", radius: 140, innerRadius: 56, accent: 0xF8D86E})
folders.Add("文件", (*) => Run(A_MyDocuments))
folders.SetIcon("文件", sys "\imageres.dll", 108)
folders.Add("下載", (*) => Run(EnvGet("USERPROFILE") "\Downloads"))
folders.SetIcon("下載", sys "\imageres.dll", 176)

; 主選單：圓形，hover 名稱顯示在中央
main := GlassRadial({
    shape: "circle",
    radius: 150,
    innerRadius: 52,
    title: "工具",
    transparency: 70,
    anim: "zoom",
    showMs: 400,
    cancelMs: 300
})
main.Add("記事本", (*) => Run("notepad.exe"))
main.SetIcon("記事本", A_WinDir "\notepad.exe")
main.Add("小算盤", (*) => Run("calc.exe"))
main.SetIcon("小算盤", sys "\calc.exe")
main.Add("資料夾", folders)
main.SetIcon("資料夾", sys "\imageres.dll", 4)

; 扇形：從滑鼠位置往上張開
fan := GlassRadial({shape: "sector", radius: 190, innerRadius: 50, start: -60, sweep: 120})
for name in ["上一首", "播放", "下一首"]
    fan.Add(name, (itemName, *) => ToolTip(itemName))

<^RButton:: main.Show()
F3:: fan.Show()
```

## 低階形狀（LiquidGlass）

要自己畫面板時，可以直接設定 `LiquidGlass` 的形狀。座標是面板內的實體像素：

```ahk
lg := LiquidGlass(300, 300)
lg.SetCircle()                                     ; 內切圓
lg.SetRing(80)                                     ; 內切圓環，洞的半徑 80
lg.SetSector(150, 280, 260, 60, -45, 90)           ; 圓心 (150, 280)、外徑 260、內徑 60、從 -45° 張開 90°
lg.FillSector(150, 150, 140, 80, -30, 60, 10, 0x33FFFFFF)   ; 內容層的扇形色塊（hover 用）
```

`SetSector` 的 `inner` 給 0 就是完整的扇形，`sweep` 給 360 就是圓環。
