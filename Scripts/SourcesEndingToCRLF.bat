@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "SCRIPT=%SCRIPT_DIR%SourcesEndingToCRLF.ps1"

for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"

if not exist "%SCRIPT%" (
    echo PowerShell script not found:
    echo %SCRIPT%
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -Root "%ROOT%" %*

if errorlevel 1 (
    echo.
    echo Failed.
    exit /b %errorlevel%
)

echo.
echo Done.
