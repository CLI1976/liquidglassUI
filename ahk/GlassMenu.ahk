; GlassMenu.ahk - liquid glass popup menu with an interface modelled on AHK's Menu() (AutoHotkey v2, x64)
; Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
;
;   sub := GlassMenu({accent: 0xC6F0FD})
;   sub.Add("Notepad", (name, pos, menu) => Run("notepad.exe"))
;   m := GlassMenu({light: "auto", iconSize: 32, accent: 0xFFC0F3A3})
;   m.Add("Open", sub)                         ; submenu
;   m.Add()                                    ; separator
;   m.Add("Calc", (*) => Run("calc.exe"))      ; callback (ItemName, ItemPos, MenuObj), same as Menu()
;   m.SetIcon("Calc", A_WinDir "\System32\calc.exe", 1)
;   m.Show()                                   ; at the mouse
;
; The panels never take focus, so the foreground window keeps it. While a menu is open, the
; arrow keys / Enter / Esc are intercepted with #HotIf-style hotkeys; clicking elsewhere,
; switching windows or pressing Win closes the menu.
;
; Options (all optional; sizes are logical px at 96 dpi):
;   light "auto" | true | false     style "liquid" | "acrylic"     transparency 0-100 (60)
;   accent 0xRRGGBB / 0xAARRGGBB    glass tint (each submenu can have its own)
;   iconSize 32   rowHeight 44   fontSize 15   font ""   minWidth 180   maxWidth 520   radius 16
;   anim "slide" | "zoom" | ""      showMs 850   invokeMs 200 (fade before a callback)   cancelMs 800
;   subDelay 250 (hover time before a submenu opens / closes)
#Requires AutoHotkey v2.0 64-bit
#Include %A_LineFile%\..\LiquidGlass.ahk

class GlassMenu {
    ; ================================================================ shared session state
    static _open := []            ; open menus, root first, deepest last
    static _byHwnd := Map()       ; panel hwnd -> GlassMenu
    static _ready := false
    static _closingAll := false
    static _subTimer := ""        ; pending submenu open / close
    static _fgWatch := ""
    static _fgHwnd := 0

    static IsOpen => GlassMenu._open.Length > 0

    static _Init() {
        if GlassMenu._ready
            return
        GlassMenu._ready := true
        OnMessage 0x0200, ObjBindMethod(GlassMenu, "_OnMouseMove")    ; WM_MOUSEMOVE
        OnMessage 0x0202, ObjBindMethod(GlassMenu, "_OnLButtonUp")    ; WM_LBUTTONUP
        OnMessage 0x02A3, ObjBindMethod(GlassMenu, "_OnMouseLeave")   ; WM_MOUSELEAVE
        GlassMenu._fgWatch := ObjBindMethod(GlassMenu, "_WatchForeground")

        ; Hotkeys that only exist while a menu is open.
        HotIf (*) => GlassMenu._open.Length > 0 && !GlassMenu._closingAll
        key := ObjBindMethod(GlassMenu, "_OnKey")
        for k in ["Up", "Down", "Left", "Right", "Enter", "NumpadEnter", "Esc"]
            Hotkey k, key
        click := ObjBindMethod(GlassMenu, "_OnClickAnywhere")
        for k in ["~*LButton", "~*RButton", "~*MButton"]
            Hotkey k, click
        cancel := (*) => GlassMenu.CancelAll()
        for k in ["~LWin Up", "~RWin Up"]
            Hotkey k, cancel
        HotIf
    }

    ; ================================================================ instance
    __New(opts := {}) {
        this.Opts := opts
        this._items := []
        this._lg := ""
        this._hover := 0
        this._parent := ""
        this._child := ""
        this._childIdx := 0
        this._isOpen := false
        this._closing := false
        this._tracking := false
        this._dirty := true
        this._colors := []
        this.X := 0, this.Y := 0, this.W := 0, this.H := 0
    }

    _O(name, default) => (IsObject(this.Opts) && this.Opts.HasProp(name)) ? this.Opts.%name% : default

    __Delete() {
        if IsObject(this._lg) {
            GlassMenu._byHwnd.Delete(this._lg.Hwnd)
            this._lg.Destroy()
        }
    }

