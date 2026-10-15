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
    echo Failed to normalize source files to UTF-8 without BOM and CRLF line endings.
    pause
    exit /b 1
)

rem Prefer newer VS major versions, with release IDEs before previews within each version.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
set "DEVENV="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=1 delims=." %%I in (`call "%VSWHERE%" -prerelease -sort -property installationVersion`) do call :FindVisualStudioForMajor %%I
)

if defined DEVENV (
    if exist "%DEVENV%" (
        echo Opening solution with "%DEVENV%"
        start "" "%DEVENV%" "%SOLUTION%"
        exit /b
    )
)

echo Visual Studio could not be located. Using the default solution handler.
start "" "%SOLUTION%"
exit /b

:FindVisualStudioForMajor
if defined DEVENV exit /b
set /a "VS_NEXT_MAJOR=%~1 + 1"

for /f "usebackq delims=" %%I in (`call "%VSWHERE%" -version "[%~1.0,%VS_NEXT_MAJOR%.0)" -sort -property productPath`) do (
    if not defined DEVENV if exist "%%I" set "DEVENV=%%I"
)
if defined DEVENV exit /b

for /f "usebackq delims=" %%I in (`call "%VSWHERE%" -version "[%~1.0,%VS_NEXT_MAJOR%.0)" -prerelease -sort -property productPath`) do (
    if not defined DEVENV if exist "%%I" set "DEVENV=%%I"
)
exit /b
