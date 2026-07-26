@echo off
setlocal EnableExtensions

set "MODE=release"
set "BOOTSTRAP=0"
set "TOOLCHAIN=msvc"
set "USE_CACHE=0"
set "NO_TEST=0"
set "EXTRA_ARGS="

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--bootstrap" set "BOOTSTRAP=1"& shift& goto parse
if /i "%~1"=="--frontend" set "MODE=frontend"& shift& goto parse
if /i "%~1"=="--clang-cl" set "TOOLCHAIN=clang-cl"& shift& goto parse
if /i "%~1"=="--sccache" set "USE_CACHE=1"& shift& goto parse
if /i "%~1"=="--no-test" set "NO_TEST=1"& shift& goto parse
if /i "%~1"=="--no-tests" set "NO_TEST=1"& shift& goto parse
if /i "%~1"=="--build-only" set "NO_TEST=1"& shift& goto parse
set "EXTRA_ARGS=%EXTRA_ARGS% %1"
shift
goto parse

:parsed
if "%BOOTSTRAP%"=="1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\bootstrap-llvm.ps1"
    if errorlevel 1 exit /b 1
)
if /i "%TOOLCHAIN%"=="clang-cl" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\bootstrap-ninja.ps1"
    if errorlevel 1 exit /b 1
)
if "%USE_CACHE%"=="1" if /i not "%TOOLCHAIN%"=="clang-cl" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\bootstrap-ninja.ps1"
    if errorlevel 1 exit /b 1
)

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
if errorlevel 1 exit /b %ERRORLEVEL%
set "VSLANG=1033"

set "CACHE_ARG="
if "%USE_CACHE%"=="1" set "CACHE_ARG=-UseCompilerCache"
set "NO_TEST_ARG="
if "%NO_TEST%"=="1" set "NO_TEST_ARG=-NoTest"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\build-windows.ps1" -Mode %MODE% -Toolchain %TOOLCHAIN% %CACHE_ARG% %NO_TEST_ARG% %EXTRA_ARGS%
exit /b %ERRORLEVEL%
