; LiquidGlass.ahk - low-level wrapper around LiquidGlass.dll (AutoHotkey v2, x64)
; Copyright (C) 2026 LiquidGlass contributors. AGPL-3.0-or-later.
;
; All coordinates and sizes are physical pixels. Content coordinates are relative to the
; panel's top-left corner (the shadow margin is handled by the dll).
;
;   lg := LiquidGlass(300, 200, {radius: 22, light: "auto", style: "liquid", transparency: 60})
;   lg.Show(x, y, "slide", 850)  ; capture background at (x, y), slide in
;   lg.Update()          ; re-compose without recapturing
;   lg.Hide("fade", 800)
#Requires AutoHotkey v2.0 64-bit

class LiquidGlass {
    ; Override before creating the first panel if the dll lives elsewhere.
    static DllPath := A_LineFile "\..\..\bin\x64\LiquidGlass.dll"

    static _hMod := 0
    static _procs := Map()

    ; ---------------------------------------------------------------- dll plumbing
    static _Load() {
        if this._hMod
            return
        if A_PtrSize != 8
            throw Error("LiquidGlass 只支援 64-bit AutoHotkey（請用 AutoHotkey64.exe 執行）")
        path := this.DllPath
        if !FileExist(path)
            throw Error("找不到 LiquidGlass.dll：" path "`n可用 LiquidGlass.DllPath 指定位置。")
        h := DllCall("LoadLibraryW", "WStr", path, "Ptr")
        if !h
            throw OSError(A_LastError, -1, "無法載入 LiquidGlass.dll：" path)
        this._hMod := h
    }

    static _Proc(name) {
        if this._procs.Has(name)
            return this._procs[name]
        this._Load()
        p := DllCall("GetProcAddress", "Ptr", this._hMod, "AStr", name, "Ptr")
        if !p
            throw Error("LiquidGlass.dll 缺少匯出函式 " name "（dll 與 LiquidGlass.ahk 版本不符？）")
        this._procs[name] := p
        return p
    }

    static LastError() => DllCall(this._Proc("LG_LastError"), "WStr")

    static _Check(result, fn) {
        if !result
            throw Error(fn " 失敗：" LiquidGlass.LastError(), -2)
        return result
    }

    ; ---------------------------------------------------------------- helpers
    static _Opt(opts, name, default) => (IsObject(opts) && opts.HasProp(name)) ? opts.%name% : default

    ; AppsUseLightTheme: 1 = light (Windows default when the value is missing)
    static SystemIsLight() {
        try return RegRead("HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme") != 0
        return true
    }

    static Luma(rgb) => 0.299 * ((rgb >> 16) & 0xFF) + 0.587 * ((rgb >> 8) & 0xFF) + 0.114 * (rgb & 0xFF)

    ; light: true / false / "auto". With a custom tint, "auto" follows the tint's brightness.
    static _ResolveLight(light, tint := "") {
        if light = "auto"
            return (tint != "") ? LiquidGlass.Luma(tint & 0xFFFFFF) >= 128 : LiquidGlass.SystemIsLight()
        return !!light
    }

    static _StyleId(style) {
        switch style, false {
            case "liquid", 0: return 0
            case "acrylic", "win11", 1: return 1
        }
        throw ValueError("未知的 style：" style "（可用 liquid / acrylic）", -2)
    }

    ; ---------------------------------------------------------------- instance
    Ptr := 0
    Gui := ""
    Hwnd := 0
    PanelW := 0
    PanelH := 0
    Light := false
    Style := "liquid"
    Tint := ""
    _lightOpt := "auto"
    _capX := ""
    _capY := ""
    X := ""
    Y := ""

