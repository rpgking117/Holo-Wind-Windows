# OpenMW_Config

OpenMW configuration files deployed by the installer. None of these are
generated at build time — they are hand-tuned for Pip-Boy streaming and
copied as-is into the appropriate locations.

## Files

| File | Deploy location | Role |
|---|---|---|
| `openmw.cfg` | `<OpenMW install dir>\openmw.cfg` | Global OpenMW config — script blacklists, fallback engine settings, lighting and water tuning |
| `userdata\settings.cfg` | `Documents\My Games\OpenMW\settings.cfg` | Initial resolution and display settings (876×700, no cursor, no fullscreen) |
| `userdata\input_v3.xml` | `Documents\My Games\OpenMW\input_v3.xml` | Input bindings — customised so keyboard/mouse pass through to the Pip-Boy bridge correctly |
| `userdata\shaders.yaml` | `Documents\My Games\OpenMW\shaders.yaml` | Shader toggle state |
| `pipboy_config\settings.cfg` | `Documents\My Games\OpenMW\settings.cfg` (template) | Alternative settings template at 1024×1024 — the watcher overwrites this with 876×700 at each launch |

## openmw.cfg

Placed in the OpenMW install directory (next to `openmw.exe`). Key sections:

- **Script blacklist** — suppresses several scripts known to cause crashes or
  hangs when running headlessly (`Museum`, `MockChangeScript`, `WereChange2Script`,
  etc.)
- **Lighting fallbacks** — linear attenuation tuned for readable Pip-Boy output
- **Font color fallbacks** — required by MyGUI so menus render correctly
- **Water fallbacks** — texture size and animation rate kept low for streaming

## userdata\settings.cfg

Written to `Documents\My Games\OpenMW\` by the installer. Sets:
- Resolution 876×700 (matches the frame buffer the SDL2 proxy captures)
- Windowed, no minimize-on-focus-loss (window must stay alive while hidden)
- `grab cursor = false` (cursor is managed by the SDL2 proxy's software cursor)
- `crosshair = false` (not visible on Pip-Boy screen)

The watcher re-writes this file with the same settings each time OpenMW
launches (and marks it read-only) to prevent OpenMW from overwriting it on
exit. It is unlocked again after OpenMW closes.

## userdata\input_v3.xml

Standard OpenMW input binding file. Customised so that navigation actions
work correctly when input is forwarded from Fallout 4 via the shared memory
bridge rather than from a real keyboard/mouse attached to the OpenMW window.

## pipboy_config\settings.cfg

A 1024×1024 variant of the settings file used as the initial template deployed
to `Documents\My Games\OpenMW\`. The watcher overwrites it with 876×700 at
launch time.
