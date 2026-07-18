@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"

set "SYNC_SCRIPT=%SCRIPT_DIR%SyncShadersToVS.ps1"
set "NORMALIZE_SCRIPT=%SCRIPT_DIR%SourcesEndingToCRLF.bat"
set "SOLUTION=%ROOT%\GraphicsGadgetLab.sln"

if not exist "%SYNC_SCRIPT%" (
    echo Shader sync script not found: "%SYNC_SCRIPT%"
    pause
    exit /b 1
)

if not exist "%NORMALIZE_SCRIPT%" (
    echo Text normalization script not found: "%NORMALIZE_SCRIPT%"
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SYNC_SCRIPT%" -RootDir "%ROOT%"
if errorlevel 1 (
    echo Failed to sync shader files to Visual Studio project.
    pause
    exit /b 1
)

call "%NORMALIZE_SCRIPT%"
if errorlevel 1 (
    echo Failed to normalize source files to UTF-8 with CRLF line endings.
    pause
    exit /b 1
)

start "" "%SOLUTION%"
