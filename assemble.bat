@echo off
REM assemble.bat -- Assembles the full Morrowind_PipBoy_Edition distribution
REM folder from already-built component outputs.
REM
REM Run this from C:\Users\NateD\Desktop\MPBE_Source\ AFTER building each
REM component in its own directory:
REM
REM   1. OpenMWPatcherSRC\build.bat      -> produces OpenMWPatcherSRC\SDL2.dll
REM   2. LaunchOpenMWSRC\build.bat       -> produces LaunchOpenMWSRC\launch_openmw.exe
REM   3. InstallerExecutable\build.bat   -> produces InstallerExecutable\MorrowindPipBoyEdition_Setup.exe
REM   4. MorrowindLauncherSRC\ (Linux)   -> produces MorrowindLauncherSRC\MorrowindLauncher.dll
REM
REM Output: dist\  (matches Morrowind_PipBoy_Edition exactly)
REM
REM NOTE: MorrowindLauncher.dll must be pre-built on Linux using
REM   MorrowindLauncherSRC\ (mingw-w64 cross-compiler).
REM   Place the compiled MorrowindLauncher.dll in MorrowindLauncherSRC\ before
REM   running this script.

setlocal
set "HERE=%~dp0"
set "DIST=%HERE%dist"

echo.
echo =====================================================
echo   Morrowind: PipBoy Edition -- Assembler
echo =====================================================
echo.

REM ---- Verify all built artifacts are present --------------------------------

if not exist "%HERE%OpenMWPatcherSRC\SDL2.dll" (
    echo ERROR: SDL2.dll not found. Run OpenMWPatcherSRC\build.bat first.
    pause & exit /b 1
)

if not exist "%HERE%LaunchOpenMWSRC\launch_openmw.exe" (
    echo ERROR: launch_openmw.exe not found. Run LaunchOpenMWSRC\build.bat first.
    pause & exit /b 1
)

if not exist "%HERE%InstallerExecutable\MorrowindPipBoyEdition_Setup.exe" (
    echo ERROR: MorrowindPipBoyEdition_Setup.exe not found. Run InstallerExecutable\build.bat first.
    pause & exit /b 1
)

if not exist "%HERE%MorrowindLauncherSRC\MorrowindLauncher.dll" (
    echo ERROR: MorrowindLauncher.dll not found in MorrowindLauncherSRC\
    echo Build it on Linux first:
    echo   cd MorrowindLauncherSRC ^&^& mkdir build ^&^& cd build
    echo   cmake .. -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ -DCMAKE_BUILD_TYPE=Release
    echo   make
    echo Then copy MorrowindLauncher.dll here before running assemble.bat.
    pause & exit /b 1
)

REM ---- Assemble dist\ ---------------------------------------------------------
echo Assembling dist\ ...

set "BUILD=%DIST%\build"

REM Top-level
xcopy /y /q "%HERE%InstallerExecutable\MorrowindPipBoyEdition_Setup.exe" "%DIST%\"

REM build\ root
xcopy /y /q "%HERE%Fallout4_Config\Fallout4Custom.ini"                   "%BUILD%\"
xcopy /y /q "%HERE%MorrowindLauncherSRC\MorrowindLauncher.dll"           "%BUILD%\"

REM build\Fallout 4\Data\
set "FO4DATA=%BUILD%\Fallout 4\Data"
xcopy /y /q "%HERE%Fallout4_Assets\MorrowindLauncher.esp"                "%FO4DATA%\"
xcopy /y /q "%HERE%Fallout4_Assets\MorrowindLauncher - Main.ba2"         "%FO4DATA%\"

xcopy /y /q /s "%HERE%Fallout4_Assets\Interface\*"  "%FO4DATA%\Interface\"
xcopy /y /q /s "%HERE%Fallout4_Assets\Meshes\*"     "%FO4DATA%\Meshes\"
xcopy /y /q /s "%HERE%Fallout4_Assets\Textures\*"   "%FO4DATA%\Textures\"

xcopy /y /q "%HERE%MorrowindLauncherSRC\MorrowindLauncher.dll"  "%FO4DATA%\F4SE\Plugins\"

REM build\Morrowind\
xcopy /y /q "%HERE%Morrowind_Runtime\morrowind_watcher.ps1"              "%BUILD%\Morrowind\"
xcopy /y /q /s "%HERE%Morrowind_Assets\*"                                "%BUILD%\Morrowind\"

REM build\OpenMW Patch\
xcopy /y /q "%HERE%OpenMWPatcherSRC\SDL2.dll"                            "%BUILD%\OpenMW Patch\"
xcopy /y /q "%HERE%OpenMWPatcherSRC\SDL2_orig.dll"                       "%BUILD%\OpenMW Patch\"
xcopy /y /q "%HERE%LaunchOpenMWSRC\launch_openmw.exe"                    "%BUILD%\OpenMW Patch\"

REM build\openmw_runtime\
xcopy /y /q "%HERE%OpenMW_Config\openmw.cfg"                             "%BUILD%\openmw_runtime\openmw_runtime\"
xcopy /y /q "%HERE%OpenMW_Config\pipboy_config\settings.cfg"             "%BUILD%\openmw_runtime\openmw_runtime\pipboy_config\"

REM build\openmw_userdata\
xcopy /y /q "%HERE%OpenMW_Config\userdata\settings.cfg"                  "%BUILD%\openmw_userdata\"
xcopy /y /q "%HERE%OpenMW_Config\userdata\input_v3.xml"                  "%BUILD%\openmw_userdata\"
xcopy /y /q "%HERE%OpenMW_Config\userdata\shaders.yaml"                  "%BUILD%\openmw_userdata\"

echo.
echo =====================================================
echo   DONE. Distribution assembled at:
echo   %DIST%
echo =====================================================
echo.

endlocal
