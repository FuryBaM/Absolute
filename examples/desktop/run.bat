@echo off
setlocal EnableExtensions

where wsl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: WSL is required to run the Absolute LLVM compiler.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer was not found.
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

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-desktop.ps1" %*
exit /b %ERRORLEVEL%

