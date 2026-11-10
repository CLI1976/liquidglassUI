@echo off
rem Build lib\LiquidGlass.dll with MSVC (Visual Studio 2019/2022 Build Tools with
rem the C++ workload, a Windows 10/11 SDK and the C++ CMake tools).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
set "VS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS goto novs
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cmake -S native -B build\msvc -A x64 || exit /b 1
cmake --build build\msvc --config Release || exit /b 1
echo Done: lib\LiquidGlass.dll
exit /b 0
:novs
echo Visual Studio Build Tools with C++ not found.
exit /b 1
