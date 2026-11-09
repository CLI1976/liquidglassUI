# LiquidGlass.dll - MSVC x64 (primary for releases), MinGW-w64 as fallback.
#
#   cmake -S native -B build/msvc -A x64
#   cmake --build build/msvc --config Release
#
# Output: bin/x64/LiquidGlass.dll (static CRT, no VC++ runtime needed).
# Needs the "Desktop development with C++" workload including a Windows 10/11 SDK.
cmake_minimum_required(VERSION 3.16)
project(LiquidGlass LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(LiquidGlass SHARED
    api.cpp
    glass.cpp
    canvas.cpp
    LiquidGlass.def
    LiquidGlass.rc
)

target_compile_definitions(LiquidGlass PRIVATE
    UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(LiquidGlass PRIVATE user32 gdi32 gdiplus dwmapi)

# "$<0:>" keeps multi-config generators from adding a Release/ subfolder.
set_target_properties(LiquidGlass PROPERTIES
    PREFIX ""
    OUTPUT_NAME LiquidGlass
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/x64/$<0:>")

if(MSVC)
    # /utf-8: sources (error messages) are UTF-8. /guard:cf, /DYNAMICBASE, /NXCOMPAT and a full
    # version resource help antivirus heuristics; nothing is packed or compressed.
    target_compile_options(LiquidGlass PRIVATE /utf-8 /W4 /permissive- /EHsc /guard:cf /O2 /Gy)
    target_link_options(LiquidGlass PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF)
    set_property(TARGET LiquidGlass PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(LiquidGlass PRIVATE -O2 -Wall -Wextra -Wno-missing-field-initializers)
    target_link_options(LiquidGlass PRIVATE -s -static -static-libgcc -static-libstdc++)
endif()
