@echo off
REM build.bat -- Compiles MorrowindPipBoyEdition_Setup.exe and MorrowindPipBoyEdition_Uninstall.exe
REM Requires: .NET Framework 4.x (ships with Windows 10/11)
REM
REM The PS1 installer script is embedded directly inside both exes as a .NET
REM resource. The installer extracts it and runs it normally; the uninstaller
REM extracts it and runs it with -Uninstall.

setlocal
cd /d "%~dp0"

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

if not exist "installer.ico" (
    echo ERROR: installer.ico not found.
    echo Run the icon conversion script to generate installer.ico first.
    pause & exit /b 1
)

REM ---- Installer --------------------------------------------------------------
echo Compiling MorrowindPipBoyEdition_Setup.exe...

"%CSC%" /nologo /target:winexe ^
    /out:MorrowindPipBoyEdition_Setup.exe ^
    /res:MorrowindPipBoyEdition_Setup.ps1 ^
    /win32manifest:app.manifest ^
    /win32icon:installer.ico ^
    /reference:System.Windows.Forms.dll ^
    /reference:System.Drawing.dll ^
    /reference:System.dll ^
    MorrowindPipBoySetup.cs

if errorlevel 1 (
    echo.
    echo FAILED -- see errors above.
    pause & exit /b 1
)

echo SUCCESS: MorrowindPipBoyEdition_Setup.exe built.
echo.

REM ---- Uninstaller ------------------------------------------------------------
echo Compiling MorrowindPipBoyEdition_Uninstall.exe...

"%CSC%" /nologo /target:winexe ^
    /out:MorrowindPipBoyEdition_Uninstall.exe ^
    /res:MorrowindPipBoyEdition_Setup.ps1 ^
    /win32manifest:app.manifest ^
    /win32icon:installer.ico ^
    /reference:System.Windows.Forms.dll ^
    /reference:System.Drawing.dll ^
    /reference:System.dll ^
    MorrowindUninstall.cs

if errorlevel 1 (
    echo.
    echo FAILED -- see errors above.
    pause & exit /b 1
)

echo SUCCESS: MorrowindPipBoyEdition_Uninstall.exe built.
echo.
echo Place both exes in the same folder as the build\ directory before
echo distributing so the installer can find the mod files.

endlocal
