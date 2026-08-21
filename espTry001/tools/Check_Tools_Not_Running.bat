@echo off
setlocal
set "TOOLS_DIR=%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "$names = @('SensorProfileUpdater.exe','SensorProfileManager.exe','CalibrationProfileManager.exe','SensorDataExporter.exe','TimeSyncManager.exe');" ^
  "$processNames = $names | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) };" ^
  "$running = @(Get-Process -Name $processNames -ErrorAction SilentlyContinue);" ^
  "if ($running.Count -gt 0) {" ^
  "  Write-Host 'Closing running Pyxis tools before packaging...';" ^
  "  $running | ForEach-Object {" ^
  "    Write-Host ('  Stop ' + $_.ProcessName + '.exe  PID=' + $_.Id);" ^
  "    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue" ^
  "  };" ^
  "  Start-Sleep -Milliseconds 300;" ^
  "};" ^
  "$locked = New-Object System.Collections.Generic.List[string];" ^
  "foreach ($name in $names) {" ^
  "  $path = Join-Path $env:TOOLS_DIR $name;" ^
  "  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue };" ^
  "  try {" ^
  "    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None);" ^
  "    $stream.Dispose();" ^
  "  } catch { [void]$locked.Add($name) }" ^
  "};" ^
  "if ($locked.Count -eq 0) { exit 0 };" ^
  "Write-Host '';" ^
  "Write-Host 'ERROR: These tool files remain locked after forced process shutdown:' -ForegroundColor Red;" ^
  "$locked | ForEach-Object { Write-Host ('  ' + $_) -ForegroundColor Yellow };" ^
  "Write-Host '';" ^
  "Write-Host 'Close the locking program or restart Windows, then run the package script again.';" ^
  "exit 2"

exit /b %errorlevel%
