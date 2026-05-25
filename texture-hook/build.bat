@echo off
echo Building texture_hook.dll...
g++ -shared -O2 -o texture_hook.dll texture_hook.cpp -luser32 -lpsapi -static -std=c++17
if %errorlevel% equ 0 (
    echo SUCCESS: texture_hook.dll built
    echo.
    echo Setup:
    echo   1. Copy texture_hook.dll to the game folder
    echo   2. Create folder: game\mods\textures\
    echo   3. Put Crigrey DDS files there (e.g. player_9_jacket_mural_fpp_dif.dds)
    echo   4. Load the DLL at game startup
) else (
    echo FAILED
)
pause
