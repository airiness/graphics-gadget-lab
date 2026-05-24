@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"

set "SYNC_SCRIPT=%SCRIPT_DIR%SyncShadersToVS.ps1"
set "CRLF_SCRIPT=%SCRIPT_DIR%SourcesEndingToCRLF.bat"
set "SOLUTION=%ROOT%\GraphicsGadgetLab.sln"

if not exist "%SYNC_SCRIPT%" (
    echo Shader sync script not found: "%SYNC_SCRIPT%"
    pause
    exit /b 1
)

if not exist "%CRLF_SCRIPT%" (
    echo CRLF conversion script not found: "%CRLF_SCRIPT%"
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SYNC_SCRIPT%" -RootDir "%ROOT%"
if errorlevel 1 (
    echo Failed to sync shader files to Visual Studio project.
    pause
    exit /b 1
)

call "%CRLF_SCRIPT%"
if errorlevel 1 (
    echo Failed to normalize source file line endings.
    pause
    exit /b 1
)

start "" "%SOLUTION%"