    ; ---------------------------------------------------------------- Menu()-like API
    ; Add()                        separator
    ; Add(name, callback)          callback(ItemName, ItemPos, MenuObj)
    ; Add(name, glassMenu)         submenu
    ; Adding an existing name updates that item, as Menu() does.
    Add(name := "", target := "", options := "") {
        if name = "" {
            this._items.Push({sep: true, name: "", sub: "", cb: "", icon: "", disabled: false})
        } else {
            if !(target is GlassMenu) && !HasMethod(target)
                throw ValueError("GlassMenu.Add：第二個參數要是回呼函式或 GlassMenu 子選單（" name "）", -1)
            idx := this._Index(name, false)
            if idx {
                it := this._items[idx]
            } else {
                it := {sep: false, name: name, icon: "", disabled: false}
                this._items.Push(it)
            }
            it.sub := target is GlassMenu ? target : ""
            it.cb := target is GlassMenu ? "" : target
            if InStr(options, "Disabled")
                it.disabled := true
        }
        this._dirty := true
    }

    ; SetIcon(item, file, index := 1): .ico, exe or dll; index as in AHK (1 = first, negative = resource ID).
    ; Pass "" as file to remove the icon.
    SetIcon(item, file, index := 1, *) {
        it := this._items[this._Index(item)]
        it.icon := file = "" ? "" : {file: file, index: index}
        this._dirty := true
    }

    ; Delete() removes all items; Delete(item) removes one.
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

    ; Show(x?, y?): screen coordinates of the top-left corner; default = mouse position.
    Show(x?, y?) {
        GlassMenu._Init()
        if GlassMenu._open.Length
            GlassMenu._CloseAll(0)
        GlassMenu._closingAll := false   ; reopened while a previous menu was still fading out
        CoordMode "Mouse", "Screen"
        MouseGetPos &mx, &my
        ax := IsSet(x) ? x : mx
        ay := IsSet(y) ? y : my
        GlassMenu._fgHwnd := WinExist("A")
        GlassMenu._open := [this]
        this._parent := ""
        this._hover := 0
        this._OpenRoot(ax, ay)
        SetTimer GlassMenu._fgWatch, 100
    }

    ; Closes the whole menu (fade out).
    Close() => GlassMenu.CancelAll()

    static CancelAll() {
        if GlassMenu._open.Length && !GlassMenu._closingAll
            GlassMenu._CloseAll(GlassMenu._open[1]._O("cancelMs", 800))
    }

    ; ---------------------------------------------------------------- items
    ; item: name (case-insensitive) or "N&" position, as in Menu().
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

    static _Label(name) {
        s := StrReplace(name, "&&", Chr(1))
        s := StrReplace(s, "&")
        return StrReplace(s, Chr(1), "&")
    }

    _Selectable(i) => i >= 1 && i <= this._items.Length && !this._items[i].sep && !this._items[i].disabled

    _Step(from, dir) {
        n := this._items.Length
        i := from
        loop n {
            i := Mod(i - 1 + dir + n, n) + 1
            if !from && dir < 0 && A_Index = 1   ; nothing selected + Up = last item
                i := n
            if this._Selectable(i)
                return i
        }
        return 0
    }

