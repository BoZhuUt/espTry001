@echo off
setlocal

rem Build both Windows tools first so the package always contains current code.
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

set "PROFILE_JSON=%~dp0..\config\sensor_profiles.json"
set "UPDATER_EXE=%~dp0SensorProfileUpdater.exe"
set "MANAGER_EXE=%~dp0SensorProfileManager.exe"
set "RELEASE_DIR=%~dp0release"

if not exist "%PROFILE_JSON%" (
  echo ERROR: JSON not found: %PROFILE_JSON%
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

rem PowerShell reads config_version, sanitizes the ZIP name and places the three
rem distribution files at the root of the archive.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "$config = Get-Content -LiteralPath $env:PROFILE_JSON -Raw -Encoding UTF8 | ConvertFrom-Json;" ^
  "$version = [string]$config.config_version;" ^
  "if ([string]::IsNullOrWhiteSpace($version)) { throw 'config_version is missing' };" ^
  "$safeVersion = $version -replace '[^0-9A-Za-z._-]', '_';" ^
  "New-Item -ItemType Directory -Path $env:RELEASE_DIR -Force | Out-Null;" ^
  "$zip = Join-Path $env:RELEASE_DIR ('Pyxis_SensorLink_Tools_' + $safeVersion + '.zip');" ^
  "$files = @($env:PROFILE_JSON, $env:UPDATER_EXE, $env:MANAGER_EXE);" ^
  "Compress-Archive -LiteralPath $files -DestinationPath $zip -CompressionLevel Optimal -Force;" ^
  "$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash;" ^
  "Write-Host ''; Write-Host ('Package: ' + $zip);" ^
  "Write-Host ('SHA256 : ' + $hash);"

if errorlevel 1 (
  echo ERROR: Packaging failed.
  if /I not "%~1"=="--no-pause" pause
  exit /b 1
)

echo.
echo Package completed successfully.
if /I not "%~1"=="--no-pause" pause
