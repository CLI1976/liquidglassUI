; demo_anim.ahk - stage 3: animations and recapture while shown.
;
;   F1  在滑鼠位置滑入 + 淡入（180 ms）
;   F2  在滑鼠位置從中心縮放（160 ms）
;   F3  淡出（120 ms）
;   F4  顯示中重截一次背景
;   F6  連續重截開關（每 300 ms 一次）：截到自己的話玻璃會越來越暗、越來越花
;   F7 / F8  動畫時間 -50 / +50 ms（三種動畫一起調）
;   Esc 結束
#Requires AutoHotkey v2.0 64-bit
#SingleInstance Force
#Include ..\ahk\LiquidGlass.ahk

CoordMode "Mouse", "Screen"
CoordMode "ToolTip", "Screen"

S(v) => Round(v * A_ScreenDPI / 96)

rows := [
    [A_WinDir "\notepad.exe", 1, "記事本"],
    [A_WinDir "\System32\imageres.dll", 4, "資料夾"],
    [A_WinDir "\System32\imageres.dll", 194, "imageres.dll #194"],
    [A_WinDir "\System32\shell32.dll", 22, "控制台"],
]
pad := S(8), rowH := S(44), iconSize := S(32)
panelW := S(260), panelH := pad * 2 + rowH * rows.Length

lg := LiquidGlass(panelW, panelH, {light: "auto", style: "liquid", radius: S(18)})
auto := false, recaptures := 0
slideMs := 850, zoomMs := 830, fadeMs := 800
Info("F1 滑入　F2 縮放　F3 淡出　F4 重截　F6 連續重截　F7/F8 調速　Esc 結束")

Info(msg) => ToolTip("LiquidGlass 第 3 階段　滑入 " slideMs " / 縮放 " zoomMs " / 淡出 " fadeMs " ms`n" msg, 8, 8)

Speed(delta) {
    global slideMs, zoomMs, fadeMs
    slideMs := Max(50, slideMs + delta)
    zoomMs := Max(50, zoomMs + delta)
    fadeMs := Max(50, fadeMs + delta)
    Info("F7 變快　F8 變慢")
}

Draw() {
    lg.Clear()
    for i, r in rows {
        y := pad + (i - 1) * rowH
        lg.Icon(r[1], r[2], pad + S(8), y + (rowH - iconSize) // 2, iconSize)
        lg.Text(r[3], pad + S(8) + iconSize + S(12), y, panelW - iconSize - S(40), rowH, {size: S(15), valign: "center"})
    }
}

; Top-left at the mouse, kept inside the monitor's work area.
PosAtMouse(&x, &y) {
    MouseGetPos &mx, &my
    mon := 1
    loop MonitorGetCount()
        if MonitorGetWorkArea(A_Index, &l, &t, &r, &b) && mx >= l && mx < r && my >= t && my < b
            mon := A_Index
    MonitorGetWorkArea mon, &l, &t, &r, &b
    x := Min(Max(mx, l), r - panelW)
    y := Min(Max(my, t), b - panelH)
}

Open(anim, ms) {
    if lg.Visible
        lg.Hide()
    PosAtMouse(&x, &y)
    lg.Capture(x, y)
    Draw()                       ; draw after capture so "auto" text color sees the real background
    t0 := A_TickCount
    frames := lg.Show(x, y, anim, ms)
    el := A_TickCount - t0
    Info(anim " " ms " ms：實際 " el " ms，" frames " 幀（約 " Round(frames * 1000 / Max(el, 1)) " fps）")
}

Recapture() {
    global recaptures
    if !lg.Visible
        return
    lg.Capture(lg.X, lg.Y)       ; panel stays on screen; dll re-presents with the new background
    recaptures += 1
    Info("顯示中重截 " recaptures " 次（玻璃應該保持一樣清透，不會越來越暗）")
}

F1:: Open("slide", slideMs)
F2:: Open("zoom", zoomMs)
F3:: lg.Hide("fade", fadeMs)
F7:: Speed(-50)
F8:: Speed(50)
F4:: Recapture()
F6:: {
    global auto := !auto
    SetTimer Recapture, auto ? 300 : 0
    Info("連續重截：" (auto ? "開" : "關"))
}
Esc:: ExitApp
