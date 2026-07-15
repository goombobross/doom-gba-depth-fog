@echo off
setlocal
set "WAD=%~1"
if "%WAD%"=="" set "WAD=GbaWadUtil\doomu.wad"
python tools\build_prelit_flats.py "%WAD%" data\prelit_flats.bin
if errorlevel 1 exit /b 1
echo Prelit flat bank regenerated from %WAD%.
