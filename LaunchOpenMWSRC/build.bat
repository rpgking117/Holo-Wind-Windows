@echo off
REM build.bat  --  Builds launch_openmw.exe
REM Requires: MSVC 2019 Build Tools (x64)
REM
REM launch_openmw.exe is a tiny Win32 shim called by the F4SE plugin when the
REM player presses F9. It starts morrowind_watcher.ps1 (detached) and writes
REM the trigger file that tells the watcher to launch OpenMW.

setlocal

REM ---- Initialize MSVC 2019 x64 environment ----
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo ERROR: MSVC 2019 Build Tools not found.
    echo Install via: Visual Studio Installer, MSVC v142 build tools ^(x64^)
    exit /b 1
)
call "%VCVARS%" >nul 2>&1
echo MSVC 2019 x64 environment initialized.

REM ---- Compile and link ----
echo Compiling launch_openmw.c...
cl /nologo /O2 /W3 launch_openmw.c /Fe:launch_openmw.exe /link /subsystem:windows kernel32.lib
if errorlevel 1 ( echo Compile failed. && exit /b 1 )

echo.
echo SUCCESS: launch_openmw.exe built.
echo.
echo Deploy: place launch_openmw.exe in the Morrowind install directory
echo alongside hollowind_paths.cfg and morrowind_watcher.ps1.

endlocal
