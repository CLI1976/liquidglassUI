; simple_menu.ahk - the smallest useful GlassMenu: Ctrl + right click anywhere.
#Requires AutoHotkey v2.0 64-bit
#SingleInstance Force
#Include ..\ahk\GlassMenu.ahk

; A submenu with its own glass tint
folders := GlassMenu({accent: 0xF8D86E})
folders.Add("文件", (*) => Run(A_MyDocuments))
folders.Add("桌面", (*) => Run(A_Desktop))
folders.Add("下載", (*) => Run("shell:Downloads"))
folders.SetIcon("文件", "imageres.dll", 108)
folders.SetIcon("桌面", "imageres.dll", 106)
folders.SetIcon("下載", "imageres.dll", 176)

; The main menu: same Add / SetIcon calls as AHK's Menu()
mainMenu := GlassMenu({accent: 0xC0F3A3})
mainMenu.Add("資料夾", folders)
mainMenu.Add()
mainMenu.Add("記事本", (*) => Run("notepad.exe"))
mainMenu.Add("小算盤", (*) => Run("calc.exe"))
mainMenu.Add()
mainMenu.Add("結束範例", (*) => ExitApp())
mainMenu.SetIcon("資料夾", "imageres.dll", 4)
mainMenu.SetIcon("記事本", A_WinDir "\notepad.exe")
mainMenu.SetIcon("小算盤", A_WinDir "\System32\calc.exe")

<^RButton:: mainMenu.Show()
