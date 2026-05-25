@echo off
:: Generate a self-signed cert for stunnel (Windows). Requires openssl in PATH.
:: For production, replace with Let's Encrypt (see docs/setup-server-linux.md).
setlocal
set CN=%1
if "%CN%"=="" set CN=pls.example.it

if not exist certs mkdir certs
openssl req -x509 -newkey rsa:4096 -keyout certs\key.pem -out certs\cert.pem -days 365 -nodes -subj "/CN=%CN%"
if errorlevel 1 (
    echo.
    echo ERROR: openssl not found. Install via: winget install ShiningLight.OpenSSL.Light
    exit /b 1
)
echo.
echo Generated certs\cert.pem and certs\key.pem with CN=%CN%
endlocal
