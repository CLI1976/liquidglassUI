; demo_panel.ahk - stage 1: an empty liquid glass panel.
;
;   F1  深色 液態玻璃        F2  淺色 液態玻璃
;   F3  深色 Acrylic         F4  淺色 Acrylic
;   F5  重新截取背景         F6  透明度循環（20 / 40 / 60 / 80）
;   Ctrl + 方向鍵  移動面板（會重新截背景）
;   Esc 結束
#Requires AutoHotkey v2.0 64-bit
#SingleInstance Force
#Include ..\lib\LiquidGlass.ahk

CoordMode "ToolTip", "Screen"

scale := A_ScreenDPI / 96
panelW := Round(360 * scale)
panelH := Round(220 * scale)
step := Round(60 * scale)

modes := [
    {name: "深色 液態玻璃", light: false, style: "liquid"},
    {name: "淺色 液態玻璃", light: true,  style: "liquid"},
    {name: "深色 Acrylic",  light: false, style: "acrylic"},
    {name: "淺色 Acrylic",  light: true,  style: "acrylic"},
]
transparencies := [20, 40, 60, 80]
tIndex := 3

try {
    lg := LiquidGlass(panelW, panelH, {light: false, style: "liquid", radius: Round(22 * scale)})
} catch as e {
    MsgBox "建立玻璃面板失敗：`n" e.Message, "demo_panel", "Iconx"
    ExitApp
}

MonitorGetWorkArea(MonitorGetPrimary(), &waL, &waT, &waR, &waB)
px := waL + (waR - waL - panelW) // 2
py := waT + (waB - waT - panelH) // 2
lg.Show(px, py)
ShowInfo("深色 液態玻璃")

ShowInfo(name) {
    ; keep the tooltip far from the panel so it is not captured into the glass
    ToolTip "LiquidGlass 第 1 階段：" name "　透明度 " transparencies[tIndex] "%`n"
        . "F1–F4 切換風格　F5 重截　F6 透明度　Ctrl+方向鍵 移動　Esc 結束", 8, 8
}

SetMode(i) {
    m := modes[i]
    lg.SetStyle(m.style)
    lg.SetLight(m.light)
    lg.Update()
    ShowInfo(m.name)
    global current := i
}
current := 1

MovePanel(dx, dy) {
    global px, py
    px += dx
    py += dy
    lg.Show(px, py)   ; position changed -> recapture, then present
}

F1:: SetMode(1)
F2:: SetMode(2)
F3:: SetMode(3)
F4:: SetMode(4)
F5:: {
    lg.Capture(px, py)
    lg.Show(px, py)
}
F6:: {
    global tIndex := Mod(tIndex, transparencies.Length) + 1
    lg.SetTransparency(transparencies[tIndex])
    lg.Update()
    ShowInfo(modes[current].name)
}
^Left::  MovePanel(-step, 0)
^Right:: MovePanel(step, 0)
^Up::    MovePanel(0, -step)
^Down::  MovePanel(0, step)
Esc:: ExitApp
