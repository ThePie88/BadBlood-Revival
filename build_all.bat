@echo off
:: =============================================================================
:: build_all.bat — Build every component of BadBlood-Revival in one shot
:: =============================================================================
::
:: Self-contained: invokes g++ directly instead of calling per-component
:: build.bat files (which have proven fragile across environments).
::
:: Produces under dist\ everything end-users need:
::   PIE-LAUNCHER.exe, stubs\*.dll, hooks\*.dll, patcher\, server\, *.md
::
:: Then zips dist\ into dist\BadBloodRevival-vX.X.zip for upload to Releases.
::
:: Requires: MinGW-w64 GCC + windres + Python 3.10+ all in PATH.
:: See HOW_TO_BUILD.md for toolchain setup.

setlocal enabledelayedexpansion
cd /d "%~dp0"

set VERSION=1.0.0
set DIST=dist
set ZIP_NAME=BadBloodRevival-v%VERSION%.zip

echo.
echo ===============================================================
echo   BadBlood-Revival -- build_all.bat
echo ===============================================================
echo   Version: %VERSION%
echo   Output:  %DIST%\
echo.

:: -----------------------------------------------------------------
:: Pre-flight
:: -----------------------------------------------------------------
echo [pre-flight] Verifying toolchain...

where g++ >nul 2>&1
if errorlevel 1 (
    echo   [X] g++ not found in PATH.
    echo   Install MinGW-w64 from https://winlibs.com/ and add bin\ to PATH.
    exit /b 1
)
for /f "tokens=*" %%v in ('g++ --version 2^>^&1 ^| findstr /R "^g++"') do echo   [OK] %%v

where gcc >nul 2>&1
if errorlevel 1 (
    echo   [X] gcc not found in PATH ^(stubs are C, need gcc^).
    exit /b 1
)
echo   [OK] gcc found

where windres >nul 2>&1
if errorlevel 1 (
    echo   [X] windres not found ^(should ship with MinGW-w64^).
    exit /b 1
)
echo   [OK] windres found

where python >nul 2>&1
if errorlevel 1 (
    echo   [X] python not found in PATH. Need 3.10+.
    exit /b 1
)
for /f "tokens=*" %%v in ('python --version 2^>^&1') do echo   [OK] %%v
echo.

:: -----------------------------------------------------------------
:: Reset dist
:: -----------------------------------------------------------------
echo [reset] Cleaning %DIST%\
if exist "%DIST%" rmdir /S /Q "%DIST%"
mkdir "%DIST%"
mkdir "%DIST%\stubs"
mkdir "%DIST%\hooks"
mkdir "%DIST%\patcher"
echo   [OK]
echo.

:: -----------------------------------------------------------------
:: 1. Launcher
:: -----------------------------------------------------------------
echo [1/5] Building launcher...

echo   compiling resource.rc...
windres launcher\src\resource.rc -o launcher\src\resource.o > %DIST%\build-launcher.log 2>&1
if errorlevel 1 (
    echo   [X] windres failed. See %DIST%\build-launcher.log
    exit /b 1
)

echo   compiling PIE-LAUNCHER.exe...
g++ -O2 -mwindows -o launcher\PIE-LAUNCHER.exe ^
    launcher\src\main.cpp ^
    launcher\src\resource.o ^
    launcher\lib\imgui\imgui.cpp ^
    launcher\lib\imgui\imgui_draw.cpp ^
    launcher\lib\imgui\imgui_tables.cpp ^
    launcher\lib\imgui\imgui_widgets.cpp ^
    launcher\lib\imgui\backends\imgui_impl_win32.cpp ^
    launcher\lib\imgui\backends\imgui_impl_dx11.cpp ^
    -I launcher\lib\imgui -I launcher\lib\imgui\backends ^
    -ld3d11 -ldxgi -ldwmapi -ld3dcompiler -lwininet -lole32 -lshell32 ^
    -static -std=c++17 >> %DIST%\build-launcher.log 2>&1
