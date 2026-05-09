# Morrowind_Runtime

Files that live in the **Morrowind install directory** alongside `hollowind_paths.cfg`
and `launch_openmw.exe`.

## Files

| File | Role |
|---|---|
| `morrowind_watcher.ps1` | PowerShell daemon that manages the entire OpenMW lifecycle during a play session |

## morrowind_watcher.ps1

This script is the runtime coordinator for the mod. It is started as a detached
background process by `launch_openmw.exe` each time the player presses F9.

On startup it reads `hollowind_paths.cfg` from its own directory to get all
paths (trigger file, shutdown file, bridge file, OpenMW exe, Morrowind dir).
It then enters a loop watching for IPC signal files:

- **Trigger file** — when `launch_openmw.exe` writes this file the watcher
  overwrites `Documents\My Games\OpenMW\settings.cfg` with the Pip-Boy streaming
  settings (876×700, no cursor, no fullscreen), then starts `openmw.exe`.
- **Shutdown file** — when the F4SE plugin writes this file (player pressed Tab
  or F9 again) the watcher kills OpenMW and unlocks `settings.cfg`.
- **FO4 PID** — if launched with `-FO4PID`, the watcher monitors that process
  and self-exits when Fallout 4 closes, killing OpenMW cleanly.

A named mutex (`Global\MorrowindPipBoyEditionWatcher`) prevents multiple
watcher instances from stacking up across sessions. If a stale instance is
detected the new one takes over.

## Deploy location

```
<Morrowind install dir>\
  morrowind_watcher.ps1    ← this file
  hollowind_paths.cfg      ← written by the installer
  launch_openmw.exe        ← built from LaunchOpenMWSRC
```
