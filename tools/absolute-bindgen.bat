@echo off
setlocal EnableExtensions
node "%~dp0absolute-bindgen.js" %*
exit /b %ERRORLEVEL%
