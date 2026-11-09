#!/bin/sh
# Build bin/x64/LiquidGlass.dll with MinGW-w64 (x64).
# Linux cross build: needs g++-mingw-w64-x86-64-posix (prefix x86_64-w64-mingw32-).
# Windows (MSYS2 / w64devkit): plain g++ / windres on PATH are used automatically.
set -e
cd "$(dirname "$0")"

PREFIX="${CROSS_PREFIX-x86_64-w64-mingw32-}"
if ! command -v "${PREFIX}g++" >/dev/null 2>&1; then
    if command -v g++ >/dev/null 2>&1; then
        PREFIX=
    else
        echo "找不到 MinGW-w64 編譯器（${PREFIX}g++ 或 g++）" >&2
        exit 1
    fi
fi

mkdir -p bin/x64 build
"${PREFIX}windres" -I native native/LiquidGlass.rc -O coff -o build/LiquidGlass.res
"${PREFIX}g++" -std=c++17 -O2 -s -Wall -Wextra -Wno-missing-field-initializers \
    -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    -shared -o bin/x64/LiquidGlass.dll \
    native/api.cpp native/glass.cpp native/LiquidGlass.def build/LiquidGlass.res \
    -static -static-libgcc -static-libstdc++ \
    -luser32 -lgdi32 -lgdiplus -ldwmapi
echo "OK: bin/x64/LiquidGlass.dll"
"${PREFIX}objdump" -p bin/x64/LiquidGlass.dll | grep -E "DLL Name" || true
