@echo off
cd /d "%~dp0"
sbtrain.exe status --dir training
echo.
pause
