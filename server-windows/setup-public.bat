@echo off
:: =============================================================================
:: setup-public.bat -- Windows public hosting setup for BadBlood-Revival
:: =============================================================================
::
:: Configures a Windows machine (Windows 10 / 11 / Server 2019+) to host the
:: BadBlood-Revival server publicly on the internet:
::
::   - Verifies Python, stunnel, openssl are installed
::   - Generates a self-signed bootstrap certificate (use win-acme for prod)
::   - Writes stunnel.conf with absolute paths
::   - Installs stunnel as a Windows service
::   - Creates a scheduled task for the FastAPI backend (auto-start at boot,
::     restart on failure -- the closest thing Windows has to systemd)
::   - Opens Windows Firewall inbound ports 80, 443, 47584
::
:: Usage (Administrator required):
::   setup-public.bat pls.example.it
::
:: After this completes, set your DNS A record to this VPS's public IP, then
:: replace the self-signed cert with one from Let's Encrypt (via win-acme):
::   https://www.win-acme.com/

setlocal enabledelayedexpansion

:: -----------------------------------------------------------------
:: Args + admin check
:: -----------------------------------------------------------------
set DOMAIN=%1
if "%DOMAIN%"=="" set DOMAIN=pls.example.it

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click setup-public.bat and choose "Run as administrator".
    exit /b 1
)

set ROOT=%~dp0..
pushd "%ROOT%"
set ROOT=%CD%
popd

echo.
echo ===============================================================
echo   BadBlood-Revival -- Windows Public Hosting Setup
echo ===============================================================
echo   Domain:       %DOMAIN%
echo   Install root: %ROOT%
echo.

:: -----------------------------------------------------------------
:: 1. Toolchain check
:: -----------------------------------------------------------------
echo [1/6] Verifying dependencies...

where python >nul 2>&1
if errorlevel 1 (
    echo   [X] Python not found in PATH.
    echo       Install Python 3.10+ from https://www.python.org/downloads/
    echo       or run: winget install Python.Python.3.12
    exit /b 1
)
for /f "tokens=*" %%v in ('python --version 2^>^&1') do echo   [OK] %%v

set STUNNEL_EXE=
if exist "C:\Program Files (x86)\stunnel\bin\stunnel.exe" set STUNNEL_EXE=C:\Program Files (x86)\stunnel\bin\stunnel.exe
if exist "C:\Program Files\stunnel\bin\stunnel.exe" set STUNNEL_EXE=C:\Program Files\stunnel\bin\stunnel.exe
if "%STUNNEL_EXE%"=="" (
    echo   [X] stunnel not found.
    echo       Install: winget install MichalTrojnara.Stunnel
    echo       Or download from https://www.stunnel.org/downloads.html
    exit /b 1
)
echo   [OK] stunnel: %STUNNEL_EXE%

where openssl >nul 2>&1
if errorlevel 1 (
    echo   [X] openssl not found in PATH.
    echo       Install: winget install ShiningLight.OpenSSL.Light
    exit /b 1
)
echo   [OK] openssl found
echo.

:: -----------------------------------------------------------------
:: 2. Python deps
:: -----------------------------------------------------------------
echo [2/6] Installing Python dependencies...
cd /d "%ROOT%\server"
python -c "import fastapi, uvicorn" 2>nul
if errorlevel 1 (
    python -m pip install -r requirements.txt
    if errorlevel 1 (
        echo   [X] pip install failed
        exit /b 1
    )
)
echo   [OK]
echo.

