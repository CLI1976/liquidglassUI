# GlassMenu 選項說明

`GlassMenu` 的所有選項都在建立時用物件傳入，全部可省略：

```ahk
#Include <GlassMenu>

m := GlassMenu({transparency: 70, showMs: 500, accent: 0xC6F0FD})
m.Add("Calc", (*) => Run("calc.exe"))
m.Show()
```

尺寸類選項一律是 96 dpi 下的邏輯像素，程式會自動乘上 DPI 縮放。

## 外觀

| 選項 | 預設 | 說明 |
|---|---|---|
| `light` | `"auto"` | `true` 亮色、`false` 暗色。設成 `"auto"` 時：有 `accent` 就依它的亮度決定，沒有就跟隨系統主題 |
| `style` | `"liquid"` | `"liquid"` 液態玻璃，或 `"acrylic"`（Win11 壓克力） |
| `transparency` | `60` | 0–100，越大越透明。**只對 `liquid` 有效** |
| `accent` | `""`（預設底色） | 玻璃底色 `0xRRGGBB`。寫成 `0xAARRGGBB` 也可以，但 AA 會被忽略 |
| `textColor` | `"auto"` | 文字顏色 `0xAARRGGBB`，例如 `0xFFFFFFFF`。`"auto"` 會依每一列實際的玻璃亮度，自動選深色字（`0xFF1A1A1A`）或白字 |
| `radius` | `16` | 圓角半徑 |

## 尺寸與字型

| 選項 | 預設 | 說明 |
|---|---|---|
| `iconSize` | `32` | 圖示大小 |
| `rowHeight` | `44` | 每一列高度 |
| `fontSize` | `15` | 字級 |
| `font` | `""` | 字型名稱，空字串就用預設字型 |
| `minWidth` | `180` | 選單最小寬度 |
| `maxWidth` | `520` | 選單最大寬度 |
| `scale` | `A_ScreenDPI / 96` | 自訂 DPI 縮放，例如 `1.5` 可預覽 150 % 的效果 |

## 動畫與時間（毫秒）

| 選項 | 預設 | 說明 |
|---|---|---|
| `anim` | `"slide"` | 開啟動畫：`"slide"` 滑入加淡入、`"zoom"` 從中心放大、`""` 不要動畫 |
| `showMs` | `850` | 開啟動畫時間 |
| `cancelMs` | `800` | 取消時的淡出時間（點外面、Esc、Win 鍵、切換視窗） |
| `invokeMs` | `200` | 點選項目後，先淡出這段時間再執行 callback |
| `subDelay` | `250` | 滑鼠停留多久才開啟或關閉子選單 |

## 注意事項

- **子選單不會繼承父選單的選項**。每個 `GlassMenu` 物件只讀自己的 options，外觀、尺寸、`anim`、`showMs`、`subDelay` 要各自設定。
- **`cancelMs` 和 `invokeMs` 看的是根選單的設定**，因為整組選單是由根選單一起關閉的。
- 只關掉單一子選單時（例如滑鼠移到別的項目），淡出時間是 `Min(cancelMs, 300)`，也就是最多 300 ms。
- `transparency` 調高時，如果玻璃和背景的明暗差很多（例如亮色玻璃在深色背景上），程式會自動把底色加深，讓文字看得清楚，所以實際效果可能沒有設定值那麼透。這個機制寫在 `native/glass.cpp` 的 `TintNow()`，要改就得重新編譯 DLL。

## 完整範例

```ahk
#Requires AutoHotkey v2.0 64-bit
#Include <GlassMenu>

sub := GlassMenu({accent: 0xC6F0FD, transparency: 70, showMs: 400})
sub.Add("Notepad", (*) => Run("notepad.exe"))

m := GlassMenu({
    light: "auto",
    style: "liquid",
    transparency: 70,
    accent: 0xC0F3A3,
    iconSize: 32,
    rowHeight: 44,
    fontSize: 15,
    radius: 16,
    anim: "slide",
    showMs: 500,
    cancelMs: 400,
    invokeMs: 150,
    subDelay: 200
})
m.Add("Open", sub)
m.Add()
m.Add("Calc", (*) => Run("calc.exe"))
m.SetIcon("Calc", A_WinDir "\System32\calc.exe", 1)

^!m:: m.Show()
```
