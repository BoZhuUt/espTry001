@echo off
setlocal
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
  echo ERROR: .NET Framework C# compiler was not found.
  exit /b 1
)

"%CSC%" /nologo /target:winexe /optimize+ /platform:anycpu ^
  /out:"%~dp0SensorDataExporter.exe" ^
  /reference:System.dll ^
  /reference:System.Core.dll ^
  /reference:System.Drawing.dll ^
  /reference:System.Windows.Forms.dll ^
  "%~dp0SensorDataExporter.cs"

if errorlevel 1 exit /b 1
echo Built: %~dp0SensorDataExporter.exe
