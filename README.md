# Morrowind: PipBoy Edition — Source

**Version 1.22.3**

Full source tree for **Morrowind: PipBoy Edition** (also known as Hollo-Wind).

Feel Free to use this source as a guide to create new projects. All I ask 
is that you provide proper credit. 

This mod renders The Elder Scrolls III: Morrowind — running via OpenMW — live
inside the Fallout 4 Pip-Boy screen. OpenMW runs as a hidden process; its
framebuffer is captured by a proxy DLL, written to shared memory, and injected
into Fallout 4's render pipeline by an F4SE plugin, which displays it on the
Pip-Boy when the Morrowind holotape is inserted.

> **Note:** As of v1.22.3 this mod works with a standard **OpenMW 0.50.0**
> install. The custom-compiled OpenMW build previously required is no longer
> needed.

---

## Directory Overview

| Directory | What it produces | Build platform |
|---|---|---|
| `OpenMWPatcherSRC\` | `SDL2.dll` — OpenMW framebuffer capture proxy | Windows (MSVC) |
| `MorrowindLauncherSRC\` | `MorrowindLauncher.dll` — F4SE plugin, D3D11 injection | Linux (mingw-w64) |
| `LaunchOpenMWSRC\` | `launch_openmw.exe` — shim that starts the watcher | Windows (MSVC) |
| `InstallerExecutable\` | `MorrowindPipBoyEdition_Setup.exe` — setup wizard | Windows (.NET/csc) |
| `Morrowind_Runtime\` | `morrowind_watcher.ps1` — shipped as-is | — |
| `OpenMW_Config\` | OpenMW config files — shipped as-is | — |
| `Fallout4_Config\` | `Fallout4Custom.ini` — shipped as-is | — |
| `Fallout4_Assets\` | ESP, BA2, meshes, textures, SWF — shipped as-is | — |
| `Morrowind_Assets\` | `mw_logo.bik` intro video — shipped as-is | — |

---

## Build Order

Each component must be built in its own directory before running `assemble.bat`.

### 1. OpenMWPatcherSRC — SDL2.dll
Requires: MSVC 2019+ Build Tools (x64)

```
cd OpenMWPatcherSRC
build.bat
```

Output: `OpenMWPatcherSRC\SDL2.dll`

See `OpenMWPatcherSRC\README.md` for full details.

---

### 2. MorrowindLauncherSRC — MorrowindLauncher.dll
Requires: Linux with mingw-w64 cross-compiler and CMake

```
cd MorrowindLauncherSRC
mkdir build && cd build
cmake .. -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ -DCMAKE_BUILD_TYPE=Release
make
```

Copy the resulting `MorrowindLauncher.dll` into `MorrowindLauncherSRC\` before
running `assemble.bat`.

See `MorrowindLauncherSRC\README.md` for full details.

---

### 3. LaunchOpenMWSRC — launch_openmw.exe
Requires: MSVC 2019+ Build Tools (x64)

```
cd LaunchOpenMWSRC
build.bat
```

Output: `LaunchOpenMWSRC\launch_openmw.exe`

See `LaunchOpenMWSRC\README.md` for full details.

---

### 4. InstallerExecutable — MorrowindPipBoyEdition_Setup.exe
Requires: .NET Framework 4.x (csc.exe)

```
cd InstallerExecutable
build.bat
```

Output: `InstallerExecutable\MorrowindPipBoyEdition_Setup.exe`

See `InstallerExecutable\README.md` for full details.

---

## Assemble the Distribution

Once all four components above are built, run from this directory:

```
assemble.bat
```

This copies all built artifacts and static assets into `dist\`, which mirrors
the final `Morrowind_PipBoy_Edition` distribution folder exactly.

---

## How It Works

1. **Holotape insertion** — The player inserts the Morrowind holotape into the
   Pip-Boy. The `MorrowindDisplay.swf` Scaleform program loads, signalling the
   F4SE plugin that the Pip-Boy is active.

2. **OpenMW launch** — The F4SE plugin (`MorrowindLauncher.dll`) writes a
   trigger file. `launch_openmw.exe` detects this and starts
   `morrowind_watcher.ps1`, which configures OpenMW for headless 876×700
   rendering and launches `openmw.exe`.

3. **Frame capture** — The proxy `SDL2.dll` intercepts `SDL_GL_SwapWindow`,
   reads the OpenGL framebuffer, scales it to 876×700, and writes it to a
   Windows named shared memory segment (`MorrowindPipboyBridge`).

4. **Frame injection** — `MorrowindLauncher.dll` hooks
   `ID3D11DeviceContext::OMSetRenderTargets` (vtable index 33). When Scaleform
   binds the 876-wide Pip-Boy render target, the plugin copies the shared
   memory frame into it, replacing the Pip-Boy UI with the live Morrowind view.

5. **Input forwarding** — Raw keyboard and mouse input captured in Fallout 4
   is forwarded through shared memory to OpenMW, making the Pip-Boy screen
   interactable.

6. **Shutdown** — Pressing F9 or ejecting the holotape writes a shutdown signal
   file. The watcher detects it, terminates OpenMW, restores settings, and
   cleans up signal files.