if errorlevel 1 (
    echo   [X] launcher build failed. See %DIST%\build-launcher.log
    exit /b 1
)
if not exist launcher\PIE-LAUNCHER.exe (
    echo   [X] PIE-LAUNCHER.exe missing despite success exit code.
    exit /b 1
)
copy /Y launcher\PIE-LAUNCHER.exe %DIST%\PIE-LAUNCHER.exe >nul
echo   [OK] PIE-LAUNCHER.exe
echo.

:: -----------------------------------------------------------------
:: 2. Stubs
:: -----------------------------------------------------------------
echo [2/5] Building stubs...

if not exist stubs\out mkdir stubs\out

echo   BEClient_x64.dll...
gcc -shared -O2 -o stubs\out\BEClient_x64.dll stubs\src\beclient.c > %DIST%\build-stubs.log 2>&1
if errorlevel 1 (
    echo   [X] BEClient build failed. See %DIST%\build-stubs.log
    exit /b 1
)

echo   BEServer_x64.dll...
gcc -shared -O2 -o stubs\out\BEServer_x64.dll stubs\src\beserver.c >> %DIST%\build-stubs.log 2>&1
if errorlevel 1 (
    echo   [X] BEServer build failed. See %DIST%\build-stubs.log
    exit /b 1
)

echo   EasyAntiCheat_x64.dll...
gcc -shared -O2 -o stubs\out\EasyAntiCheat_x64.dll stubs\src\eac_x64.c -lws2_32 >> %DIST%\build-stubs.log 2>&1
if errorlevel 1 (
    echo   [X] EAC x64 build failed. See %DIST%\build-stubs.log
    exit /b 1
)

echo   EasyAntiCheat_x86.dll ^(actually x64 - install 32-bit MinGW for real x86^)...
gcc -shared -O2 -o stubs\out\EasyAntiCheat_x86.dll stubs\src\eac_x86.c >> %DIST%\build-stubs.log 2>&1
if errorlevel 1 (
    echo   [X] EAC x86 build failed. See %DIST%\build-stubs.log
    exit /b 1
)

copy /Y stubs\out\BEClient_x64.dll %DIST%\stubs\ >nul
copy /Y stubs\out\BEServer_x64.dll %DIST%\stubs\ >nul
copy /Y stubs\out\EasyAntiCheat_x64.dll %DIST%\stubs\ >nul
copy /Y stubs\out\EasyAntiCheat_x86.dll %DIST%\stubs\ >nul
echo   [OK] BEClient, BEServer, EasyAntiCheat
echo.

:: -----------------------------------------------------------------
:: 3. Texture-hook + LightFX proxy
:: -----------------------------------------------------------------
echo [3/5] Building texture-hook...

echo   texture_hook.dll...
g++ -shared -O2 -o texture-hook\texture_hook.dll texture-hook\texture_hook.cpp ^
    -luser32 -lpsapi -lws2_32 -lcrypt32 ^
    -static -std=c++17 > %DIST%\build-texturehook.log 2>&1
if errorlevel 1 (
    echo   [X] texture_hook build failed. See %DIST%\build-texturehook.log
    exit /b 1
)

echo   LightFX.dll ^(injection proxy^)...
g++ -shared -O2 -o texture-hook\LightFX.dll texture-hook\lightfx_proxy.cpp ^
    -static -std=c++17 >> %DIST%\build-texturehook.log 2>&1
if errorlevel 1 (
    echo   [X] LightFX proxy build failed. See %DIST%\build-texturehook.log
    exit /b 1
)

copy /Y texture-hook\texture_hook.dll %DIST%\hooks\ >nul
copy /Y texture-hook\LightFX.dll %DIST%\hooks\ >nul
echo   [OK] texture_hook.dll, LightFX.dll
echo.

:: -----------------------------------------------------------------
:: 4. Bundle patcher + server + docs
:: -----------------------------------------------------------------
echo [4/5] Bundling patcher, server, and docs...

copy /Y patcher\apply_patches.py %DIST%\patcher\ >nul
copy /Y patcher\patches.json %DIST%\patcher\ >nul
copy /Y patcher\README.md %DIST%\patcher\ >nul