:: -----------------------------------------------------------------
:: 3. Self-signed bootstrap certificate
:: -----------------------------------------------------------------
echo [3/6] Self-signed bootstrap certificate ^(replace with Let's Encrypt later^)...
if not exist certs mkdir certs
if exist certs\cert.pem if exist certs\key.pem (
    echo   [OK] certs\cert.pem and certs\key.pem already exist
) else (
    openssl req -x509 -newkey rsa:4096 -keyout certs\key.pem -out certs\cert.pem -days 365 -nodes -subj "/CN=%DOMAIN%" >nul 2>&1
    if errorlevel 1 (
        echo   [X] openssl failed
        exit /b 1
    )
    echo   [OK] Generated certs\cert.pem and certs\key.pem ^(CN=%DOMAIN%^)
)
echo.

:: -----------------------------------------------------------------
:: 4. stunnel.conf
:: -----------------------------------------------------------------
echo [4/6] Writing stunnel.conf...

(
    echo ; BadBlood-Revival -- stunnel config ^(Windows public hosting^)
    echo.
    echo [pls]
    echo accept = 443
    echo connect = 127.0.0.1:80
    echo cert = %ROOT%\server\certs\cert.pem
    echo key  = %ROOT%\server\certs\key.pem
    echo TIMEOUTclose = 0
) > stunnel.conf

echo   [OK] %CD%\stunnel.conf
echo.

:: -----------------------------------------------------------------
:: 5. Scheduled task for the FastAPI backend
:: -----------------------------------------------------------------
echo [5/6] Creating scheduled task for the FastAPI backend...

set TASK_NAME=BadBloodRevival-Server
set PY_EXE=
for /f "tokens=*" %%p in ('where python') do (
    if not defined PY_EXE set PY_EXE=%%p
)

:: Delete old task if it exists
schtasks /Delete /TN "%TASK_NAME%" /F >nul 2>&1

:: Create the task: At system startup, run python main.py from server folder, restart on failure
schtasks /Create ^
    /TN "%TASK_NAME%" ^
    /TR "\"%PY_EXE%\" \"%ROOT%\server\main.py\"" ^
    /SC ONSTART ^
    /RU SYSTEM ^
    /RL HIGHEST ^
    /F >nul

if errorlevel 1 (
    echo   [X] Failed to create scheduled task. Are you Administrator?
    exit /b 1
)

:: Enable restart-on-failure via PowerShell (schtasks alone can't set this)
powershell -NoLogo -NoProfile -Command ^
    "$st = New-ScheduledTaskTrigger -AtStartup; " ^
    "$action = New-ScheduledTaskAction -Execute '%PY_EXE%' -Argument '%ROOT%\server\main.py' -WorkingDirectory '%ROOT%\server'; " ^
    "$settings = New-ScheduledTaskSettingsSet -RestartCount 99 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit 0; " ^
    "$principal = New-ScheduledTaskPrincipal -UserId SYSTEM -LogonType ServiceAccount -RunLevel Highest; " ^
    "Register-ScheduledTask -TaskName '%TASK_NAME%' -Trigger $st -Action $action -Settings $settings -Principal $principal -Force | Out-Null" >nul 2>&1

echo   [OK] Scheduled task '%TASK_NAME%' created ^(auto-start at boot, restart on failure^)
echo.

:: -----------------------------------------------------------------
:: 6. stunnel as a Windows service + Firewall + start everything
:: -----------------------------------------------------------------
echo [6/6] Installing stunnel as a Windows service + opening firewall...

:: Install stunnel as a service (uses stunnel.exe's built-in -install flag)
"%STUNNEL_EXE%" -install >nul 2>&1

:: Update the service to point at our config (stunnel's default config might
:: be elsewhere). Best practice: copy our config to stunnel's config dir.
set STUNNEL_DIR=
for %%I in ("%STUNNEL_EXE%\..") do set STUNNEL_BIN=%%~fI
for %%I in ("%STUNNEL_BIN%\..") do set STUNNEL_DIR=%%~fI
if exist "%STUNNEL_DIR%\config\stunnel.conf" (
    copy /Y "%CD%\stunnel.conf" "%STUNNEL_DIR%\config\stunnel.conf" >nul
    echo   [OK] stunnel.conf copied to %STUNNEL_DIR%\config\
) else (
    echo   [info] stunnel config dir not at default location.
    echo          Configure stunnel to use %CD%\stunnel.conf manually.
)

:: Start the service
net start stunnel >nul 2>&1
if errorlevel 1 (
    echo   [info] stunnel service couldn't start automatically.
    echo          Open services.msc and start "stunnel" manually, or check certs.
) else (
    echo   [OK] stunnel service started
)

:: Open firewall ports
echo   opening firewall ports 80, 443, 47584...
netsh advfirewall firewall delete rule name="BadBlood-Revival HTTP" >nul 2>&1
netsh advfirewall firewall delete rule name="BadBlood-Revival HTTPS" >nul 2>&1
netsh advfirewall firewall delete rule name="BadBlood-Revival Relay" >nul 2>&1
netsh advfirewall firewall add rule name="BadBlood-Revival HTTP"  dir=in action=allow protocol=TCP localport=80 >nul
netsh advfirewall firewall add rule name="BadBlood-Revival HTTPS" dir=in action=allow protocol=TCP localport=443 >nul
netsh advfirewall firewall add rule name="BadBlood-Revival Relay" dir=in action=allow protocol=any localport=47584 >nul
echo   [OK] firewall rules added
echo.

:: Start the backend task immediately (without waiting for next boot)
schtasks /Run /TN "%TASK_NAME%" >nul 2>&1
if errorlevel 1 (
    echo   [info] Could not start backend task right now. Will run at next boot.
) else (
    echo   [OK] Backend task started
)

echo.
echo ===============================================================
echo   SETUP COMPLETE
echo ===============================================================
echo.
echo   Backend:  http://localhost:80 ^(scheduled task: %TASK_NAME%^)
echo   stunnel:  https://localhost:443 ^(Windows service: stunnel^)
echo   Domain:   %DOMAIN%
echo.
echo   Management:
echo     schtasks /Query /TN "%TASK_NAME%"     -- backend status
echo     schtasks /Run   /TN "%TASK_NAME%"     -- start backend
echo     schtasks /End   /TN "%TASK_NAME%"     -- stop backend
echo     sc query stunnel                       -- stunnel service status
echo     net stop stunnel ^&^& net start stunnel -- restart stunnel
echo.
echo   Logs:
echo     %ROOT%\server\pls-emu.log
echo     ^(stunnel logs to its own location, see stunnel docs^)
echo.
echo   Next steps:
echo     1. Point DNS A record for %DOMAIN% at this server's public IP.
echo     2. Replace self-signed cert with Let's Encrypt:
echo          Download win-acme from https://www.win-acme.com/
echo          Run wacs.exe and pick "Create renewal" -^> single binding
echo          Configure it to deploy to %ROOT%\server\certs\
echo          Restart stunnel after each renewal.
echo     3. Verify externally: curl -sk https://%DOMAIN%/auth/login/steam/ -X POST -d "auth_session_ticket=test"
echo.
endlocal
