# MorrowindLauncher — F4SE Plugin Build Instructions

## Why this build is different from the SDL2 proxy

The SDL2 capture proxy (`SDL2.dll`) was built on Windows using MSVC and a
simple batch file. This project is different because it was **originally
developed and built on Pop!_OS Linux**. Rather than port the build environment
to Windows, it uses **mingw-w64** — a Linux toolchain that cross-compiles
directly to Windows DLLs. The `f4se_stubs.h` file exists for the same reason:
the full F4SE SDK assumes MSVC, so minimal compatible stubs were written to
keep the build self-contained and Linux-friendly.

The end result is identical — a native Windows x64 DLL — but the toolchain and
build steps are entirely Linux-based.

## What this builds

`MorrowindLauncher.dll` — an F4SE plugin for Fallout 4 that bridges Morrowind
(running via OpenMW) into the Pip-Boy screen (Hollo-Wind project).

It hooks Fallout 4's D3D11/DXGI rendering pipeline and monitors the
`OMSetRenderTargets` call stream to detect when Scaleform (Fallout 4's
SWF-based UI system) is actively rendering to the Pip-Boy render target. This
is how the plugin knows the Morrowind holotape has been inserted and the Pip-Boy
is open — Scaleform only draws to that render target when the holotape program
is running on screen. The F9 key will only launch Morrowind while that condition
is true; pressing F9 with the Pip-Boy closed does nothing.

Once the holotape is detected as active, the plugin reads frames written by the
OpenMW SDL2 capture proxy from shared memory (`MorrowindPipboyBridge`) and
injects them directly into the Pip-Boy's D3D11 texture each time Scaleform
finishes a frame — overwriting whatever Scaleform drew with the live Morrowind
output. It also forwards keyboard and mouse input from Fallout 4 into the shared
memory for OpenMW to consume, and handles Tab to cleanly eject the holotape.

## Files

| File | Role |
|---|---|
| `CMakeLists.txt` | CMake build definition — configures the DLL target, linker flags, and post-build copy |
| `src/main.cpp` | All plugin logic: F4SE registration, D3D11/DXGI vtable hooks, frame copy, process launch, input forwarding |
| `src/f4se_stubs.h` | Minimal F4SE type definitions that allow cross-compilation with mingw-w64 without the full F4SE SDK |
| `src/MorrowindBridge.h` | Shared memory bridge layout — must match the layout in the SDL2 proxy exactly |
| `src/papyrus_native.h` | Papyrus native function registration helpers (currently disabled, pending correct FO4 1.11.191 offsets) |

## Prerequisites

This project **cross-compiles on Linux** using **mingw-w64**. It does not use
MSVC and does not require a Windows build environment.

- **CMake** 3.15 or newer
- **mingw-w64** x86-64 C++ cross-compiler (`x86_64-w64-mingw32-g++`)
- No external SDK needed — `f4se_stubs.h` replaces the F4SE headers

Install on Debian/Ubuntu:
```
sudo apt install cmake mingw-w64
```

## How to build

1. Open a terminal in this folder.

2. Create a build directory and run CMake:
   ```
   mkdir build && cd build
   cmake .. -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
            -DCMAKE_BUILD_TYPE=Release
   ```

3. Compile:
   ```
   make
   ```

4. Output: `MorrowindLauncher.dll` in the `build/` directory (~53 KB).

The post-build step in CMakeLists.txt also copies the DLL to
`../Mod/Data/F4SE/Plugins/MorrowindLauncher.dll` if that folder exists.

## Deploying the output

The built DLL goes into the **Fallout 4 `Data\F4SE\Plugins\` directory**:

```
Fallout 4 install directory:
  Data\
    F4SE\
      Plugins\
        MorrowindLauncher.dll   ← the file built here
        MorrowindLauncher.ini   ← companion config (written by the installer)
```

`MorrowindLauncher.ini` is required at runtime and must contain:

```ini
[General]
OpenMWPath=<full path to launch_openmw.exe>
MorrowindDataPath=<full path to Morrowind\Data Files>
ShutdownFile=<full path to the IPC shutdown file>
```

The Hollo-Wind installer writes this file automatically with the correct paths.
If configuring manually, make sure all three values are set.

## Runtime dependencies

- **F4SE** (Fallout 4 Script Extender) must be installed — the plugin is loaded
  by `f4se_loader.exe`, not by Fallout 4 directly.
- The **SDL2 capture proxy** (`SDL2.dll`) must be deployed to the OpenMW
  installation directory, as it is what writes frames into the shared memory
  that this plugin reads.
- Fallout 4 runtime version **1.11.191** (next-gen) with **F4SE 0.7.7**.

## Notes

- The DLL statically links libgcc and libstdc++ so no mingw runtime DLLs are
  needed on the target Windows machine.
- Papyrus native registration is currently disabled (`#define DISABLE_PAPYRUS_REGISTER`
  in `main.cpp`) pending correct vtable offsets for FO4 1.11.191. The F9 key
  handler works regardless.
