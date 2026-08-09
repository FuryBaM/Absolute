@echo off
setlocal EnableExtensions
node "%~dp0tools\absolute-dev.js" %*
exit /b %ERRORLEVEL%
