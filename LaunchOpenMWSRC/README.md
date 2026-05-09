# launch_openmw — Build Instructions

## What this builds

`launch_openmw.exe` — a small Win32 shim that acts as the bridge between the
Fallout 4 F4SE plugin and the Morrowind/OpenMW side of the mod.

The F4SE plugin calls `CreateProcess(OpenMWPath, ...)` when the player presses
F9. Windows `CreateProcess` cannot launch a `.ps1` or `.bat` file directly, so
`OpenMWPath` is pointed at this exe instead. When it runs it does two things:

1. Starts `morrowind_watcher.ps1` as a detached background process (keeps
   running after this exe exits)
2. Writes the trigger file that tells the watcher to immediately launch OpenMW

It reads both paths from `hollowind_paths.cfg` in its own directory so it knows
where the trigger file and watcher script live.

## Files

| File | Role |
|---|---|
| `launch_openmw.c` | Full C source — the only file needed to build the exe |
| `build.bat` | Compiles `launch_openmw.c` → `launch_openmw.exe` |

## Prerequisites

**MSVC 2019 Build Tools (x64)** — `build.bat` initializes the environment via
`vcvars64.bat` automatically. No Developer Command Prompt needed.

Install via: Visual Studio Installer → Individual Components →
"MSVC v142 - VS 2019 C++ x64/x86 build tools"

## How to build

1. Open File Explorer and navigate to this folder.
2. Click the address bar, type `cmd`, press Enter.
3. Type `build.bat` and press Enter.

Output: `launch_openmw.exe` in this folder (~12 KB).

A few warnings about `fopen` and `atoi` are expected and harmless — the exe
compiles and runs correctly.

## Deploying the output

`launch_openmw.exe` lives in the **Morrowind install directory**, alongside the
other files the watcher needs:

```
Morrowind install directory:
  launch_openmw.exe       ← the file built here
  hollowind_paths.cfg     ← written by the installer, tells this exe where things are
  morrowind_watcher.ps1   ← the PS1 the watcher starts as a detached process
```

`MorrowindLauncher.ini` (in Fallout 4's `Data\F4SE\Plugins\`) points
`OpenMWPath` at this exe. When F9 is pressed in-game, the F4SE plugin calls
`CreateProcess` on it.
