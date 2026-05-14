@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "SYNC_SCRIPT=%ROOT%\Scripts\SyncShadersToVS.ps1"
set "SOLUTION=%ROOT%\GraphicsGadgetLab.sln"

if not exist "%SYNC_SCRIPT%" (
    echo Shader sync script not found: "%SYNC_SCRIPT%"
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SYNC_SCRIPT%" -RootDir "%ROOT%"
if errorlevel 1 (
    echo Failed to sync shader files to Visual Studio project.
    pause
    exit /b 1
)

start "" "%SOLUTION%"