    __New(panelW, panelH, opts := {}) {
        this.PanelW := panelW
        this.PanelH := panelH
        this.Style := LiquidGlass._Opt(opts, "style", "liquid")
        this.Tint := LiquidGlass._Opt(opts, "tint", "")
        this._lightOpt := LiquidGlass._Opt(opts, "light", "auto")
        this.Light := LiquidGlass._ResolveLight(this._lightOpt, this.Tint)

        ; Layered (WS_EX_LAYERED 0x80000), never takes focus (WS_EX_NOACTIVATE 0x08000000).
        ; The dll shows the window itself (SWP_SHOWWINDOW | SWP_NOACTIVATE); do not call Gui.Show().
        this.Gui := Gui("-Caption +ToolWindow +AlwaysOnTop +E0x80000 +E0x08000000")
        this.Hwnd := this.Gui.Hwnd

        flags := (this.Light ? 1 : 0) | (LiquidGlass._StyleId(this.Style) ? 2 : 0)
        this.Ptr := DllCall(LiquidGlass._Proc("LG_Create"), "Ptr", this.Hwnd, "Int", panelW, "Int", panelH, "Int", flags, "Ptr")
        if !this.Ptr {
            err := LiquidGlass.LastError()
            this.Gui.Destroy()
            this.Gui := ""
            throw Error("LG_Create 失敗：" err)
        }
        if (r := LiquidGlass._Opt(opts, "radius", "")) != ""
            this.SetRadius(r)
        if this.Tint != ""
            this.SetTint(this.Tint)
        if (t := LiquidGlass._Opt(opts, "transparency", "")) != ""
            this.SetTransparency(t)
    }

    __Delete() => this.Destroy()

    Destroy() {
        this._StopAnim()
        if this.Ptr {
            try DllCall(LiquidGlass._Proc("LG_Destroy"), "Ptr", this.Ptr, "Int")
            this.Ptr := 0
        }
        if IsObject(this.Gui) {
            try this.Gui.Destroy()
            this.Gui := ""
        }
    }

    ; Shadow margin around the panel (physical px); window = panel + 2 * Margin.
    Margin => DllCall(LiquidGlass._Proc("LG_Margin"), "Ptr", this.Ptr, "Int")
    ; DPI scale the dll uses for its effect constants (1.0 = 96 dpi).
    Scale => DllCall(LiquidGlass._Proc("LG_Scale"), "Ptr", this.Ptr, "Float")

