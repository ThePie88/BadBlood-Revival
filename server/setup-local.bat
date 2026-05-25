@echo off
:: setup-local.bat — One-stop local setup for BadBlood-Revival (Windows)
::
:: What it does:
::   1. Verifies Python and stunnel are installed
::   2. Creates a self-signed cert (CN=pls.dlbb.com) in certs/
::   3. Installs Python dependencies
::   4. Writes a working stunnel.conf
::   5. Tells you what to do next
::
:: Run from this folder (release/server/). No admin needed for setup itself,
:: but you'll need admin to run stunnel later (port 443 is privileged).

setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo ========================================
echo   BadBlood-Revival — Local Setup
echo ========================================
echo.

:: 1. Check Python
echo [1/4] Checking Python...
python --version >nul 2>&1
if errorlevel 1 (
    echo   [!!] Python not found in PATH.
    echo   Install Python 3.10+ from https://www.python.org/downloads/
    echo   Make sure to tick "Add Python to PATH" during install.
    exit /b 1
)
for /f "tokens=2" %%v in ('python --version 2^>^&1') do echo   [OK] Python %%v

:: 2. Check stunnel
echo [2/4] Checking stunnel...
set STUNNEL_EXE=
if exist "C:\Program Files (x86)\stunnel\bin\stunnel.exe" set STUNNEL_EXE=C:\Program Files (x86)\stunnel\bin\stunnel.exe
if exist "C:\Program Files\stunnel\bin\stunnel.exe" set STUNNEL_EXE=C:\Program Files\stunnel\bin\stunnel.exe
if "%STUNNEL_EXE%"=="" (
    echo   [!!] stunnel not found.
    echo   Install: winget install MichalTrojnara.Stunnel
    echo   Or download from https://www.stunnel.org/downloads.html
    exit /b 1
)
echo   [OK] stunnel found at: %STUNNEL_EXE%

:: 3. Generate self-signed cert if missing
echo [3/4] Self-signed certificate...
if not exist certs mkdir certs
if exist certs\cert.pem if exist certs\key.pem (
    echo   [OK] certs\cert.pem and certs\key.pem already exist, keeping them
) else (
    openssl version >nul 2>&1
    if errorlevel 1 (
        echo   [!!] openssl not found.
        echo   Install: winget install ShiningLight.OpenSSL.Light
        echo   Then re-run setup-local.bat.
        exit /b 1
    )
    echo   Generating self-signed cert (CN=pls.dlbb.com)...
    openssl req -x509 -newkey rsa:4096 -keyout certs\key.pem -out certs\cert.pem -days 365 -nodes -subj "/CN=pls.dlbb.com" >nul 2>&1
    if errorlevel 1 (
        echo   [!!] openssl failed. Cert NOT generated.
        exit /b 1
    )
    echo   [OK] Generated certs\cert.pem and certs\key.pem
)

:: 4. Install Python deps
echo [4/4] Installing Python dependencies...
python -c "import fastapi, uvicorn" 2>nul
if errorlevel 1 (
    python -m pip install -r requirements.txt >nul
    if errorlevel 1 (
        echo   [!!] pip install failed
        exit /b 1
    )
    echo   [OK] Dependencies installed
) else (
    echo   [OK] Dependencies already present
)

:: Write stunnel.conf from template
if not exist stunnel.conf (
    if exist stunnel.conf.template (
        copy /Y stunnel.conf.template stunnel.conf >nul
        echo   [OK] stunnel.conf created from template
    )
)

echo.
echo ========================================
echo   SETUP COMPLETE
echo ========================================
echo.
echo Next steps:
echo.
echo   1. Open Notepad as Administrator
echo      Edit C:\Windows\System32\drivers\etc\hosts
echo      Add the line:
echo          127.0.0.1 pls.dlbb.com
echo.
echo   2. In THIS terminal, run the server backend:
echo          run.bat
echo.
echo   3. In ANOTHER terminal (run as Administrator), start stunnel:
echo          "%STUNNEL_EXE%" stunnel.conf
echo.
echo   4. Patch your game (one-time, from the patcher/ folder):
echo          python apply_patches.py --game-dir "C:\path\to\DLBB" --local
echo.
echo   5. Drop the built stubs + texture-hook DLLs into the game folder
echo      (see ../README.md for the file list).
echo.
echo   6. Run the BadBlood-Revival launcher and PLAY.
echo.
endlocal
