@echo off
setlocal

title Pyxis SensorLink - Build and Package All Tools

echo ============================================================
echo   Pyxis SensorLink - Build and Package
echo ============================================================
echo.
echo This will rebuild and package:
echo   1. SensorProfileManager.exe
echo   2. CalibrationProfileManager.exe
echo   3. SensorProfileUpdater.exe
echo   4. SensorDataExporter.exe
echo   5. TimeSyncManager.exe
echo   6. Current config\sensor_profiles.json
echo   7. Calibration profiles, importer, builder, and USB uploader
echo.

call "%~dp0Package_Profile_Tools.bat" --no-pause
if errorlevel 1 (
  echo.
  echo FAILED. Review the error message above.
  pause
  exit /b 1
)

echo.
echo The ZIP file is ready in:
echo   %~dp0release
echo.
pause
