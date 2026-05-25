@echo off
setlocal

set MINGW_BIN=C:\Users\filip\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set GCC=%MINGW_BIN%\gcc.exe
set SRC=%~dp0src
set OUT=%~dp0out

if not exist "%OUT%" mkdir "%OUT%"

echo Using GCC: %GCC%
echo.

echo Building BEClient_x64.dll...
"%GCC%" -shared -o "%OUT%\BEClient_x64.dll" "%SRC%\beclient.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo Building BEServer_x64.dll...
"%GCC%" -shared -o "%OUT%\BEServer_x64.dll" "%SRC%\beserver.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo Building EasyAntiCheat_x64.dll...
"%GCC%" -shared -o "%OUT%\EasyAntiCheat_x64.dll" "%SRC%\eac_x64.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo Building EasyAntiCheat_x86.dll (as x64 - no 32-bit compiler)...
"%GCC%" -shared -o "%OUT%\EasyAntiCheat_x86.dll" "%SRC%\eac_x86.c"
if %errorlevel% neq 0 goto :error
echo   OK

echo.
echo === Build complete ===
dir /b "%OUT%\*.dll"
goto :end

:error
echo BUILD FAILED
exit /b 1

:end
