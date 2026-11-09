; demo_content.ahk - stage 2: content layer on glass (text, icons, PNG, round rects, separators).
;
;   滑鼠移到列上：圓角半透明高亮
;   F1–F4 切換深淺色 / 液態 / Acrylic（文字顏色自動跟著換）
;   F5 重新截取背景　Esc 結束
#Requires AutoHotkey v2.0 64-bit
#SingleInstance Force
#Include ..\ahk\LiquidGlass.ahk

CoordMode "Mouse", "Screen"
CoordMode "ToolTip", "Screen"

S(v) => Round(v * A_ScreenDPI / 96)   ; logical px -> physical px

assets := A_ScriptDir "\assets"
rows := [
    {icon: [assets "\sample.ico", 1],                 text: "自訂 .ico 圖示（多尺寸、半透明）"},
    {icon: [A_WinDir "\notepad.exe", 1],              text: "記事本 Notepad（exe 圖示）"},
    {icon: [A_WinDir "\System32\imageres.dll", 194],  text: "imageres.dll 第 194 個圖示"},
    {sep: true},
    {png: assets "\sample.png",                       text: "PNG 圖片（alpha 漸層）"},
    {icon: [A_WinDir "\System32\shell32.dll", 4],     text: "粗體文字 Bold", bold: true},
    {icon: [A_WinDir "\System32\imageres.dll", 3],    text: "很長很長的中文標題，用來測試超出寬度時最後面會不會正確顯示省略號"},
]

pad := S(8), rowH := S(44), sepH := S(13), iconSize := S(32)
panelW := S(340)
panelH := pad * 2
for r in rows
    panelH += r.HasProp("sep") ? sepH : rowH

modes := [
    {name: "深色 液態玻璃", light: false, style: "liquid"},
    {name: "淺色 液態玻璃", light: true,  style: "liquid"},
    {name: "深色 Acrylic",  light: false, style: "acrylic"},
    {name: "淺色 Acrylic",  light: true,  style: "acrylic"},
]

lg := LiquidGlass(panelW, panelH, {light: "auto", style: "liquid", radius: S(18)})
MonitorGetWorkArea(MonitorGetPrimary(), &waL, &waT, &waR, &waB)
px := waL + (waR - waL - panelW) // 2
py := waT + (waB - waT - panelH) // 2
hover := 0

lg.Capture(px, py)
Draw()
lg.Show(px, py)
ToolTip "LiquidGlass 第 2 階段：內容層`nF1–F4 切換風格　F5 重截　Esc 結束", 8, 8
SetTimer TrackHover, 30

; Rebuilds the whole content layer; ~1 ms for this panel, so hover just redraws everything.
Draw() {
    lg.Clear()
    y := pad
    for i, r in rows {
        if r.HasProp("sep") {
            lg.Line(pad + S(8), y + sepH // 2, panelW - pad - S(8), y + sepH // 2)
            y += sepH
            continue
        }
        if i = hover
            lg.Rect(pad, y, panelW - 2 * pad, rowH, S(10), lg.Light ? 0x1F000000 : 0x2EFFFFFF)
        ix := pad + S(8), iy := y + (rowH - iconSize) // 2
        if r.HasProp("png")
            lg.Image(r.png, ix, iy, iconSize, iconSize)
        else
            lg.Icon(r.icon[1], r.icon[2], ix, iy, iconSize)
        tx := ix + iconSize + S(12)
        lg.Text(r.text, tx, y, panelW - pad - S(10) - tx, rowH
            , {size: S(15), valign: "center", bold: r.HasProp("bold")})
        y += rowH
    }
}

RowAt(mx, my) {
    if mx < px + pad || mx >= px + panelW - pad
        return 0
    y := py + pad
    for i, r in rows {
        h := r.HasProp("sep") ? sepH : rowH
        if my >= y && my < y + h
            return r.HasProp("sep") ? 0 : i
        y += h
    }
    return 0
}

TrackHover() {
    global hover
    MouseGetPos &mx, &my
    i := RowAt(mx, my)
    if i != hover {
        hover := i
        Draw()
        lg.Update()
    }
}

SetMode(i) {
    m := modes[i]
    lg.SetStyle(m.style)
    lg.SetLight(m.light)
    Draw()          ; text colors follow the new glass brightness
    lg.Update()
}

F1:: SetMode(1)
F2:: SetMode(2)
F3:: SetMode(3)
F4:: SetMode(4)
F5:: {
    lg.Capture(px, py)
    Draw()
    lg.Show(px, py)
}
Esc:: ExitApp
