@echo off
rem Runs the full 48-hour training. Leave this window open. Close it (or press Ctrl+C) to pause;
rem double-click again to resume where it left off.
cd /d "%~dp0"
title Spectacular Bid training
sbtrain.exe run --hours 48 --dir training
echo.
echo Training stopped. Double-click Start-Training.bat to resume.
pause
