@echo off
setlocal
call "%~dp0..\desktop\run.bat" -Source "%~dp0Chess.absproj" -Output "%~dp0.build\AbsoluteChess.exe"
exit /b %ERRORLEVEL%
