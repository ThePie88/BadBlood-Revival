@echo off
:: =============================================================================
:: build_all.bat — Build every component of BadBlood-Revival in one shot
:: =============================================================================
::
:: Produces under dist\ everything end-users need:
::
::   dist\
::   ├── PIE-LAUNCHER.exe          launcher
::   ├── stubs\
::   │   ├── BEClient_x64.dll
::   │   ├── BEServer_x64.dll
::   │   └── EasyAntiCheat_x64.dll
::   ├── hooks\
::   │   ├── LightFX.dll           texture-hook injection proxy
::   │   └── texture_hook.dll      the actual hook engine
::   ├── patcher\
::   │   ├── apply_patches.py
::   │   └── patches.json
::   ├── server\                   FastAPI backend (copied as-is)
::   ├── HOW_TO_PLAY.md            user guide
::   └── README.txt                quick orientation
::
:: Then zips dist\ into dist\BadBloodRevival-vX.X.zip (if 7z or tar is
:: available) so you can upload to GitHub Releases.
::
:: Prereqs: MinGW-w64 GCC, windres, Python 3.10+. See HOW_TO_BUILD.md.

setlocal enabledelayedexpansion
cd /d "%~dp0"

set VERSION=1.0.0
set DIST=dist
set ZIP_NAME=BadBloodRevival-v%VERSION%.zip

echo.
echo ===============================================================
echo   BadBlood-Revival — build_all.bat
echo ===============================================================
echo   Version: %VERSION%
echo   Output:  %DIST%\
echo.

:: -----------------------------------------------------------------
:: Pre-flight checks
:: -----------------------------------------------------------------
echo [pre-flight] Verifying toolchain...

where g++ >nul 2>&1
if errorlevel 1 (
    echo   [!!] g++ not found in PATH.
    echo   Install MinGW-w64 from https://winlibs.com/ and add the bin/ to PATH.
    echo   See HOW_TO_BUILD.md for the full toolchain setup.
    exit /b 1
)
for /f "tokens=*" %%v in ('g++ --version 2^>^&1 ^| findstr /R "^g++"') do echo   [OK] %%v

where windres >nul 2>&1
if errorlevel 1 (
    echo   [!!] windres not found. Should ship with MinGW-w64.
    exit /b 1
)
echo   [OK] windres found

