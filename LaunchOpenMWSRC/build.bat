@echo off
REM build.bat  --  Builds launch_openmw.exe
REM Requires: MSVC 2019+ Build Tools (x64)
REM
REM launch_openmw.exe is a tiny Win32 shim called by the F4SE plugin when the
REM player presses F9. It starts morrowind_watcher.ps1 (detached) and writes
REM the trigger file that tells the watcher to launch OpenMW.

setlocal

REM ---- Locate MSVC via vswhere ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    echo Install Visual Studio 2017 or newer with C++ build tools, then retry.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)
if not defined VS_PATH (
    echo ERROR: No Visual Studio installation with C++ build tools found.
    echo Install the "MSVC v14x - VS 20xx C++ x64/x86 build tools" component.
    exit /b 1
)
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>&1
echo MSVC x64 environment initialized.

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
