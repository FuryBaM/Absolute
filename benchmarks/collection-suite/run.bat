@echo off
setlocal EnableExtensions

set "SAMPLES=%~1"
set "WARMUPS=%~2"
set "INCLUDE_PYTHON=%~3"

if not defined SAMPLES set "SAMPLES=11"
if not defined WARMUPS set "WARMUPS=2"
if not defined INCLUDE_PYTHON set "INCLUDE_PYTHON=1"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer vswhere.exe was not found.
    goto :failed
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
    echo ERROR: Visual Studio C++ x64 build tools were not found.
    goto :failed
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo ERROR: Could not initialize the Visual Studio x64 toolchain.
    goto :failed
)

echo Collection benchmark: samples=%SAMPLES%, warmups=%WARMUPS%, python=%INCLUDE_PYTHON%
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-benchmark.ps1" -Samples %SAMPLES% -Warmups %WARMUPS% -IncludePython %INCLUDE_PYTHON%
set "RESULT=%ERRORLEVEL%"

if not "%RESULT%"=="0" goto :failed_with_code

echo.
echo Benchmark completed successfully.
if /i not "%ABSOLUTE_BENCHMARK_NO_PAUSE%"==="1" pause
exit /b 0

:failed
set "RESULT=1"

:failed_with_code
echo.
echo Benchmark failed with exit code %RESULT%.
if /i not "%ABSOLUTE_BENCHMARK_NO_PAUSE%"==="1" pause
exit /b %RESULT%