    ; Corner radius in physical px; "" or < 0 = style default.
    SetRadius(radius) {
        r := (radius = "") ? -1.0 : radius + 0.0
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_SetShape"), "Ptr", this.Ptr, "Int", 0
            , "Float", r, "Float", 0.0, "Float", 0.0, "Float", 0.0, "Int"), "LG_SetShape")
    }

    ; light: true / false / "auto"
    SetLight(light) {
        this._lightOpt := light
        this.Light := LiquidGlass._ResolveLight(light, this.Tint)
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_SetLight"), "Ptr", this.Ptr, "Int", this.Light, "Int"), "LG_SetLight")
    }

    ; style: "liquid" / "acrylic"
    SetStyle(style) {
        id := LiquidGlass._StyleId(style)
        this.Style := style
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_SetStyle"), "Ptr", this.Ptr, "Int", id, "Int"), "LG_SetStyle")
    }

    ; rgb: 0xRRGGBB, or "" for the default tint. Re-evaluates light when it is "auto".
    SetTint(rgb := "") {
        this.Tint := rgb
        custom := rgb != ""
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_SetTint"), "Ptr", this.Ptr, "Int", custom
            , "UInt", custom ? rgb & 0xFFFFFF : 0, "Int"), "LG_SetTint")
        if this._lightOpt = "auto"
            this.SetLight("auto")
    }

    ; 0 - 100, default 60 (liquid style only)
    SetTransparency(pct) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_SetTransparency"), "Ptr", this.Ptr, "Int", Integer(pct), "Int"), "LG_SetTransparency")
    }

    Resize(panelW, panelH) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Resize"), "Ptr", this.Ptr, "Int", panelW, "Int", panelH, "Int"), "LG_Resize")
        this.PanelW := panelW
        this.PanelH := panelH
        this._capX := this._capY := ""
    }

    ; Captures the background where the panel's top-left will be (screen px).
    Capture(x, y) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Capture"), "Ptr", this.Ptr, "Int", x, "Int", y, "Int"), "LG_Capture")
        this._capX := x
        this._capY := y
    }

    ; Shows the panel with its top-left at (x, y). Captures first when the position changed.
    ; anim: "" (none), "slide" (slide in + fade in), "zoom" (grow from center). Blocking.
    ; Returns the number of frames drawn.
    Show(x, y, anim := "", ms := 850) {
        this._StopAnim()
        if (x != this._capX || y != this._capY)
            this.Capture(x, y)
        this.X := x
        this.Y := y
        switch anim, false {
            case "", "none":
                return LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Present"), "Ptr", this.Ptr, "Int", x, "Int", y, "Int"), "LG_Present")
            case "slide": kind := 1
            case "zoom", "scale": kind := 3
            default: throw ValueError("未知的動畫：" anim "（可用 slide / zoom）", -1)
        }
        return LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Animate"), "Ptr", this.Ptr, "Int", kind, "Int", ms, "Int"), "LG_Animate")
    }

    ; True while the panel window is on screen.
    Visible => IsObject(this.Gui) && DllCall("IsWindowVisible", "Ptr", this.Hwnd, "Int")

    ; Re-composes glass + content at the current position (after style / content changes).
    Update() {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Update"), "Ptr", this.Ptr, "Int"), "LG_Update")
    }

    ; anim: "" (immediately) or "fade". Blocking.
    Hide(anim := "", ms := 800) {
        if !IsObject(this.Gui)
            return
        this._StopAnim()
        switch anim, false {
            case "", "none": this.Gui.Hide()
            case "fade":
                LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Animate"), "Ptr", this.Ptr, "Int", 2, "Int", ms, "Int"), "LG_Animate")
            default: throw ValueError("未知的動畫：" anim "（可用 fade）", -1)
        }
    }

    ; ---------------------------------------------------------------- non-blocking animation
    ; Frames advance on a shared timer, so hotkeys / mouse / hover keep working meanwhile.
    ; Content changes during a showing animation are picked up by the next frame.
    ; onDone (optional) is called once the animation has finished.

    ShowAsync(x, y, anim := "slide", ms := 850, onDone := "") {
        if (x != this._capX || y != this._capY)
            this.Capture(x, y)
        this.X := x
        this.Y := y
        switch anim, false {
            case "", "none":
                this._StopAnim()
                LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Present"), "Ptr", this.Ptr, "Int", x, "Int", y, "Int"), "LG_Present")
                if onDone
                    onDone()
                return
            case "slide": kind := 1
            case "zoom", "scale": kind := 3
            default: throw ValueError("未知的動畫：" anim "（可用 slide / zoom）", -1)
        }
        this._StartAnim(kind, ms, onDone)
    }

    HideAsync(ms := 800, onDone := "") => this._StartAnim(2, ms, onDone)

    ; True while a ShowAsync / HideAsync animation is running.
    Animating => this.Ptr && LiquidGlass._running.Has(this.Ptr)

    static _running := Map()   ; Ptr -> {lg, done}
    static _tickFn := ""

    _StartAnim(kind, ms, onDone) {
        r := LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_AnimBegin"), "Ptr", this.Ptr, "Int", kind, "Int", ms, "Int"), "LG_AnimBegin")
        if r = 2 {   ; nothing to animate (already there, or animations are off)
            this._StopAnim()
            if onDone
                onDone()
            return
        }
        if !LiquidGlass._running.Count {
            DllCall("winmm\timeBeginPeriod", "UInt", 1, "UInt")   ; ~1 ms timer resolution for smooth frames
            if !LiquidGlass._tickFn
                LiquidGlass._tickFn := ObjBindMethod(LiquidGlass, "_Tick")
            SetTimer LiquidGlass._tickFn, 10
        }
        LiquidGlass._running[this.Ptr] := {lg: this, done: onDone}
        LiquidGlass._StepOne(this.Ptr)   ; first frame right away
    }

    _StopAnim() {
        if this.Ptr && LiquidGlass._running.Has(this.Ptr) {
            LiquidGlass._running.Delete(this.Ptr)
            LiquidGlass._MaybeStopTimer()
        }
    }

    static _StepOne(ptr) {
        if !this._running.Has(ptr)
            return
        e := this._running[ptr]
        r := DllCall(this._Proc("LG_AnimStep"), "Ptr", ptr, "Int")
        if r = 1
            return
        this._running.Delete(ptr)
        this._MaybeStopTimer()
        if r = 0
            OutputDebug "LiquidGlass: LG_AnimStep 失敗：" this.LastError()
        if e.done
            e.done.Call()
    }

    static _Tick() {
        for ptr in [this._running*]   ; snapshot of keys: callbacks may start / stop animations
            this._StepOne(ptr)
    }

    static _MaybeStopTimer() {
        if this._running.Count || !this._tickFn
            return
        SetTimer this._tickFn, 0
        DllCall("winmm\timeEndPeriod", "UInt", 1, "UInt")
    }

    ; Width in px of single-line text drawn with Text() using the same font options.
    static MeasureText(text, size := 15, bold := false, font := "") {
        w := DllCall(this._Proc("LG_MeasureText"), "WStr", String(text), "WStr", font, "Int", size, "Int", bold ? 1 : 0, "Int")
        if w < 0
            throw Error("LG_MeasureText 失敗：" this.LastError())
        return w
    }

    ; ---------------------------------------------------------------- content layer
    ; Panel-relative physical pixels, colors 0xAARRGGBB. Call Update() / Show() to display.

    Clear() {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Clear"), "Ptr", this.Ptr, "Int"), "LG_Clear")
    }

    ; Semi-transparent round rect (hover / selection).
    Rect(x, y, w, h, r := 0, argb := 0x33FFFFFF) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_FillRoundRect"), "Ptr", this.Ptr, "Int", x, "Int", y
            , "Int", w, "Int", h, "Int", r, "UInt", argb, "Int"), "LG_FillRoundRect")
    }

    ; Separator line. argb "auto" = subtle line matching light / dark glass.
    Line(x1, y1, x2, y2, width := 1, argb := "auto") {
        if argb = "auto"
            argb := this.Light ? 0x26000000 : 0x33FFFFFF
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Line"), "Ptr", this.Ptr, "Int", x1, "Int", y1
            , "Int", x2, "Int", y2, "Int", width, "UInt", argb, "Int"), "LG_Line")
    }

    ; opts: font ("Microsoft JhengHei UI"), size (15 px), bold, align (left/center/right),
    ;       valign (top/center/bottom), ellipsis (true), wrap (false), color ("auto" or 0xAARRGGBB)
    Text(text, x, y, w, h, opts := {}) {
        flags := 0
        if LiquidGlass._Opt(opts, "bold", false)
            flags |= 0x01
        switch LiquidGlass._Opt(opts, "align", "left"), false {
            case "center": flags |= 0x02
            case "right": flags |= 0x04
        }
        switch LiquidGlass._Opt(opts, "valign", "top"), false {
            case "center": flags |= 0x08
            case "bottom": flags |= 0x40
        }
        if LiquidGlass._Opt(opts, "ellipsis", true)
            flags |= 0x10
        if !LiquidGlass._Opt(opts, "wrap", false)
            flags |= 0x20
        color := LiquidGlass._Opt(opts, "color", "auto")
        if color = "auto"
            color := this.AutoTextColor(x, y, w, h)
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Text"), "Ptr", this.Ptr, "WStr", String(text)
            , "Int", x, "Int", y, "Int", w, "Int", h, "WStr", LiquidGlass._Opt(opts, "font", "")
            , "Int", LiquidGlass._Opt(opts, "size", 15), "Int", flags, "UInt", color, "Int"), "LG_Text")
    }

    ; Dark text on light glass, white text on dark glass (judged from the actual glass below).
    AutoTextColor(x, y, w, h) {
        try luma := this.BaseLuma(x, y, w, h)
        catch
            luma := this.Light ? 255 : 0
        return luma >= 128 ? 0xFF1A1A1A : 0xFFFFFFFF
    }

    ; Icon from .ico / .exe / .dll. index follows AHK: 1 = first icon, negative = resource ID.
    Icon(file, index, x, y, size) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_IconFile"), "Ptr", this.Ptr, "WStr", file, "Int", index
            , "Int", x, "Int", y, "Int", size, "Int"), "LG_IconFile")
    }

    ; Draws an HICON (32-bit alpha preserved). The caller keeps ownership of hIcon.
    IconHandle(hIcon, x, y, size) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Icon"), "Ptr", this.Ptr, "Ptr", hIcon
            , "Int", x, "Int", y, "Int", size, "Int"), "LG_Icon")
    }

    ; PNG or any GDI+ image. w / h = 0 keeps the aspect ratio (both 0 = natural size).
    Image(file, x, y, w := 0, h := 0) {
        LiquidGlass._Check(DllCall(LiquidGlass._Proc("LG_Image"), "Ptr", this.Ptr, "WStr", file
            , "Int", x, "Int", y, "Int", w, "Int", h, "Int"), "LG_Image")
    }

    ; Average brightness (0 - 255) of the glass in a panel-relative rect.
    BaseLuma(x, y, w, h) {
        v := DllCall(LiquidGlass._Proc("LG_BaseLuma"), "Ptr", this.Ptr, "Int", x, "Int", y, "Int", w, "Int", h, "Float")
        if v < 0
            throw Error("LG_BaseLuma 失敗：" LiquidGlass.LastError())
        return v
    }
}
