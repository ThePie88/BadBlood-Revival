@echo off
:: Build EAC + BattlEye stub DLLs
::
:: Requires gcc (MinGW-w64) in PATH. See HOW_TO_BUILD.md for toolchain setup.

setlocal
set SRC=%~dp0src
set OUT=%~dp0out

if not exist "%OUT%" mkdir "%OUT%"

where gcc >nul 2>&1
if errorlevel 1 (
    echo ERROR: gcc not found in PATH.
    echo Install MinGW-w64 from https://winlibs.com/ and add the bin/ folder to PATH.
    exit /b 1
)
echo Using gcc from PATH:
gcc --version | findstr /R "^gcc"
echo.

echo Building BEClient_x64.dll...
gcc -shared -O2 -o "%OUT%\BEClient_x64.dll" "%SRC%\beclient.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo Building BEServer_x64.dll...
gcc -shared -O2 -o "%OUT%\BEServer_x64.dll" "%SRC%\beserver.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo Building EasyAntiCheat_x64.dll...
gcc -shared -O2 -o "%OUT%\EasyAntiCheat_x64.dll" "%SRC%\eac_x64.c" -lws2_32
if %errorlevel% neq 0 goto :error
echo   OK

echo Building EasyAntiCheat_x86.dll (as x64 - install 32-bit MinGW for real x86)...
gcc -shared -O2 -o "%OUT%\EasyAntiCheat_x86.dll" "%SRC%\eac_x86.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo.
echo === Build complete ===
dir /b "%OUT%\*.dll"
exit /b 0

:error
echo BUILD FAILED
exit /b 1
