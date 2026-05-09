# Fallout4_Config

Fallout 4 configuration file merged into the player's `Fallout4Custom.ini`
by the installer.

## Files

| File | Deploy location | Role |
|---|---|---|
| `Fallout4Custom.ini` | `Documents\My Games\Fallout4\Fallout4Custom.ini` | Required INI settings for the mod to function |

## Fallout4Custom.ini

The installer merges these keys into the player's existing `Fallout4Custom.ini`
rather than replacing the file outright. Key settings:

| Section | Key | Value | Why |
|---|---|---|---|
| `[Display]` | `iLocation X/Y` | `0` | Keeps FO4 window at screen origin |
| `[General]` | `bAlwaysActive` | `1` | Keeps FO4 running while OpenMW has focus — required so the F4SE plugin keeps processing input and the bridge stays active |
| `[Archive]` | `bInvalidateOlderFiles` | `1` | Allows loose mod files to override BA2 archives |
| `[Archive]` | `sResourceDataDirsFinal` | `STRINGS\, INTERFACE\` | Required for F4SE mods that add interface files |
| `[Archive]` | `sResourceArchiveList2` | `Fallout4 - Animations.ba2` | Keeps the base animation archive loaded alongside the mod's BA2 |
