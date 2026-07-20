@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "JOBS=%~1"
if not defined JOBS set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"
set "BUILD_LOCATION=%~2"
if not defined BUILD_LOCATION set "BUILD_LOCATION=linux"
set "BACKEND=%~3"
if not defined BACKEND set "BACKEND=windows"

echo Absolute build benchmark: jobs=%JOBS%, backend=%BACKEND%, location=%BUILD_LOCATION%
echo.

if /i "%BACKEND%"=="windows" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" goto :failed
    set "VSINSTALL="
    for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
    if not defined VSINSTALL goto :failed
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 goto :failed
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-build-benchmark-windows.ps1" -Jobs %JOBS%
) else (
    where wsl.exe >nul 2>nul
    if errorlevel 1 goto :failed
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-build-benchmark.ps1" -Jobs %JOBS% -BuildLocation %BUILD_LOCATION%
)
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" goto :failed_with_code

echo.
echo Build benchmark completed successfully.
if /i not "%ABSOLUTE_BENCHMARK_NO_PAUSE%"=="1" pause
exit /b 0

:failed
set "RESULT=1"

:failed_with_code
echo.
echo Build benchmark failed with exit code %RESULT%.
if /i not "%ABSOLUTE_BENCHMARK_NO_PAUSE%"=="1" pause
exit /b %RESULT%
