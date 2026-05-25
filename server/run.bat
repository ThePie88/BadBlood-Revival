@echo off
:: BadBlood-Revival — start the server on Windows
::
:: Starts the FastAPI backend in this terminal. Run stunnel separately as admin
:: for HTTPS termination (see server/README.md).

setlocal
cd /d "%~dp0"

:: Activate venv if present
if exist venv\Scripts\activate.bat (
    call venv\Scripts\activate.bat
)

:: Verify Python is reachable
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found in PATH.
    echo Install Python 3.10+ from python.org and ensure it's on PATH.
    exit /b 1
)

:: Install deps if missing
python -c "import fastapi" 2>nul
if errorlevel 1 (
    echo [setup] Installing dependencies...
    python -m pip install -r requirements.txt
)

:: Run
echo.
echo ========================================
echo   BadBlood-Revival Server
echo ========================================
echo.
python main.py
endlocal
