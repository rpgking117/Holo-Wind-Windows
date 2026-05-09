# Morrowind: PipBoy Edition — Installer Source

## What this builds

`MorrowindPipBoyEdition_Setup.exe` — a self-contained Windows setup wizard that
walks the user through installing the mod. The PowerShell installer script is
embedded directly inside the exe as a .NET resource. When the user clicks
Install, the exe extracts the PS1 to a temp folder and runs it via PowerShell,
passing the three game paths the user selected.

## Files

| File | Role |
|---|---|
| `MorrowindPipBoySetup.cs` | C# WinForms source — the setup wizard UI and PS1 runner |
| `MorrowindPipBoyEdition_Setup.ps1` | The PowerShell installer script — embedded into the exe at compile time |
| `app.manifest` | UAC manifest — requests Administrator elevation on launch |
| `build.bat` | Compiles everything into `MorrowindPipBoyEdition_Setup.exe` |

## Prerequisites

**.NET Framework 4.x** — ships with Windows 10 and 11. No install needed.

The build uses `csc.exe` from `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\`,
which is present on all modern Windows machines.

## How to build

1. Open File Explorer, navigate to this folder.
2. Click the address bar, type `cmd`, press Enter.
3. Type `build.bat` and press Enter.

Output: `MorrowindPipBoyEdition_Setup.exe` in this folder.

## How the PS1 is embedded

`build.bat` passes `/res:MorrowindPipBoyEdition_Setup.ps1` to `csc.exe`, which
stores the script as a .NET manifest resource inside the compiled exe. At
runtime, `MorrowindPipBoySetup.cs` reads it back out with:

```csharp
Assembly.GetExecutingAssembly().GetManifestResourceStream("MorrowindPipBoyEdition_Setup.ps1")
```

It writes the script to a temp folder, runs it via PowerShell, then deletes the
temp folder when the window closes.

## Updating the installer script

Edit `MorrowindPipBoyEdition_Setup.ps1`, then run `build.bat` again. The new
script will be embedded in the next exe.

## Where the built exe must live (for distribution)

The exe must be placed in the **same folder as the `build\` directory**. The PS1
uses `-InstallerDir` (the exe's own folder) to locate `build\` at runtime:

```
MorrowindPipBoyEdition_Setup.exe   ← the built exe
build\
  OpenMW Patch\
    SDL2.dll
    SDL2_orig.dll
    launch_openmw.exe
  Fallout 4\
    Data\
      F4SE\Plugins\MorrowindLauncher.dll
      MorrowindLauncher - Main.ba2
      MorrowindLauncher.esp
      Interface\Programs\MorrowindDisplay.swf
      Meshes\HolloWind\...
      Textures\HolloWind\...
  Morrowind\
    morrowind_watcher.ps1
    Data Files\Video\mw_logo.bik
  openmw_runtime\...
  openmw_userdata\...
  MorrowindLauncher.dll
  Fallout4Custom.ini
```

The full `build\` directory structure is already assembled at
`C:\Users\NateD\Desktop\Morrowind_PipBoy_Edition\build\`.
