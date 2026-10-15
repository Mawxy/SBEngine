@echo off
rem About 12 minutes. Checks that training works on this PC before the long run.
cd /d "%~dp0"
title Spectacular Bid training - quick test
sbtrain.exe quick --dir training-quick
echo.
echo Quick test finished. If you saw "Training finished" above, start the real run with Start-Training.bat.
pause
