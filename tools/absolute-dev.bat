@echo off
setlocal EnableExtensions
node "%~dp0absolute-dev.js" %*
exit /b %ERRORLEVEL%
