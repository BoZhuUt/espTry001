@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0sensor_profile_updater_gui.ps1"
if errorlevel 1 pause
