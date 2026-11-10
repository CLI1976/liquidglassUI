; demo_radial.ahk - circle, ring and sector glass menus (GlassRadial)
;   Ctrl + right click   circle menu (with a submenu; its center goes back)
;   F1 / F2 / F3         circle / ring / sector menu at the mouse
;   Esc in a menu        back / close
#Requires AutoHotkey v2.0 64-bit
#SingleInstance Force
#Include ..\lib\GlassRadial.ahk

sys := A_WinDir "\System32"

Tip(name, *) {
    ToolTip "選了：" name
    SetTimer () => ToolTip(), -1500
}

MakeFolders(opts) {
    m := GlassRadial(opts)
    for item in [["文件", 108], ["下載", 176], ["桌面", 106], ["本機", 105], ["資源回收筒", 50]] {
        m.Add(item[1], Tip)
        m.SetIcon(item[1], sys "\imageres.dll", item[2])
    }
    return m
}

; ---- circle: icons around, hovered name in the center
folders := MakeFolders({shape: "circle", accent: 0xF8D86E, title: "資料夾"})
circle := GlassRadial({shape: "circle", radius: 150, title: "LiquidGlass"})
circle.Add("記事本", (*) => Run("notepad.exe"))
circle.SetIcon("記事本", A_WinDir "\notepad.exe")
circle.Add("小算盤", (*) => Run("calc.exe"))
circle.SetIcon("小算盤", sys "\calc.exe")
circle.Add("資料夾", folders)
circle.SetIcon("資料夾", sys "\imageres.dll", 4)
circle.Add("控制台", Tip)
circle.SetIcon("控制台", sys "\shell32.dll", 22)
circle.Add("小畫家", Tip)
circle.SetIcon("小畫家", sys "\mspaint.exe")
circle.Add("停用項目", Tip, "Disabled")
circle.SetIcon("停用項目", sys "\imageres.dll", 3)

; ---- ring: icon + name in the band
ring := MakeFolders({shape: "ring", radius: 140, innerRadius: 56, accent: 0xC6F0FD})
ring.Add("記事本", (*) => Run("notepad.exe"))
ring.SetIcon("記事本", A_WinDir "\notepad.exe")

; ---- sector: a fan opening upwards from the mouse
fan := GlassRadial({shape: "sector", radius: 190, innerRadius: 50, start: -60, sweep: 120, accent: 0xC0F3A3})
for name in ["上一首", "播放", "下一首", "靜音"]
    fan.Add(name, Tip)

<^RButton:: circle.Show()
F1:: circle.Show()
F2:: ring.Show()
F3:: fan.Show()