where python >nul 2>&1
if errorlevel 1 (
    echo   [!!] python not found in PATH. Need 3.10+.
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
:: 1. Build the launcher
:: -----------------------------------------------------------------
echo [1/5] Building launcher...
pushd launcher
call build.bat >..\dist\build-launcher.log 2>&1
if errorlevel 1 (
    echo   [!!] launcher build failed. See dist\build-launcher.log
    popd
    exit /b 1
)
if not exist PIE-LAUNCHER.exe (
    echo   [!!] launcher build said success but PIE-LAUNCHER.exe missing.
    popd
    exit /b 1
)
copy /Y PIE-LAUNCHER.exe ..\%DIST%\PIE-LAUNCHER.exe >nul
echo   [OK] PIE-LAUNCHER.exe
popd
echo.

:: -----------------------------------------------------------------
:: 2. Build the stubs
:: -----------------------------------------------------------------
echo [2/5] Building stubs...
pushd stubs
call build.bat >..\dist\build-stubs.log 2>&1
if errorlevel 1 (
    echo   [!!] stubs build failed. See dist\build-stubs.log
    popd
    exit /b 1
)
if not exist out\BEClient_x64.dll (
    echo   [!!] stubs build said success but BEClient_x64.dll missing.
    popd
    exit /b 1
)
copy /Y out\BEClient_x64.dll ..\%DIST%\stubs\ >nul
copy /Y out\BEServer_x64.dll ..\%DIST%\stubs\ >nul
copy /Y out\EasyAntiCheat_x64.dll ..\%DIST%\stubs\ >nul
if exist out\EasyAntiCheat_x86.dll copy /Y out\EasyAntiCheat_x86.dll ..\%DIST%\stubs\ >nul
echo   [OK] BEClient, BEServer, EasyAntiCheat
popd
echo.

:: -----------------------------------------------------------------
:: 3. Build texture-hook + LightFX proxy
:: -----------------------------------------------------------------
echo [3/5] Building texture-hook...
pushd texture-hook
call build.bat >..\dist\build-texturehook.log 2>&1
if errorlevel 1 (
    echo   [!!] texture-hook build failed. See dist\build-texturehook.log
    popd
    exit /b 1
)
if not exist texture_hook.dll (
    echo   [!!] texture-hook build said success but texture_hook.dll missing.
    popd
    exit /b 1
)
echo   [OK] texture_hook.dll

echo   [build] LightFX proxy (injection vector)...
g++ -shared -O2 -o LightFX.dll lightfx_proxy.cpp -static -std=c++17 >..\dist\build-lightfx.log 2>&1
if errorlevel 1 (
    echo   [!!] LightFX proxy build failed. See dist\build-lightfx.log
    popd
    exit /b 1
)
echo   [OK] LightFX.dll

copy /Y texture_hook.dll ..\%DIST%\hooks\ >nul
copy /Y LightFX.dll ..\%DIST%\hooks\ >nul
popd
echo.

:: -----------------------------------------------------------------
:: 4. Bundle patcher + server + docs
:: -----------------------------------------------------------------
echo [4/5] Bundling patcher, server, and docs...

:: Patcher (Python script + recipe)
copy /Y patcher\apply_patches.py %DIST%\patcher\ >nul
copy /Y patcher\patches.json %DIST%\patcher\ >nul
copy /Y patcher\README.md %DIST%\patcher\ >nul

:: Server (copy the whole folder except runtime artifacts)
xcopy /E /I /Q /Y server %DIST%\server >nul
:: Strip runtime junk if any
if exist %DIST%\server\__pycache__ rmdir /S /Q %DIST%\server\__pycache__
if exist %DIST%\server\pls-emu.log del /Q %DIST%\server\pls-emu.log
if exist %DIST%\server\data\dlbb.db del /Q %DIST%\server\data\dlbb.db
if exist %DIST%\server\certs\cert.pem del /Q %DIST%\server\certs\cert.pem
if exist %DIST%\server\certs\key.pem del /Q %DIST%\server\certs\key.pem
if exist %DIST%\server\stunnel.conf del /Q %DIST%\server\stunnel.conf

:: Top-level docs
copy /Y HOW_TO_PLAY.md %DIST%\ >nul
copy /Y HOW_TO_HOST.md %DIST%\ >nul
copy /Y HOW_TO_BUILD.md %DIST%\ >nul
copy /Y LICENSE %DIST%\ >nul
copy /Y NOTICE %DIST%\ >nul

:: Quick-orientation file for end users (so they don't have to find HOW_TO_PLAY among 50 files)
(
    echo BadBlood-Revival v%VERSION% — Quick Orientation
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
    echo.
    echo   PIE-LAUNCHER.exe   the launcher you'll run to register / play
    echo   stubs\             empty replacement DLLs ^(anti-cheat^)
    echo   hooks\             texture replacement + injection proxy
    echo   patcher\           Python script that patches your game files
    echo   server\            the FastAPI backend ^(runs on your PC^)
    echo.
    echo Source code: https://github.com/ThePie88/BadBlood-Revival
    echo License:     Apache 2.0 ^(see LICENSE^)
    echo.
) > %DIST%\README.txt

echo   [OK] bundle assembled
echo.

:: -----------------------------------------------------------------
:: 5. Make the release ZIP
:: -----------------------------------------------------------------
echo [5/5] Creating release ZIP...

set ZIP_PATH=%DIST%\%ZIP_NAME%

:: Try 7z first
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

:: Fall back to PowerShell Compress-Archive
echo   [info] 7z not found, falling back to PowerShell Compress-Archive...
powershell -NoLogo -NoProfile -Command "Compress-Archive -Path %DIST%\* -DestinationPath %DIST%\%ZIP_NAME% -Force -CompressionLevel Optimal" 2>nul
if exist "%ZIP_PATH%" (
    echo   [OK] %ZIP_PATH% ^(via PowerShell^)
    goto :zip_done
)

echo   [!!] Could not create ZIP. Install 7-Zip or use a modern PowerShell.
echo        The dist\ folder is ready — zip it manually before uploading.
goto :end

:zip_done
echo.

:: -----------------------------------------------------------------
:: Final summary
:: -----------------------------------------------------------------
:end
echo ===============================================================
echo   BUILD COMPLETE
echo ===============================================================
echo.
echo   Output folder: %CD%\%DIST%\
echo   Release ZIP:   %CD%\%DIST%\%ZIP_NAME% ^(if created above^)
echo.
echo   Next: upload the ZIP as a GitHub Release at
echo         https://github.com/ThePie88/BadBlood-Revival/releases/new
echo.
echo   Suggested release title: BadBlood-Revival v%VERSION%
echo   Suggested tag:           v%VERSION%
echo.
endlocal
