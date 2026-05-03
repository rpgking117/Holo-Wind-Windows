# Morrowind: Pip-Boy Edition — v1.22.0

**Morrowind. On your Pip-Boy. For real.**

Holo-Wind streams The Elder Scrolls III: Morrowind live into the Fallout 4 Pip-Boy screen using a custom-modified build of OpenMW 0.50 — bridged directly into the game engine via shared memory and a dedicated F4SE plugin. Insert the Morrowind Holotape, open your Pip-Boy, and the rest is up to you..

---

> Created by [rpgking117](https://youtube.com/@rpgking117)

---

## What It Does

OpenMW runs in a hidden window locked to 876x700 which gets upscaled to 1024x1024 and streams its framebuffer directly into Fallout 4's Pip-Boy display in real time. A custom F4SE plugin handles the holotape trigger, the shared-memory bridge, and input passthrough so keyboard controls reach Morrowind while you're in-game.

---

## Requirements

| Requirement | Notes |
|---|---|
| Windows 10 / 11 (64-bit) | Required |
| Steam | Required |
| Fallout 4 (Steam) | Must own |
| The Elder Scrolls III: Morrowind (Steam) | Must own |
| [F4SE — Fallout 4 Script Extender](https://nexusmods.com/fallout4/mods/42147) | Must install before launching |
| Most OpenMW compatible mods should just work |

---

## ⚠️ Back Up Your OpenMW Settings First

The installer **overwrites** these files:

```
%USERPROFILE%\Documents\My Games\OpenMW\settings.cfg
%USERPROFILE%\Documents\My Games\OpenMW\input_v3.xml
%USERPROFILE%\Documents\My Games\OpenMW\shaders.yaml
```

It forces a fixed 876×700 windowed resolution and disables the cursor and crosshair. **These changes will "break" standalone OpenMW.**

**DISCLAIMER**
This version of OpenMW is tailered to this project. 
You may not be able to run it normally on the desktop and if you do manager
it may break features of this mod. 

Before running the installer, copy your entire `%USERPROFILE%\Documents\My Games\OpenMW\` folder somewhere safe.

---

## Installation

1. Back up your OpenMW settings folder (see above).
2. Run `HolloWindSetup.exe` as Administrator.
3. Click **INSTALL** and let it finish.
4. Install F4SE from Nexus Mods if you haven't already.

---

## How to Play

1. Launch Fallout 4 via `f4se_loader.exe` — **not** the normal launcher.
2. Load any save.
3. Open the console and type `help morrowind 4`, then `player.additem XX000800` to add the Morrowind Holotape.
4. Open your Pip-Boy → **MISC** → select and insert the Morrowind Holotape.
5. Morrowind launches and appears on the Pip-Boy screen.
6. Press **Tab** to close Morrowind and return to Fallout 4.

---

## Credits

- **OpenMW** — Open-source Morrowind engine re-implementation by the OpenMW Contributors. Licensed under [GPL v3](https://gnu.org/licenses/gpl-3.0.txt). This mod ships a modified build of OpenMW 0.50 that adds a shared-memory framebuffer and input bridge. The unmodified source is at [github.com/OpenMW/openmw](https://github.com/OpenMW/openmw).
- **F4SE** — Fallout 4 Script Extender by the F4SE Team. [nexusmods.com/fallout4/mods/42147](https://nexusmods.com/fallout4/mods/42147)
- **Holo-Wind** — concept, F4SE plugin, installer, and integration code by [rpgking117](https://youtube.com/@rpgking117)

---

## Terms of Use

This is a fan-made modification for personal use. It is not affiliated with, endorsed by, or sponsored by Bethesda Softworks, ZeniMax Media, or the OpenMW project. All trademarks and copyrights belong to their respective owners. You must own legal copies of Morrowind and Fallout 4 to use this mod.

This software is provided **as-is** with no warranty. The author is not liable for data loss, save corruption, or configuration damage. Back up your OpenMW settings before installing.

You may share this installer freely in unmodified form with proper credit. Do not redistribute modified versions without permission.
