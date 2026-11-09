; demo_menu.ahk - stage 4: GlassMenu with submenus, icons, separators and three glass tints.
;
;   Ctrl + 右鍵        在滑鼠位置彈出選單
;   滑鼠停在 ▸ 項目約 250 ms 展開子選單；左鍵放開才執行
;   ↑↓ 移動　→ / Enter 展開子選單　← 收回　Enter 執行　Esc 關閉
;   點選單外、按 Win、Alt+Tab 都會關閉
#Requires AutoHotkey v2.0 64-bit
#SingleInstance Force
#Include ..\ahk\GlassMenu.ahk

sys := A_WinDir "\System32"
assets := A_ScriptDir "\assets"

; ---- submenu 1 (blue)
apps := GlassMenu({accent: 0xC6F0FD})
apps.Add("記事本", (*) => Run("notepad.exe"))
apps.Add("小算盤", (*) => Run("calc.exe"))
apps.Add("小畫家", (*) => Run("mspaint.exe"))
apps.Add("工作管理員`tCtrl+Shift+Esc", (*) => Run("taskmgr.exe"))
apps.Add("命令提示字元", (*) => Run(A_ComSpec, A_MyDocuments))
apps.SetIcon("記事本", A_WinDir "\notepad.exe", 1)
apps.SetIcon("小算盤", sys "\calc.exe", 1)
apps.SetIcon("小畫家", sys "\mspaint.exe", 1)
apps.SetIcon("工作管理員`tCtrl+Shift+Esc", sys "\taskmgr.exe", 1)
apps.SetIcon("命令提示字元", sys "\cmd.exe", 1)

; ---- submenu 2 (yellow)
places := GlassMenu({accent: 0xF8D86E})
places.Add("本機", (*) => Run("explorer.exe ::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"))
places.Add("下載", (*) => Run("shell:Downloads"))
places.Add("文件", (*) => Run(A_MyDocuments))
places.Add("桌面", (*) => Run(A_Desktop))
places.Add()
places.Add("顯示桌面", (*) => (ComObject("Shell.Application").ToggleDesktop()))
places.Add("資源回收筒", (*) => Run("shell:RecycleBinFolder"))
places.SetIcon("本機", sys "\imageres.dll", 105)
places.SetIcon("下載", sys "\imageres.dll", 176)
places.SetIcon("文件", sys "\imageres.dll", 108)
places.SetIcon("桌面", sys "\imageres.dll", 106)
places.SetIcon("顯示桌面", sys "\imageres.dll", 106)
places.SetIcon("資源回收筒", sys "\imageres.dll", 50)

; ---- main menu (green)
main := GlassMenu({light: "auto", iconSize: 32, accent: 0xFFC0F3A3})
main.Add("應用程式", apps)
main.Add()
main.Add("位置", places)
main.Add()
main.Add("記事本", (*) => Run("notepad.exe"))
main.Add("小算盤", (*) => Run("calc.exe"))
main.Add("檔案總管", (*) => Run("explorer.exe"))
main.Add("範例圖示（.ico）", (name, pos, m) => MsgBox("你點了「" name "」（第 " pos " 項）", "GlassMenu"))
main.Add("停用的項目", (*) => 0)
main.Disable("停用的項目")
main.SetIcon("應用程式", sys "\imageres.dll", 3)
main.SetIcon("位置", sys "\imageres.dll", 4)
main.SetIcon("記事本", A_WinDir "\notepad.exe", 1)
main.SetIcon("小算盤", sys "\calc.exe", 1)
main.SetIcon("檔案總管", A_WinDir "\explorer.exe", 1)
main.SetIcon("範例圖示（.ico）", assets "\sample.ico", 1)

TrayTip "Ctrl + 右鍵 彈出玻璃選單", "GlassMenu 範例"

<^RButton:: main.Show()
