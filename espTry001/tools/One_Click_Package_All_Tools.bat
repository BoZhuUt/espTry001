@echo off
setlocal EnableExtensions

title Pyxis SensorLink - One Click Package

call "%~dp0Check_Tools_Not_Running.bat"
if errorlevel 1 (
  if "%~1"=="" pause
  exit /b 1
)

set "PACKAGE_NAME=Pyxis_SensorLink_Tools_"
if not "%~1"=="" (
  set "PACKAGE_NAME=%~1"
) else (
  echo ============================================================
  echo   Pyxis SensorLink - One Click Package
  echo ============================================================
  echo.
  set /p "PACKAGE_NAME=Package name [Pyxis_SensorLink_Tools_]: "
  if not defined PACKAGE_NAME set "PACKAGE_NAME=Pyxis_SensorLink_Tools_"
)

echo.
echo Building all Windows tools...
call "%~dp0Build_Profile_Updater.bat"
if errorlevel 1 goto :build_failed
call "%~dp0Build_Profile_Manager.bat"
if errorlevel 1 goto :build_failed
call "%~dp0Build_Calibration_Profile_Manager.bat"
if errorlevel 1 goto :build_failed
call "%~dp0Build_SensorDataExporter.bat"
if errorlevel 1 goto :build_failed
call "%~dp0Build_TimeSyncManager.bat"
if errorlevel 1 goto :build_failed

set "TOOLS_DIR=%~dp0"
set "PROJECT_DIR=%~dp0.."
set "RELEASE_DIR=%~dp0release"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "$name = $env:PACKAGE_NAME.Trim();" ^
  "if ([string]::IsNullOrWhiteSpace($name)) { $name = 'Pyxis_SensorLink_Tools_' };" ^
  "if ($name -in @('.', '..') -or $name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw ('Invalid package name: ' + $name) };" ^
  "$release = [IO.Path]::GetFullPath($env:RELEASE_DIR);" ^
  "$folder = Join-Path $release $name;" ^
  "$zip = Join-Path $release ($name + '.zip');" ^
  "if (Test-Path -LiteralPath $folder) { throw ('Package folder already exists; choose another name: ' + $folder) };" ^
  "if (Test-Path -LiteralPath $zip) { throw ('Package ZIP already exists; choose another name: ' + $zip) };" ^
  "New-Item -ItemType Directory -Path $folder -Force | Out-Null;" ^
  "$toolFiles = @('SensorProfileUpdater.exe','SensorProfileManager.exe','CalibrationProfileManager.exe','SensorDataExporter.exe','TimeSyncManager.exe','import_calibration_profiles.py','build_calibration_bundle.py','build_calibration_package.py','upload_calibration_package.py','upload_sensor_profiles.py','sync_rtc.py','CALIBRATION_PROFILE_TOOLS_README.md','SENSOR_PROFILE_MANAGER_README.md');" ^
  "foreach ($file in $toolFiles) { $source = Join-Path $env:TOOLS_DIR $file; if (-not (Test-Path -LiteralPath $source)) { throw ('Required file missing: ' + $source) }; Copy-Item -LiteralPath $source -Destination (Join-Path $folder $file) };" ^
  "$configFiles = @('sensor_profiles.json','calibration_import_rules.json');" ^
  "foreach ($file in $configFiles) { $source = Join-Path $env:PROJECT_DIR ('config\' + $file); if (-not (Test-Path -LiteralPath $source)) { throw ('Required file missing: ' + $source) }; Copy-Item -LiteralPath $source -Destination (Join-Path $folder $file) };" ^
  "$userGuide = Join-Path $env:PROJECT_DIR 'WAVESHARE_28_USER_GUIDE.md';" ^
  "if (-not (Test-Path -LiteralPath $userGuide)) { throw ('User guide missing: ' + $userGuide) };" ^
  "Copy-Item -LiteralPath $userGuide -Destination (Join-Path $folder 'WAVESHARE_28_USER_GUIDE.md');" ^
  "$profiles = Join-Path $env:PROJECT_DIR 'config\calibration_profiles';" ^
  "if (-not (Test-Path -LiteralPath $profiles -PathType Container)) { throw ('Calibration profile folder missing: ' + $profiles) };" ^
  "Copy-Item -LiteralPath $profiles -Destination (Join-Path $folder 'calibration_profiles') -Recurse;" ^
  "Compress-Archive -Path (Join-Path $folder '*') -DestinationPath $zip -CompressionLevel Optimal;" ^
  "$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash;" ^
  "Write-Host ''; Write-Host ('Folder : ' + $folder); Write-Host ('ZIP    : ' + $zip); Write-Host ('SHA256 : ' + $hash);"

if errorlevel 1 (
  echo.
  echo PACKAGE FAILED. No existing package was overwritten.
  if "%~1"=="" pause
  exit /b 1
)

echo.
echo Package completed successfully.
if "%~1"=="" pause
exit /b 0

:build_failed
echo.
echo BUILD FAILED. Close any running tool and try again.
if "%~1"=="" pause
exit /b 1
