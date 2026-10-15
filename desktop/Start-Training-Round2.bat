@echo off
rem Round 2 training: starts from the models in training\promoted and works in training2.
rem Leave this window open. Close it (or press Ctrl+C) to pause; double-click again to resume.
cd /d "%~dp0"
title Spectacular Bid training (round 2)
sbtrain.exe run --hours 48 --dir training2 --from training\promoted
echo.
echo Training stopped. Double-click Start-Training-Round2.bat to resume.
pause