xcopy /E /I /Q /Y server %DIST%\server >nul
if exist %DIST%\server\__pycache__ rmdir /S /Q %DIST%\server\__pycache__
if exist %DIST%\server\pls-emu.log del /Q %DIST%\server\pls-emu.log
if exist %DIST%\server\data\dlbb.db del /Q %DIST%\server\data\dlbb.db
if exist %DIST%\server\certs\cert.pem del /Q %DIST%\server\certs\cert.pem
if exist %DIST%\server\certs\key.pem del /Q %DIST%\server\certs\key.pem
if exist %DIST%\server\stunnel.conf del /Q %DIST%\server\stunnel.conf

:: Public-hosting helpers for both OSes (optional — users who only want
:: local play can ignore them)
if exist server-linux xcopy /E /I /Q /Y server-linux %DIST%\server-linux >nul
if exist server-windows xcopy /E /I /Q /Y server-windows %DIST%\server-windows >nul

copy /Y HOW_TO_PLAY.md %DIST%\ >nul
copy /Y HOW_TO_HOST.md %DIST%\ >nul
copy /Y HOW_TO_BUILD.md %DIST%\ >nul
copy /Y LICENSE %DIST%\ >nul
copy /Y NOTICE %DIST%\ >nul

(
    echo BadBlood-Revival v%VERSION% -- Quick Orientation
    echo ===============================================
    echo.
    echo READ HOW_TO_PLAY.md FIRST. It walks you through every step.
    echo.
    echo This ZIP contains pre-built binaries so you don't have to compile
    echo anything. You'll still need to:
    echo   1. Own Dying Light: Bad Blood on Steam
    echo   2. Install Python 3.10+, stunnel, and openssl
    echo   3. Run Steamless on your BadBloodGame.exe
    echo   4. Follow the rest of HOW_TO_PLAY.md
    echo.
    echo Folders in this ZIP:
    echo   PIE-LAUNCHER.exe   the launcher to run
    echo   stubs\             empty replacement DLLs ^(anti-cheat^)
    echo   hooks\             texture replacement + injection proxy
    echo   patcher\           Python script that patches your game files
    echo   server\            the FastAPI backend ^(runs on your PC^)
    echo.
    echo Source code: https://github.com/ThePie88/BadBlood-Revival
    echo License:     Apache 2.0 ^(see LICENSE^)
) > %DIST%\README.txt

echo   [OK] bundle assembled
echo.

:: -----------------------------------------------------------------
:: 5. Make the release ZIP
:: -----------------------------------------------------------------
echo [5/5] Creating release ZIP...

set ZIP_PATH=%DIST%\%ZIP_NAME%

where 7z >nul 2>&1
if not errorlevel 1 (
    pushd %DIST%
    7z a -tzip "%ZIP_NAME%" * -x!"%ZIP_NAME%" -x!build-*.log >nul
    popd
    if exist "%ZIP_PATH%" (
        echo   [OK] %ZIP_PATH% ^(via 7z^)
        goto :zip_done
    )
)

echo   [info] 7z not found, using PowerShell Compress-Archive...
powershell -NoLogo -NoProfile -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath '%DIST%\%ZIP_NAME%' -Force -CompressionLevel Optimal" 2>nul
if exist "%ZIP_PATH%" (
    echo   [OK] %ZIP_PATH% ^(via PowerShell^)
    goto :zip_done
)

echo   [X] Could not create ZIP. Install 7-Zip or use a modern PowerShell.
echo       The %DIST%\ folder is ready -- zip it manually before uploading.

:zip_done
echo.

echo ===============================================================
echo   BUILD COMPLETE
echo ===============================================================
echo.
echo   Output folder: %CD%\%DIST%\
if exist "%ZIP_PATH%" echo   Release ZIP:   %CD%\%ZIP_PATH%
echo.
echo   Next: upload the ZIP as a GitHub Release at
echo         https://github.com/ThePie88/BadBlood-Revival/releases/new
echo.
echo   Suggested release title: BadBlood-Revival v%VERSION%
echo   Suggested tag:           v%VERSION%
echo.
endlocal
