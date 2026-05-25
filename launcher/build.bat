@echo off
echo Building PIE-LAUNCHER...
windres src/resource.rc -o src/resource.o
g++ -O2 -mwindows -o PIE-LAUNCHER.exe ^
    src/main.cpp src/resource.o ^
    lib/imgui/imgui.cpp ^
    lib/imgui/imgui_draw.cpp ^
    lib/imgui/imgui_tables.cpp ^
    lib/imgui/imgui_widgets.cpp ^
    lib/imgui/backends/imgui_impl_win32.cpp ^
    lib/imgui/backends/imgui_impl_dx11.cpp ^
    -I lib/imgui -I lib/imgui/backends ^
    -ld3d11 -ldxgi -ldwmapi -ld3dcompiler -lwininet -lole32 -lshell32 -static -std=c++17
if %errorlevel% equ 0 (
    echo SUCCESS: PIE-LAUNCHER.exe
) else (
    echo FAILED
)
pause
