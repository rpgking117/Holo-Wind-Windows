@echo off
REM build.bat -- Compiles MorrowindPipBoyEdition_Setup.exe
REM Requires: .NET Framework 4.x (ships with Windows 10/11)
REM
REM The PS1 installer script is embedded directly inside the exe as a .NET
REM resource. When the user clicks Install the exe extracts it to a temp
REM folder and runs it via PowerShell, passing the three game paths.

setlocal

set "CSC=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

if not exist "%CSC%" (
    echo ERROR: csc.exe not found at %CSC%
    echo .NET Framework 4.x is required. It ships with Windows 10 and 11.
    pause & exit /b 1
)

if not exist "MorrowindPipBoyEdition_Setup.ps1" (
    echo ERROR: MorrowindPipBoyEdition_Setup.ps1 not found.
    echo This file must be in the same folder as build.bat.
    pause & exit /b 1
)

echo Compiling MorrowindPipBoyEdition_Setup.exe...

"%CSC%" /nologo /target:winexe ^
    /out:MorrowindPipBoyEdition_Setup.exe ^
    /res:MorrowindPipBoyEdition_Setup.ps1 ^
    /win32manifest:app.manifest ^
    /reference:System.Windows.Forms.dll ^
    /reference:System.Drawing.dll ^
    /reference:System.dll ^
    MorrowindPipBoySetup.cs

if errorlevel 1 (
    echo.
    echo FAILED -- see errors above.
    pause & exit /b 1
)

echo.
echo SUCCESS: MorrowindPipBoyEdition_Setup.exe built.
echo.
echo Place this exe in the same folder as the build\ directory before
echo distributing so the installer can find the mod files.

endlocal
