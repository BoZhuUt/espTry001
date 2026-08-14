@echo off
setlocal

rem Build all three Windows tools first so the package always contains current code.
call "%~dp0Build_Profile_Updater.bat"
if errorlevel 1 (
  echo ERROR: SensorProfileUpdater.exe build failed.
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

call "%~dp0Build_Profile_Manager.bat"
if errorlevel 1 (
  echo ERROR: SensorProfileManager.exe build failed.
  echo Close the program if it is currently running, then try again.
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

call "%~dp0Build_Calibration_Profile_Manager.bat"
if errorlevel 1 (
  echo ERROR: CalibrationProfileManager.exe build failed.
  echo Close the program if it is currently running, then try again.
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

call "%~dp0Build_SensorDataExporter.bat"
if errorlevel 1 (
  echo ERROR: SensorDataExporter.exe build failed.
  echo Close the program if it is currently running, then try again.
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

set "PROFILE_JSON=%~dp0..\config\sensor_profiles.json"
set "UPDATER_EXE=%~dp0SensorProfileUpdater.exe"
set "MANAGER_EXE=%~dp0SensorProfileManager.exe"
set "CALIBRATION_MANAGER_EXE=%~dp0CalibrationProfileManager.exe"
set "EXPORTER_EXE=%~dp0SensorDataExporter.exe"
set "CALIBRATION_PROFILES_DIR=%~dp0..\config\calibration_profiles"
set "CALIBRATION_RULES=%~dp0..\config\calibration_import_rules.json"
set "CALIBRATION_IMPORTER=%~dp0import_calibration_profiles.py"
set "CALIBRATION_BUNDLE_BUILDER=%~dp0build_calibration_bundle.py"
set "CALIBRATION_PACKAGE_BUILDER=%~dp0build_calibration_package.py"
set "CALIBRATION_UPLOADER=%~dp0upload_calibration_package.py"
set "SENSOR_UPLOAD_LIBRARY=%~dp0upload_sensor_profiles.py"
set "RELEASE_DIR=%~dp0release"
set "PROJECT_DIR=%~dp0.."

if not exist "%PROFILE_JSON%" (
  echo ERROR: JSON not found: %PROFILE_JSON%
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

rem Keep the package version consistent with get_git_version.py: V1.0 plus the
rem current HEAD commit count. Add local package time so repeated builds never
rem overwrite or masquerade as an older artifact. config_version remains
rem visible in the output and inside sensor_profiles.json, but no longer names
rem the ZIP file.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "$config = Get-Content -LiteralPath $env:PROFILE_JSON -Raw -Encoding UTF8 | ConvertFrom-Json;" ^
  "$configVersion = [string]$config.config_version;" ^
  "if ([string]::IsNullOrWhiteSpace($configVersion)) { throw 'config_version is missing' };" ^
  "$commitCount = (& git -C $env:PROJECT_DIR rev-list --count HEAD);" ^
  "if ($LASTEXITCODE -ne 0) { throw ('Cannot read Git version: ' + ($commitCount -join ' ')) };" ^
  "$gitVersion = 'V1.0.' + ([string]$commitCount).Trim();" ^
  "$packageTime = Get-Date -Format 'yyyyMMdd_HHmmss';" ^
  "New-Item -ItemType Directory -Path $env:RELEASE_DIR -Force | Out-Null;" ^
  "$zip = Join-Path $env:RELEASE_DIR ('Pyxis_SensorLink_Tools_' + $gitVersion + '_' + $packageTime + '.zip');" ^
  "$files = @($env:PROFILE_JSON, $env:UPDATER_EXE, $env:MANAGER_EXE, $env:CALIBRATION_MANAGER_EXE, $env:EXPORTER_EXE, $env:CALIBRATION_PROFILES_DIR, $env:CALIBRATION_RULES, $env:CALIBRATION_IMPORTER, $env:CALIBRATION_BUNDLE_BUILDER, $env:CALIBRATION_PACKAGE_BUILDER, $env:CALIBRATION_UPLOADER, $env:SENSOR_UPLOAD_LIBRARY);" ^
  "Compress-Archive -LiteralPath $files -DestinationPath $zip -CompressionLevel Optimal -Force;" ^
  "$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash;" ^
  "Write-Host ''; Write-Host ('Git     : ' + $gitVersion);" ^
  "Write-Host ('Config  : ' + $configVersion);" ^
  "Write-Host ('Built at: ' + $packageTime);" ^
  "Write-Host ('Package : ' + $zip);" ^
  "Write-Host ('SHA256 : ' + $hash);"

if errorlevel 1 (
  echo ERROR: Packaging failed.
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

echo.
echo Package completed successfully.
if /I not "%~1"=="--no-pause" pause
