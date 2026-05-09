@echo off
REM build.bat  --  Hollo-Wind Windows capture DLL builder
REM Requires: MSVC 2019+ Build Tools (x64)
REM Run this AFTER placing SDL2_orig.dll in this folder
REM (copy SDL2.dll from the OpenMW install, rename it SDL2_orig.dll)

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

REM ---- Generate SDL2 proxy DEF ----
if not exist "SDL2_orig.dll" (
    echo ERROR: SDL2_orig.dll not found.
    echo Copy SDL2.dll from OpenMW installation and rename to SDL2_orig.dll.
    exit /b 1
)

echo Generating SDL2 proxy DEF file...
python generate_proxy_def.py SDL2_orig.dll
if errorlevel 1 ( echo DEF generation failed. && exit /b 1 )

REM ---- Compile pipboy_capture.c ----
echo Compiling pipboy_capture.c...
cl /nologo /O2 /W3 /MD /c pipboy_capture.c /Fopipboy_capture.obj
if errorlevel 1 ( echo Compile failed. && exit /b 1 )

REM ---- Build SDL2_orig import library ----
echo Building SDL2_orig import library...
lib /nologo /def:SDL2_orig.def /machine:x64 /out:SDL2_orig.lib
if errorlevel 1 ( echo lib.exe failed. && exit /b 1 )

REM ---- Link SDL2.dll ----
echo Linking SDL2.dll...
link /nologo /DLL /OUT:SDL2.dll /DEF:SDL2_proxy.def /MACHINE:X64 ^
    pipboy_capture.obj SDL2_orig.lib kernel32.lib user32.lib
if errorlevel 1 ( echo Link failed. && exit /b 1 )

echo.
echo SUCCESS: SDL2.dll built.
echo.
echo Deploy steps:
echo   1. Copy SDL2.dll      to the OpenMW install directory.
echo   2. Copy SDL2_orig.dll to the same directory (renamed from the real SDL2.dll).
echo   That is, in the OpenMW folder:
echo      SDL2.dll      = our capture proxy (this file)
echo      SDL2_orig.dll = original SDL2 (renamed)

endlocal
