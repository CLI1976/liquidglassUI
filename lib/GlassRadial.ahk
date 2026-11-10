; GlassRadial.ahk - liquid glass radial menu (circle / ring / sector) with a Menu()-like interface
; (AutoHotkey v2, x64)
; Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
;
;   sub := GlassRadial({shape: "ring"})
;   sub.Add("Notepad", (name, pos, menu) => Run("notepad.exe"))
;   r := GlassRadial({shape: "circle", radius: 150})
;   r.Add("Apps", sub)                          ; submenu: opens in place, the center goes back
;   r.Add("Calc", (*) => Run("calc.exe"))
;   r.SetIcon("Calc", A_WinDir "\System32\calc.exe", 1)
;   r.Show()                                    ; centered on the mouse
;
; Items are laid out clockwise around the center, the first one at 12 o'clock (circle / ring)
; or at `start` (sector). Hover an item and click to run it; clicking the center (or Esc /
; Backspace) goes back to the parent menu or closes; clicking elsewhere closes. Keyboard:
; ← ↑ / → ↓ / Tab move, Enter runs. Like GlassMenu, the panels never take focus.
;
; Options (all optional; sizes are logical px at 96 dpi, angles in degrees, 0 = 12 o'clock, clockwise):
;   shape "circle" | "ring" | "sector"           light "auto" | true | false
;   style "liquid" | "acrylic"                   transparency 0-100 (60)   accent 0xRRGGBB   textColor "auto"
;   radius 140 (outer)    innerRadius 52 (circle / ring) or 44 (sector): where the items start
;   start -60   sweep 120 (sector only)          corner -1 (sector glass corner, -1 = style default)
;   iconSize 32   fontSize 13   font ""          labels "auto" | "center" | "item" | "none"
;   title "" (circle: shown in the center when nothing is hovered)
;   gap 4 (px between hover highlights)          dividers true
;   anim "zoom" | "slide" | ""   showMs 500   invokeMs 200   cancelMs 400   scale (A_ScreenDPI / 96)
#Requires AutoHotkey v2.0 64-bit
#Include %A_LineFile%\..\LiquidGlass.ahk

class GlassRadial {
    ; ================================================================ shared session state
    static _open := []            ; menu chain, root first; only the last one is visible
    static _byHwnd := Map()
    static _ready := false
    static _closingAll := false
    static _fgWatch := ""
    static _fgHwnd := 0

    static IsOpen => GlassRadial._open.Length > 0

    static _Init() {
        if GlassRadial._ready
            return
        GlassRadial._ready := true
        OnMessage 0x0200, ObjBindMethod(GlassRadial, "_OnMouseMove")    ; WM_MOUSEMOVE
        OnMessage 0x0202, ObjBindMethod(GlassRadial, "_OnLButtonUp")    ; WM_LBUTTONUP
        OnMessage 0x02A3, ObjBindMethod(GlassRadial, "_OnMouseLeave")   ; WM_MOUSELEAVE
        GlassRadial._fgWatch := ObjBindMethod(GlassRadial, "_WatchForeground")

        HotIf (*) => GlassRadial._open.Length > 0 && !GlassRadial._closingAll
        key := ObjBindMethod(GlassRadial, "_OnKey")
        for k in ["Up", "Down", "Left", "Right", "Tab", "+Tab", "Enter", "NumpadEnter", "Esc", "Backspace"]
            Hotkey k, key
        click := ObjBindMethod(GlassRadial, "_OnClickAnywhere")
        for k in ["~*LButton", "~*RButton", "~*MButton"]
            Hotkey k, click
        cancel := (*) => GlassRadial.CancelAll()
        for k in ["~LWin Up", "~RWin Up"]
            Hotkey k, cancel
        HotIf
    }

    ; ================================================================ instance
    __New(opts := {}) {
        this.Opts := opts
        this._items := []
        this._lg := ""
        this._hover := 0          ; item index, -1 = center, 0 = nothing
        this._parent := ""
        this._isOpen := false
        this._closing := false
        this._tracking := false
        this._dirty := true
        this._colors := []
        this.X := 0, this.Y := 0, this.W := 0, this.H := 0
        this.CX := 0, this.CY := 0   ; center on screen
    }

    _O(name, default) => (IsObject(this.Opts) && this.Opts.HasProp(name)) ? this.Opts.%name% : default
    _Scale() => this._O("scale", A_ScreenDPI / 96)

    _Shape() {
        s := this._O("shape", "circle")
        switch s, false {
            case "circle", "pie": return "circle"
            case "ring", "donut": return "ring"
            case "sector", "fan": return "sector"
        }
        throw ValueError("GlassRadial：未知的 shape：" s "（可用 circle / ring / sector）", -2)
    }

    __Delete() {
        if IsObject(this._lg) {
            GlassRadial._byHwnd.Delete(this._lg.Hwnd)
            this._lg.Destroy()
        }
    }

    ; ---------------------------------------------------------------- Menu()-like API
    ; Add()                        empty slot (keeps a position free)
    ; Add(name, callback)          callback(ItemName, ItemPos, MenuObj)
    ; Add(name, glassRadial)       submenu
    Add(name := "", target := "", options := "") {
        if name = "" {
            this._items.Push({sep: true, name: "", sub: "", cb: "", icon: "", disabled: false})
        } else {
            if !(target is GlassRadial) && !HasMethod(target)
                throw ValueError("GlassRadial.Add：第二個參數要是回呼函式或 GlassRadial 子選單（" name "）", -1)
            idx := this._Index(name, false)
            if idx {
                it := this._items[idx]
            } else {
                it := {sep: false, name: name, icon: "", disabled: false}
                this._items.Push(it)
            }
            it.sub := target is GlassRadial ? target : ""
            it.cb := target is GlassRadial ? "" : target
            if InStr(options, "Disabled")
                it.disabled := true
        }
        this._dirty := true
    }

    SetIcon(item, file, index := 1, *) {
        it := this._items[this._Index(item)]
        it.icon := file = "" ? "" : {file: file, index: index}
        this._dirty := true
    }

    Delete(item?) {
        if !IsSet(item)
            this._items := []
        else
            this._items.RemoveAt(this._Index(item))
        this._hover := 0
        this._dirty := true
    }

    Disable(item) => this._SetDisabled(item, true)
    Enable(item) => this._SetDisabled(item, false)
    ToggleEnable(item) => this._SetDisabled(item, !this._items[this._Index(item)].disabled)

    _SetDisabled(item, state) {
        this._items[this._Index(item)].disabled := state
        if this._isOpen
            this._Redraw()
    }

    ; Show(x?, y?): screen position of the center (the apex for a sector); default = mouse.
    Show(x?, y?) {
        GlassRadial._Init()
        if GlassRadial._open.Length
            GlassRadial._CloseAll(0)
        GlassRadial._closingAll := false
        CoordMode "Mouse", "Screen"
        MouseGetPos &mx, &my
        GlassRadial._fgHwnd := WinExist("A")
        GlassRadial._open := [this]
        this._parent := ""
        this._hover := 0
        this._OpenAt(IsSet(x) ? x : mx, IsSet(y) ? y : my)
        SetTimer GlassRadial._fgWatch, 100
    }

    Close() => GlassRadial.CancelAll()

    static CancelAll() {
        if GlassRadial._open.Length && !GlassRadial._closingAll
            GlassRadial._CloseAll(GlassRadial._open[1]._O("cancelMs", 400))
    }

    _Index(item, mustExist := true) {
        if item is Integer
            idx := item
        else if RegExMatch(item, "^(\d+)&$", &m)
            idx := Integer(m[1])
        else {
            idx := 0
            for i, it in this._items
                if !it.sep && it.name = item {
                    idx := i
                    break
                }
        }
        if idx < 1 || idx > this._items.Length {
            if !mustExist
                return 0
            throw TargetError("找不到選單項目：" item, -2)
        }
        return idx
    }

    _Selectable(i) => i >= 1 && i <= this._items.Length && !this._items[i].sep && !this._items[i].disabled

    _Step(from, dir) {
        n := this._items.Length
        i := from > 0 ? from : (dir > 0 ? 0 : 1)
        loop n {
            i := Mod(i - 1 + dir + n, n) + 1
            if this._Selectable(i)
                return i
        }
        return 0
    }

    ; ---------------------------------------------------------------- geometry
    ; Compass angle (0 = up, clockwise) -> unit vector on screen (y down).
    static _Dir(deg, &ux, &uy) {
        a := deg * 0.017453292519943295
        ux := Sin(a), uy := -Cos(a)
    }

    static _Wrap(deg) => deg - Floor(deg / 360) * 360

    ; ATan2 only exists from AutoHotkey v2.1 on.
    static _Atan2(y, x) {
        if x > 0
            return ATan(y / x)
        if x < 0
            return ATan(y / x) + (y >= 0 ? 3.141592653589793 : -3.141592653589793)
        return y > 0 ? 1.5707963267948966 : y < 0 ? -1.5707963267948966 : 0
    }

    _Layout() {
        s := this._Scale()
        L := {}
        L.shape := this._Shape()
        L.R := Round(this._O("radius", 140) * s)
        L.Ri := Round(this._O("innerRadius", L.shape = "sector" ? 44 : 52) * s)
        L.Ri := Max(0, Min(L.Ri, L.R - Round(24 * s)))
        if L.shape = "sector" {
            L.start := this._O("start", -60) + 0.0
            L.sweep := Max(10, Min(360, this._O("sweep", 120) + 0.0))
        } else {
            L.sweep := 360.0
            n := Max(1, this._items.Length)
            L.start := -L.sweep / n / 2   ; the first item centered at 12 o'clock
        }
        L.n := Max(1, this._items.Length)
        L.step := L.sweep / L.n
        L.iconSz := Round(this._O("iconSize", 32) * s)
        L.fontPx := Round(this._O("fontSize", 13) * s)
        L.font := this._O("font", "")
        L.gap := this._O("gap", 4) * s
        L.corner := this._O("corner", -1)
        L.corner := L.corner < 0 ? -1 : L.corner * s
        L.hlCorner := Round(10 * s)
        labels := this._O("labels", "auto")
        if labels = "auto"
            labels := L.shape = "circle" ? "center" : "item"
        L.labels := labels
        L.midR := (L.R + L.Ri) / 2

        ; bounding box of the outer shape, relative to the center
        minX := 0, minY := 0, maxX := 0, maxY := 0
        pts := []
        for deg in [L.start, L.start + L.sweep]
            for rad in [L.R, L.Ri] {
                GlassRadial._Dir(deg, &ux, &uy)
                pts.Push([ux * rad, uy * rad])
            }
        for deg in [0, 90, 180, 270]
            if L.sweep >= 360 || GlassRadial._Wrap(deg - L.start) <= L.sweep {
                GlassRadial._Dir(deg, &ux, &uy)
                pts.Push([ux * L.R, uy * L.R])
            }
        minX := maxX := pts[1][1], minY := maxY := pts[1][2]
        for p in pts
            minX := Min(minX, p[1]), maxX := Max(maxX, p[1]), minY := Min(minY, p[2]), maxY := Max(maxY, p[2])
        ; keep the center zone clickable: include it in the panel
        if L.Ri > 0
            minX := Min(minX, -L.Ri), maxX := Max(maxX, L.Ri), minY := Min(minY, -L.Ri), maxY := Max(maxY, L.Ri)
        L.w := Ceil(maxX - minX), L.h := Ceil(maxY - minY)
        L.cx := -minX, L.cy := -minY   ; center in panel px

        for i, it in this._items {
            it.a0 := L.start + (i - 1) * L.step
            it.mid := it.a0 + L.step / 2
            GlassRadial._Dir(it.mid, &ux, &uy)
            it.px := L.cx + ux * L.midR, it.py := L.cy + uy * L.midR
            it.label := it.sep ? "" : GlassRadial._Label(it.name)
        }
        this._L := L
        this.W := L.w, this.H := L.h
    }

    static _Label(name) {
        s := StrReplace(name, "&&", Chr(1))
        s := StrReplace(s, "&")
        return StrReplace(s, Chr(1), "&")
    }

    _EnsurePanel() {
        if this._dirty || !IsObject(this._lg) || !this.HasProp("_L")
            this._Layout()
        L := this._L
        if !IsObject(this._lg) {
            accent := this._O("accent", "")
            this._lg := LiquidGlass(L.w, L.h, {
                light: this._O("light", "auto"),
                style: this._O("style", "liquid"),
                transparency: this._O("transparency", 60),
                tint: accent = "" ? "" : accent & 0xFFFFFF,
                scale: this._Scale()})
            GlassRadial._byHwnd[this._lg.Hwnd] := this
        } else {
            if Abs(this._lg.Scale - this._Scale()) > 0.001
                this._lg.SetScale(this._Scale())
            if this._lg.PanelW != L.w || this._lg.PanelH != L.h
                this._lg.Resize(L.w, L.h)
        }
        switch L.shape {
            case "circle": this._lg.SetSector(L.cx, L.cy, L.R, 0, 0, 360)
            case "ring": this._lg.SetSector(L.cx, L.cy, L.R, L.Ri, 0, 360)
            default: this._lg.SetSector(L.cx, L.cy, L.R, L.shape = "sector" && L.Ri < L.R * 0.15 ? 0 : L.Ri
                , L.start, L.sweep, L.corner)
        }
        this._dirty := false
    }

    ; ---------------------------------------------------------------- drawing
    _LabelRect(it, &x, &y, &w, &h) {
        L := this._L
        w := Round(Min(L.step / 360 * 6.2832 * L.midR, (L.R - L.Ri) * 1.6))
        h := Round(L.fontPx * 1.5)
        x := Round(it.px - w / 2)
        y := Round(it.py + (it.icon != "" ? L.iconSz / 2 - L.fontPx * 0.35 : -h / 2))
    }

    _CenterRect(&x, &y, &w, &h) {
        L := this._L
        w := Round(L.Ri * 1.6), h := Round(L.fontPx * 3.2)
        x := Round(L.cx - w / 2), y := Round(L.cy - h / 2)
    }

    _ComputeColors() {
        this._colors := []
        tc := this._O("textColor", "auto")
        for it in this._items {
            if it.sep {
                this._colors.Push(0)
                continue
            }
            this._LabelRect(it, &x, &y, &w, &h)
            this._colors.Push(tc != "auto" ? tc : this._lg.AutoTextColor(x, y, w, h))
        }
        if this._L.shape = "circle" {
            this._CenterRect(&x, &y, &w, &h)
            this._centerColor := tc != "auto" ? tc : this._lg.AutoTextColor(x, y, w, h)
        } else
            this._centerColor := this._lg.Light ? 0xFF1A1A1A : 0xFFFFFFFF
    }

    _Draw() {
        lg := this._lg, L := this._L
        lg.Clear()
        light := lg.Light
        hoverColor := light ? 0x1F000000 : 0x2EFFFFFF
        lineColor := light ? 0x1F000000 : 0x29FFFFFF
        ; The hole of a ring / the apex of a sector is transparent; an almost invisible fill keeps
        ; clicks there on this window (center = back / close) instead of passing through.
        if L.Ri > 0 && L.shape != "circle"
            lg.FillSector(L.cx, L.cy, L.Ri, 0, 0, 360, 0, 0x01000000)
        if L.shape = "sector" && L.Ri < L.R * 0.15
            lg.FillSector(L.cx, L.cy, L.R, 0, L.start, L.sweep, 0, 0x01000000)

        inset := L.gap / 2
        gapDeg := L.gap / Max(1, L.midR) * 57.29578
        for i, it in this._items {
            if i = this._hover && this._Selectable(i)
                lg.FillSector(L.cx, L.cy, L.R - inset - 2, L.Ri + inset, it.a0 + gapDeg / 2, L.step - gapDeg, L.hlCorner, hoverColor)
            if this._O("dividers", true) && L.n > 1 && (L.sweep >= 360 || i > 1) {
                GlassRadial._Dir(it.a0, &ux, &uy)
                r0 := L.Ri + L.gap * 2, r1 := L.R - L.gap * 2
                lg.Line(Round(L.cx + ux * r0), Round(L.cy + uy * r0), Round(L.cx + ux * r1), Round(L.cy + uy * r1), 1, lineColor)
            }
            if it.sep
                continue
            color := this._colors.Length >= i ? this._colors[i] : (light ? 0xFF1A1A1A : 0xFFFFFFFF)
            if it.disabled
                color := (color & 0xFFFFFF) | 0x73000000
            ; an item without an icon always shows its name (unless labels = "none")
            showLabel := L.labels = "item" || (L.labels != "none" && it.icon = "")
            if it.icon != "" {
                iy := it.py - L.iconSz / 2 - (showLabel ? L.fontPx * 0.55 : 0)
                try lg.Icon(it.icon.file, it.icon.index, Round(it.px - L.iconSz / 2), Round(iy), L.iconSz)
                catch as e
                    OutputDebug "GlassRadial: " e.Message
            }
            if showLabel {
                this._LabelRect(it, &x, &y, &w, &h)
                lg.Text(it.label, x, y, w, h, {size: L.fontPx, font: L.font, align: "center", valign: "center", color: color})
            }
            if it.sub != "" {   ; submenu: a short arc along the outer edge
                tick := Min(L.step * 0.3, 18)
                lg.FillSector(L.cx, L.cy, L.R - L.gap - 3, L.R - L.gap - 3 - Max(2, Round(3 * this._Scale()))
                    , it.mid - tick / 2, tick, 2, (color & 0xFFFFFF) | 0x99000000)
            }
        }
        ; center: label of the hovered item, the title, or a back arrow in a submenu
        if L.shape = "circle" && L.Ri > 0 {
            if this._hover = -1
                lg.FillSector(L.cx, L.cy, L.Ri - inset, 0, 0, 360, 0, hoverColor)
            text := ""
            if this._hover > 0 && L.labels = "center"
                text := this._items[this._hover].label
            else if this._parent
                text := "‹"
            else
                text := this._O("title", "")
            if text != "" {
                this._CenterRect(&x, &y, &w, &h)
                big := text = "‹"
                lg.Text(text, x, y, w, h, {size: big ? Round(L.fontPx * 2.2) : L.fontPx, font: L.font
                    , align: "center", valign: "center", wrap: !big, bold: !big, color: this._centerColor})
            }
        }
    }

    _Redraw() {
        if !this._isOpen || this._closing
            return
        this._Draw()
        if !this._lg.Animating
            this._lg.Update()
    }

    ; ---------------------------------------------------------------- opening and closing
    _OpenAt(cx, cy) {
        this._EnsurePanel()
        L := this._L
        GlassRadial._WorkArea(cx, cy, &waL, &waT, &waR, &waB)   ; (variable names are case-insensitive: l = L)
        x := Max(waL, Min(Round(cx - L.cx), waR - L.w))
        y := Max(waT, Min(Round(cy - L.cy), waB - L.h))
        this.X := x, this.Y := y
        this.CX := x + L.cx, this.CY := y + L.cy
        this._isOpen := true
        this._closing := false
        this._lg.Capture(x, y)
        this._ComputeColors()
        this._Draw()
        this._lg.ShowAsync(x, y, this._O("anim", "zoom"), this._O("showMs", 500))
    }

    static _WorkArea(x, y, &l, &t, &r, &b) {
        loop MonitorGetCount() {
            MonitorGet A_Index, &ml, &mt, &mr, &mb
            if x >= ml && x < mr && y >= mt && y < mb {
                MonitorGetWorkArea A_Index, &l, &t, &r, &b
                return
            }
        }
        MonitorGetWorkArea MonitorGetPrimary(), &l, &t, &r, &b
    }

    ; Submenus replace their parent at the same center; the center of the submenu goes back.
    _OpenSub(idx) {
        it := this._items[idx]
        sub := it.sub
        if !this._isOpen || this._closing || sub._isOpen
            return
        this._isOpen := false
        this._closing := true
        me := this
        this._lg.HideAsync(150, () => me._closing := false)
        sub._parent := this
        sub._hover := 0
        GlassRadial._open.Push(sub)
        sub._OpenAt(this.CX, this.CY)
    }

    _Back() {
        p := this._parent
        if !p
            return GlassRadial.CancelAll()
        GlassRadial._open.Pop()
        this._isOpen := false
        this._closing := true
        this._parent := ""
        me := this
        this._lg.HideAsync(150, () => me._closing := false)
        p._hover := 0
        p._OpenAt(p.CX, p.CY)
    }

    static _CloseAll(ms, done := "") {
        SetTimer GlassRadial._fgWatch, 0
        menus := GlassRadial._open
        GlassRadial._open := []
        if !menus.Length {
            if done
                SetTimer done, -1
            return
        }
        GlassRadial._closingAll := true
        top := menus[menus.Length]
        for m in menus
            m._isOpen := false, m._parent := "", m._hover := 0
        finished := (*) => (top._closing := false, GlassRadial._closingAll := false, done ? SetTimer(done, -1) : 0)
        top._closing := true
        if ms > 0
            top._lg.HideAsync(ms, finished)
        else {
            top._lg.Hide()
            finished()
        }
    }

    static _Invoke(m, idx) {
        it := m._items[idx]
        cb := it.cb, name := it.name
        GlassRadial._CloseAll(GlassRadial._open[1]._O("invokeMs", 200), () => cb(name, idx, m))
    }

    _Activate(idx) {
        if idx = -1
            return this._Back()
        if !this._Selectable(idx)
            return
        if this._items[idx].sub is GlassRadial
            this._OpenSub(idx)
        else
            GlassRadial._Invoke(this, idx)
    }

    ; ---------------------------------------------------------------- hover
    _SetHover(idx) {
        if idx = this._hover
            return
        this._hover := idx
        this._Redraw()
    }

    ; Panel px -> item index, -1 = center zone, 0 = nothing.
    _HitTest(px, py) {
        L := this._L
        dx := px - L.cx, dy := py - L.cy
        rho := Sqrt(dx * dx + dy * dy)
        if rho < L.Ri
            return -1
        if rho > L.R + 2
            return 0
        deg := GlassRadial._Wrap(GlassRadial._Atan2(dy, dx) * 57.29578 + 90)   ; compass angle
        rel := GlassRadial._Wrap(deg - L.start)
        if rel >= L.sweep
            return 0
        return Min(L.n, Floor(rel / L.step) + 1)
    }

    ; ---------------------------------------------------------------- window messages
    static _FromMsg(hwnd, lParam, &m, &px, &py) {
        if !GlassRadial._byHwnd.Has(hwnd)
            return false
        m := GlassRadial._byHwnd[hwnd]
        margin := m._lg.Margin
        px := (lParam << 48 >> 48) - margin
        py := (lParam << 32 >> 48) - margin
        return true
    }

    static _OnMouseMove(wParam, lParam, msg, hwnd) {
        if !GlassRadial._FromMsg(hwnd, lParam, &m, &px, &py)
            return
        if !m._isOpen || m._closing || GlassRadial._closingAll
            return 0
        if !m._tracking {
            tme := Buffer(24, 0)
            NumPut("UInt", 24, tme, 0), NumPut("UInt", 0x2, tme, 4), NumPut("Ptr", hwnd, tme, 8)   ; TME_LEAVE
            DllCall("TrackMouseEvent", "Ptr", tme, "Int")
            m._tracking := true
        }
        i := m._HitTest(px, py)
        m._SetHover(i = -1 || m._Selectable(i) || (i > 0 && m._items[i].disabled) ? i : 0)
        return 0
    }

    static _OnMouseLeave(wParam, lParam, msg, hwnd) {
        if !GlassRadial._byHwnd.Has(hwnd)
            return
        m := GlassRadial._byHwnd[hwnd]
        m._tracking := false
        if m._isOpen && !m._closing && m._hover
            m._SetHover(0)
        return 0
    }

    static _OnLButtonUp(wParam, lParam, msg, hwnd) {
        if !GlassRadial._FromMsg(hwnd, lParam, &m, &px, &py)
            return
        if !m._isOpen || m._closing || GlassRadial._closingAll
            return 0
        i := m._HitTest(px, py)
        if i = 0
            GlassRadial.CancelAll()
        else
            m._Activate(i)
        return 0
    }

    ; Clicks anywhere: close unless the click is inside the open menu's circle.
    static _OnClickAnywhere(*) {
        CoordMode "Mouse", "Screen"
        MouseGetPos &mx, &my, &win
        m := GlassRadial._open[GlassRadial._open.Length]
        if m._lg.Hwnd = win && m._HitTest(mx - m.X, my - m.Y) != 0
            return
        GlassRadial.CancelAll()
    }

    static _WatchForeground() {
        if !GlassRadial._open.Length {
            SetTimer GlassRadial._fgWatch, 0
            return
        }
        if WinExist("A") != GlassRadial._fgHwnd
            GlassRadial.CancelAll()
    }

    ; ---------------------------------------------------------------- keyboard
    static _OnKey(key) {
        if !GlassRadial._open.Length
            return
        m := GlassRadial._open[GlassRadial._open.Length]
        if !m._isOpen || m._closing
            return
        switch key {
            case "Right", "Down", "Tab":
                m._SetHover(m._Step(m._hover, 1))
            case "Left", "Up", "+Tab":
                m._SetHover(m._Step(m._hover, -1))
            case "Enter", "NumpadEnter":
                if m._hover
                    m._Activate(m._hover)
            case "Esc", "Backspace":
                m._Back()
        }
    }
}
