@echo off
:: Build texture_hook.dll + LightFX proxy
::
:: Requires g++ (MinGW-w64) in PATH. See HOW_TO_BUILD.md for toolchain setup.

setlocal
cd /d "%~dp0"

where g++ >nul 2>&1
if errorlevel 1 (
    echo ERROR: g++ not found in PATH.
    echo Install MinGW-w64 from https://winlibs.com/ and add the bin/ folder to PATH.
    exit /b 1
)

echo Building texture_hook.dll...
g++ -shared -O2 -o texture_hook.dll texture_hook.cpp ^
    -luser32 -lpsapi -lws2_32 -lcrypt32 ^
    -static -std=c++17
if errorlevel 1 (
    echo BUILD FAILED for texture_hook.dll
    exit /b 1
)
echo   OK

echo Building LightFX.dll (injection proxy)...
g++ -shared -O2 -o LightFX.dll lightfx_proxy.cpp -static -std=c++17
if errorlevel 1 (
    echo BUILD FAILED for LightFX.dll
    exit /b 1
)
echo   OK

echo.
echo === Build complete ===
dir /b *.dll
exit /b 0
