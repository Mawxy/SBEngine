@echo off
rem Jockey value sweep with the trained models: full self-play games scored by wins.
rem Put this and sbsweep.exe next to SpectacularBid.exe (the folder with training\promoted).
rem Results: sweep-jockey-trained\REPORT.md. Close the window or press Ctrl+C to pause; double-click again to resume.
cd /d "%~dp0"
title Spectacular Bid jockey value sweep (trained models)
set MODELS=
if exist training\promoted\info.bin set MODELS=training\promoted
if exist training2\promoted\info.bin set MODELS=training2\promoted
if "%MODELS%"=="" (echo No trained models found in training\promoted or training2\promoted next to this file. & pause & exit /b 1)
sbsweep.exe --param jockey_value --values 0,0.5,1,1.5,2,2.5,3,4,6 --baseline 0 --pairs 2000 --dets 32 --nodes 20000 --bidding calibrated --models %MODELS% --dir sweep-jockey-trained
echo.
echo Sweep stopped or finished. Results: sweep-jockey-trained\REPORT.md
pause
