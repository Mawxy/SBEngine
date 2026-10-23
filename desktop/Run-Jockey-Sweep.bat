@echo off
rem Jockey value sweep: full self-play games scored by wins.
rem Each jockey value plays the engine with jockey value 0 on the same deals, seats swapped.
rem Results: sweep-jockey\REPORT.md. Close the window or press Ctrl+C to pause; double-click again to resume.
cd /d "%~dp0"
title Spectacular Bid jockey value sweep
set MODELS=training\promoted
if exist training2\promoted\info.bin set MODELS=training2\promoted
sbsweep.exe --param jockey_value --values 0,0.5,1,1.5,2,2.5,3,4,6 --baseline 0 --pairs 2000 --dets 32 --nodes 20000 --bidding calibrated --models %MODELS% --dir sweep-jockey
echo.
echo Sweep stopped or finished. Results: sweep-jockey\REPORT.md
pause
