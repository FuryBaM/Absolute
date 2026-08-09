@echo off
setlocal EnableExtensions

set "SAMPLES=%~1"
set "WARMUPS=%~2"
if not defined SAMPLES set "SAMPLES=9"
if not defined WARMUPS set "WARMUPS=2"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer vswhere.exe was not found.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
    echo ERROR: Visual Studio C++ x64 build tools were not found.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

echo Value/reference ABI benchmark: samples=%SAMPLES%, warmups=%WARMUPS%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-benchmark.ps1" -Samples %SAMPLES% -Warmups %WARMUPS%
exit /b %ERRORLEVEL%
