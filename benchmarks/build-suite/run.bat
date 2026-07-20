@echo off
setlocal EnableExtensions

set "JOBS=%~1"
if not defined JOBS set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"

where wsl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: WSL is required but wsl.exe was not found.
    goto :failed
)

echo Absolute build benchmark: jobs=%JOBS%
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-build-benchmark.ps1" -Jobs %JOBS%
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