    ; ---------------------------------------------------------------- layout and drawing
    _Layout() {
        s := A_ScreenDPI / 96
        R(v) => Round(v * s)
        L := {}
        L.pad := R(6), L.rowH := R(this._O("rowHeight", 44)), L.sepH := R(11)
        L.iconSz := R(this._O("iconSize", 32)), L.iconL := R(10), L.gap := R(12)
        L.arrowW := R(22), L.rightPad := R(14), L.fontPx := R(this._O("fontSize", 15))
        L.font := this._O("font", ""), L.radius := R(this._O("radius", 16))
        hasIcons := false, hasSub := false, maxText := 0, maxKey := 0
        for it in this._items {
            if it.sep
                continue
            hasIcons := hasIcons || it.icon != ""
            hasSub := hasSub || it.sub != ""
            parts := StrSplit(it.name, "`t", , 2)
            it.label := GlassMenu._Label(parts[1])
            it.keyText := parts.Length > 1 ? parts[2] : ""
            maxText := Max(maxText, LiquidGlass.MeasureText(it.label, L.fontPx, false, L.font))
            if it.keyText != ""
                maxKey := Max(maxKey, LiquidGlass.MeasureText(it.keyText, L.fontPx, false, L.font))
        }
        L.textX := L.pad + L.iconL + (hasIcons ? L.iconSz + L.gap : R(4))
        L.keyW := maxKey ? maxKey + R(24) : 0
        L.tailW := L.rightPad + (hasSub ? L.arrowW : 0)
        w := L.textX + maxText + L.keyW + L.tailW + L.pad
        L.w := Min(Max(w, R(this._O("minWidth", 180))), R(this._O("maxWidth", 520)))
        y := L.pad
        for it in this._items {
            it.y := y
            it.h := it.sep ? L.sepH : L.rowH
            y += it.h
        }
        L.h := y + L.pad
        this._L := L
        this.W := L.w, this.H := L.h
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
                radius: L.radius})
            GlassMenu._byHwnd[this._lg.Hwnd] := this
        } else if this._lg.PanelW != L.w || this._lg.PanelH != L.h {
            this._lg.Resize(L.w, L.h)
        }
        this._dirty := false
    }

    ; Text colors per row from the real glass brightness (needs the background captured).
    _ComputeColors() {
        L := this._L
        this._colors := []
        tc := this._O("textColor", "auto")
        for it in this._items {
            if it.sep {
                this._colors.Push(0)
                continue
            }
            this._colors.Push(tc != "auto" ? tc : this._lg.AutoTextColor(L.textX, it.y, L.w - L.textX - L.pad, it.h))
        }
    }

    _Draw() {
        lg := this._lg, L := this._L
        lg.Clear()
        hoverColor := lg.Light ? 0x1F000000 : 0x2EFFFFFF
        for i, it in this._items {
            if it.sep {
                cy := it.y + it.h // 2
                lg.Line(L.pad + L.iconL, cy, L.w - L.pad - L.iconL, cy)
                continue
            }
            if i = this._hover && !it.disabled
                lg.Rect(L.pad, it.y, L.w - 2 * L.pad, it.h, Round(L.radius * 0.6), hoverColor)
            color := this._colors.Length >= i ? this._colors[i] : (lg.Light ? 0xFF1A1A1A : 0xFFFFFFFF)
            if it.disabled
                color := (color & 0xFFFFFF) | 0x73000000
            if it.icon != "" {
                try lg.Icon(it.icon.file, it.icon.index, L.pad + L.iconL, it.y + (it.h - L.iconSz) // 2, L.iconSz)
                catch as e
                    OutputDebug "GlassMenu: " e.Message
            }
            textW := L.w - L.textX - L.tailW - L.pad - L.keyW
            lg.Text(it.label, L.textX, it.y, textW, it.h, {size: L.fontPx, font: L.font, valign: "center", color: color})
            if it.keyText != ""
                lg.Text(it.keyText, L.w - L.pad - L.tailW - L.keyW, it.y, L.keyW, it.h
                    , {size: L.fontPx, font: L.font, valign: "center", align: "right", color: (color & 0xFFFFFF) | 0x99000000})
            if it.sub != ""
                lg.Text("›", L.w - L.pad - L.rightPad - L.arrowW + Round(L.arrowW * 0.3), it.y, L.arrowW, it.h
                    , {size: Round(L.fontPx * 1.5), font: L.font, valign: "center", color: color})
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

    _OpenRoot(ax, ay) {
        this._EnsurePanel()
        GlassMenu._WorkArea(ax, ay, &l, &t, &r, &b)
        x := ax + this.W > r ? ax - this.W : ax    ; no room on the right: open to the left of the point
        y := ay + this.H > b ? ay - this.H : ay    ; no room below: open upwards
        this._OpenAt(Max(l, Min(x, r - this.W)), Max(t, Min(y, b - this.H)))
    }

    _OpenAsChild(parent, idx) {
        this._EnsurePanel()
        it := parent._items[idx]
        gap := Round(4 * A_ScreenDPI / 96)
        GlassMenu._WorkArea(parent.X + parent.W // 2, parent.Y + it.y, &l, &t, &r, &b)
        x := parent.X + parent.W + gap
        if x + this.W > r                          ; right edge: open on the left of the parent
            x := parent.X - this.W - gap
        y := parent.Y + it.y - this._L.pad        ; first row lines up with the parent item
        if y + this.H > b                          ; bottom edge: push up
            y := b - this.H
        this._OpenAt(Max(l, Min(x, r - this.W)), Max(t, y))
    }

    _OpenAt(x, y) {
        this.X := x, this.Y := y
        this._isOpen := true
        this._closing := false
        this._child := "", this._childIdx := 0
        this._lg.Capture(x, y)
        this._ComputeColors()
        this._Draw()
        anim := this._O("anim", "slide")
        this._lg.ShowAsync(x, y, anim, this._O("showMs", 850))
    }

    _OpenSub(idx, byKeyboard := false) {
        GlassMenu._CancelSubTimer()
        if !this._isOpen || this._closing || !this._Selectable(idx)
            return
        it := this._items[idx]
        if !(it.sub is GlassMenu)
            return
        if this._child == it.sub {
            if byKeyboard && !it.sub._hover
                it.sub._SetHover(it.sub._Step(0, 1))
            return
        }
        this._CloseChild()
        if it.sub._isOpen   ; the same menu object is open elsewhere in the chain
            return
        sub := it.sub
        sub._parent := this
        sub._hover := byKeyboard ? sub._Step(0, 1) : 0
        this._child := sub, this._childIdx := idx
        this._hover := idx
        this._Redraw()
        GlassMenu._open.Push(sub)
        sub._OpenAsChild(this, idx)
    }

    _CloseChild() {
        child := this._child
        if !child
            return
        child._CloseChild()
        this._child := "", this._childIdx := 0
        loop GlassMenu._open.Length
            if GlassMenu._open[A_Index] == child {
                GlassMenu._open.RemoveAt(A_Index)
                break
            }
        child._isOpen := false
        child._closing := true
        child._hover := 0
        child._parent := ""
        child._lg.HideAsync(Min(child._O("cancelMs", 800), 300), () => child._closing := false)
    }

    ; Closes every open menu with a fade of ms, then runs done (if any) in a new thread.
    static _CloseAll(ms, done := "") {
        GlassMenu._CancelSubTimer()
        SetTimer GlassMenu._fgWatch, 0
        menus := GlassMenu._open
        GlassMenu._open := []
        if !menus.Length {
            if done
                SetTimer done, -1
            return
        }
        GlassMenu._closingAll := true
        pending := menus.Length
        finished := (*) => (
            --pending,
            pending = 0 ? (GlassMenu._closingAll := false, done ? SetTimer(done, -1) : 0) : 0
        )
        for m in menus {
            m._isOpen := false
            m._closing := true
            m._child := "", m._childIdx := 0, m._parent := ""
            m._hover := 0
            mm := m
            if ms > 0
                m._lg.HideAsync(ms, GlassMenu._AfterClose.Bind(GlassMenu, mm, finished))
            else {
                m._lg.Hide()
                GlassMenu._AfterClose(mm, finished)
            }
        }
    }

    static _AfterClose(m, finished) {
        m._closing := false
        finished()
    }

    static _Invoke(m, idx) {
        it := m._items[idx]
        cb := it.cb, name := it.name
        GlassMenu._CloseAll(GlassMenu._open[1]._O("invokeMs", 200), () => cb(name, idx, m))
    }

    ; ---------------------------------------------------------------- hover
    _SetHover(idx) {
        if idx = this._hover
            return
        this._hover := idx
        this._Redraw()
        GlassMenu._CancelSubTimer()
        delay := this._O("subDelay", 250)
        if idx && this._Selectable(idx) && this._items[idx].sub is GlassMenu {
            if this._child != this._items[idx].sub
                GlassMenu._SetSubTimer(ObjBindMethod(this, "_OpenSub", idx, false), delay)
        } else if this._child {
            GlassMenu._SetSubTimer(ObjBindMethod(this, "_CloseChild"), delay)
        }
    }

    static _SetSubTimer(fn, delay) {
        GlassMenu._subTimer := fn
        SetTimer fn, -delay
    }

    static _CancelSubTimer() {
        if GlassMenu._subTimer {
            SetTimer GlassMenu._subTimer, 0
            GlassMenu._subTimer := ""
        }
    }

    _HitTest(px, py) {
        L := this._L
        if px < L.pad || px >= L.w - L.pad
            return 0
        for i, it in this._items
            if py >= it.y && py < it.y + it.h
                return i
        return 0
    }

    _InsidePanel(px, py) => px >= 0 && py >= 0 && px < this.W && py < this.H

    ; ---------------------------------------------------------------- window messages
    static _FromMsg(hwnd, lParam, &m, &px, &py) {
        if !GlassMenu._byHwnd.Has(hwnd)
            return false
        m := GlassMenu._byHwnd[hwnd]
        margin := m._lg.Margin
        px := (lParam << 48 >> 48) - margin    ; signed client x - shadow margin = panel x
        py := (lParam << 32 >> 48) - margin
        return true
    }

    static _OnMouseMove(wParam, lParam, msg, hwnd) {
        if !GlassMenu._FromMsg(hwnd, lParam, &m, &px, &py)
            return
        if !m._isOpen || m._closing || GlassMenu._closingAll
            return 0
        if !m._tracking {   ; ask for WM_MOUSELEAVE
            tme := Buffer(24, 0)
            NumPut("UInt", 24, tme, 0), NumPut("UInt", 0x2, tme, 4), NumPut("Ptr", hwnd, tme, 8)   ; TME_LEAVE
            DllCall("TrackMouseEvent", "Ptr", tme, "Int")
            m._tracking := true
        }
        ; Entering a submenu keeps its parent item highlighted and cancels a pending close.
        if p := m._parent {
            GlassMenu._CancelSubTimer()
            if p._hover != p._childIdx {
                p._hover := p._childIdx
                p._Redraw()
            }
        }
        i := m._HitTest(px, py)
        m._SetHover(m._Selectable(i) || (i && m._items[i].disabled) ? i : 0)
        return 0
    }

    static _OnMouseLeave(wParam, lParam, msg, hwnd) {
        if !GlassMenu._byHwnd.Has(hwnd)
            return
        m := GlassMenu._byHwnd[hwnd]
        m._tracking := false
        if !m._isOpen || m._closing
            return 0
        ; Leaving towards an open submenu keeps that item lit; otherwise clear the hover.
        if m._child {
            if m._hover != m._childIdx {
                GlassMenu._CancelSubTimer()
                m._hover := m._childIdx
                m._Redraw()
            }
        } else if m._hover {
            GlassMenu._CancelSubTimer()
            m._hover := 0
            m._Redraw()
        }
        return 0
    }

    static _OnLButtonUp(wParam, lParam, msg, hwnd) {
        if !GlassMenu._FromMsg(hwnd, lParam, &m, &px, &py)
            return
        if !m._isOpen || m._closing || GlassMenu._closingAll
            return 0
        if !m._InsidePanel(px, py) {   ; click on the shadow = outside
            GlassMenu.CancelAll()
            return 0
        }
        i := m._HitTest(px, py)
        if !m._Selectable(i)
            return 0
        if m._items[i].sub is GlassMenu
            m._OpenSub(i, false)
        else
            GlassMenu._Invoke(m, i)
        return 0
    }

    ; ~LButton / ~RButton / ~MButton anywhere: close unless the click is on an open panel.
    static _OnClickAnywhere(*) {
        CoordMode "Mouse", "Screen"
        MouseGetPos &mx, &my, &win
        for m in GlassMenu._open
            if m._lg.Hwnd = win && m._InsidePanel(mx - m.X, my - m.Y)
                return
        GlassMenu.CancelAll()
    }

    static _WatchForeground() {
        if !GlassMenu._open.Length {
            SetTimer GlassMenu._fgWatch, 0
            return
        }
        if WinExist("A") != GlassMenu._fgHwnd   ; Alt+Tab, Start menu, another window activated
            GlassMenu.CancelAll()
    }

    ; ---------------------------------------------------------------- keyboard
    static _OnKey(key) {
        if !GlassMenu._open.Length
            return
        m := GlassMenu._open[GlassMenu._open.Length]   ; the deepest menu has the keyboard
        GlassMenu._CancelSubTimer()
        switch key {
            case "Up", "Down":
                m._SetHover(m._Step(m._hover, key = "Up" ? -1 : 1))
                GlassMenu._CancelSubTimer()   ; keyboard opens submenus only with Right / Enter
            case "Right":
                if m._Selectable(m._hover) && m._items[m._hover].sub is GlassMenu
                    m._OpenSub(m._hover, true)
            case "Left":
                if p := m._parent
                    p._CloseChild()
            case "Enter", "NumpadEnter":
                if !m._Selectable(m._hover)
                    return
                if m._items[m._hover].sub is GlassMenu
                    m._OpenSub(m._hover, true)
                else
                    GlassMenu._Invoke(m, m._hover)
            case "Esc":
                if p := m._parent
                    p._CloseChild()
                else
                    GlassMenu.CancelAll()
        }
    }
}
