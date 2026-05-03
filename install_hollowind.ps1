#Requires -Version 5.0
<#
.SYNOPSIS
    Hollo-Wind Windows Installer  --  Morrowind on the Fallout 4 Pip-Boy

.DESCRIPTION
    Single-source-of-truth installer.  Once it knows where Fallout 4 and
    Morrowind are installed it computes ALL paths, then:
      - Patches MorrowindLauncher.dll with those exact paths
      - Writes MorrowindLauncher.ini with those exact paths
      - Writes hollowind_paths.cfg so the watcher uses the same paths
      - Installs OpenMW 0.48.0 into the Morrowind directory
      - Builds the SDL2 capture proxy DLL (MSVC 2019 required)
      - Deploys all Fallout 4 mod files and Papyrus scripts
      - Merges required Fallout4Custom.ini settings
      - Enables the ESP in Plugins.txt
      - Creates a desktop shortcut for the watcher

.PARAMETER FO4Path       Override Fallout 4 install path
.PARAMETER MorrowindPath Override Morrowind install path
.PARAMETER IpcDir        Override IPC directory (default C:\HolloWind)
                         Max path lengths: trigger <= 37, shutdown <= 39, bridge <= 30
.PARAMETER SkipBuild     Skip DLL compilation (use pre-built SDL2.dll)
.PARAMETER SkipDownload  Skip OpenMW download (assume openmw.exe already present)
.PARAMETER Uninstall     Remove all installed components
#>
param(
    [string]$FO4Path       = "",
    [string]$MorrowindPath = "",
    [string]$OpenMWPath    = "",
    [string]$IpcDir        = "C:\HolloWind",
    [switch]$SkipBuild,
    [switch]$Uninstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SCRIPT_DIR    = Split-Path -Parent $MyInvocation.MyCommand.Path
$WINDOWS_DIR   = Split-Path -Parent $SCRIPT_DIR   # Hollo-Wind-Windows\
$INSTALLER_DIR = Join-Path (Split-Path -Parent $WINDOWS_DIR) "Hollo-Wind-Installer"
$BUILD_DIR     = Join-Path $WINDOWS_DIR "build"
$RUNTIME_DIR   = Join-Path $WINDOWS_DIR "OpenMW_Runtime"
$MORROWIND_SRC = Join-Path (Split-Path -Parent $WINDOWS_DIR) "Morrowind"

# IPC path constraint limits (slot size = original string length)
$MAX_TRIGGER  = 37   # "Z:\home\npearson\.morrowind_launch"
$MAX_SHUTDOWN = 39   # "Z:\home\npearson\.morrowind_shutdown"
$MAX_BRIDGE   = 30   # "Z:\tmp\morrowind_pipboy_bridge"

# Bridge must use C:\tmp\ to fit in 30 chars with the fixed filename
$BRIDGE_FILE  = "C:\tmp\morrowind_pipboy_bridge"

# ---- Helpers ---------------------------------------------------------------

function Step($n, $msg) {
    Write-Host "`n[$n] $msg" -ForegroundColor Cyan
}
function OK($msg)   { Write-Host "     OK   $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "     WARN $msg" -ForegroundColor Yellow }
function Fail($msg) { Write-Host "     FAIL $msg" -ForegroundColor Red; throw $msg }

# ---- Steam path discovery --------------------------------------------------

function Find-SteamLibraries {
    $roots = @()
    foreach ($reg in @("HKLM:\SOFTWARE\WOW6432Node\Valve\Steam","HKLM:\SOFTWARE\Valve\Steam")) {
        try { $p = (Get-ItemProperty $reg -EA Stop).InstallPath; if ($p -and (Test-Path $p)) { $roots += $p } } catch {}
    }
    foreach ($root in @($roots)) {
        $vdf = Join-Path $root "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            Get-Content $vdf | ForEach-Object {
                if ($_ -match '"path"\s+"([^"]+)"') { $lib = $Matches[1] -replace '\\\\','\'; if (Test-Path $lib) { $roots += $lib } }
            }
        }
    }
    return $roots | Select-Object -Unique
}

function Find-Game($libs, $name) {
    foreach ($l in $libs) { $p = Join-Path $l "steamapps\common\$name"; if (Test-Path $p) { return $p } }
    return $null
}

# ---- IPC path validation ---------------------------------------------------

function Validate-IpcDir($dir) {
    $dir = $dir.TrimEnd('\')
    $t = $dir + "\.morrowind_launch"
    $s = $dir + "\.morrowind_shutdown"
    if ($t.Length -gt $MAX_TRIGGER)  { Fail "IPC dir too long: trigger path is $($t.Length) chars (max $MAX_TRIGGER): $t" }
    if ($s.Length -gt $MAX_SHUTDOWN) { Fail "IPC dir too long: shutdown path is $($s.Length) chars (max $MAX_SHUTDOWN): $s" }
    if ($BRIDGE_FILE.Length -gt $MAX_BRIDGE) { Fail "Bridge path is $($BRIDGE_FILE.Length) chars (max $MAX_BRIDGE)" }
    return $dir
}

# ===========================================================================
# MAIN
# ===========================================================================

Write-Host ""
Write-Host "==========================================" -ForegroundColor Magenta
Write-Host "  HOLLO-WIND  -  Morrowind on the Pip-Boy" -ForegroundColor Magenta
Write-Host "  Windows Installer                       " -ForegroundColor Magenta
Write-Host "==========================================" -ForegroundColor Magenta

if ($Uninstall) {
    Write-Host "`nUninstalling..." -ForegroundColor Yellow
    $manifestFile = Join-Path $env:LOCALAPPDATA "HolloWind\install_manifest.txt"
    if (Test-Path $manifestFile) {
        Get-Content $manifestFile | ForEach-Object { Remove-Item $_ -Force -EA SilentlyContinue }
        OK "Files removed per manifest"
    } else { Warn "No manifest found - remove files manually" }
    & schtasks /Delete /TN "HolloWind-StartWatcher" /F 2>$null
    Remove-Item (Join-Path ([Environment]::GetFolderPath("Desktop")) "Hollo-Wind Watcher.lnk") -EA SilentlyContinue
    OK "Uninstall complete"
    exit 0
}

# ---------------------------------------------------------------------------
# STEP 1: Detect game paths
# ---------------------------------------------------------------------------
Step 1 "Detecting game installations"

$libs = Find-SteamLibraries

if (-not $FO4Path) {
    $FO4Path = Find-Game $libs "Fallout 4"
    if (-not $FO4Path) {
        Write-Host ""
        $FO4Path = Read-Host "  Fallout 4 install path"
    }
}
if (-not (Test-Path (Join-Path $FO4Path "Fallout4.exe"))) { Fail "Fallout4.exe not found in: $FO4Path" }
OK "Fallout 4  : $FO4Path"

if (-not $MorrowindPath) {
    $MorrowindPath = Find-Game $libs "Morrowind"
    if (-not $MorrowindPath) {
        Write-Host ""
        $MorrowindPath = Read-Host "  Morrowind install path (e.g. C:\Games\Morrowind)"
    }
}
if (-not (Test-Path (Join-Path $MorrowindPath "Data Files\Morrowind.esm"))) { Fail "Morrowind.esm not found in: $MorrowindPath\Data Files" }
OK "Morrowind  : $MorrowindPath"

if ($MorrowindPath -match ' ') { Warn "Morrowind path contains spaces - may affect launch. Consider C:\Games\Morrowind" }

# ---------------------------------------------------------------------------
# STEP 2: Compute and validate all paths
# ---------------------------------------------------------------------------
Step 2 "Computing installation paths"

$IpcDir = Validate-IpcDir $IpcDir

$TRIGGER_FILE  = $IpcDir + "\.morrowind_launch"
$SHUTDOWN_FILE = $IpcDir + "\.morrowind_shutdown"
# BRIDGE_FILE is fixed (30-char limit)

$LAUNCHER_EXE  = Join-Path $MorrowindPath "launch_openmw.exe"
$WATCHER_PS1   = Join-Path $MorrowindPath "morrowind_watcher.ps1"
$PATHS_CFG     = Join-Path $MorrowindPath "hollowind_paths.cfg"

OK "IPC dir    : $IpcDir"
OK "Trigger    : $TRIGGER_FILE  ($($TRIGGER_FILE.Length) chars)"
OK "Shutdown   : $SHUTDOWN_FILE  ($($SHUTDOWN_FILE.Length) chars)"
OK "Bridge     : $BRIDGE_FILE  ($($BRIDGE_FILE.Length) chars)"

# ---------------------------------------------------------------------------
# STEP 3: Create IPC and temp directories
# ---------------------------------------------------------------------------
Step 3 "Creating IPC directories"
New-Item -ItemType Directory -Force $IpcDir          | Out-Null
New-Item -ItemType Directory -Force (Split-Path $BRIDGE_FILE) | Out-Null
OK "Created: $IpcDir"
OK "Created: $(Split-Path $BRIDGE_FILE)"

# ---------------------------------------------------------------------------
# STEP 4: Extract bundled OpenMW zip
# ---------------------------------------------------------------------------
Step 4 "Installing OpenMW from bundled package"

if (-not $OpenMWPath) {
    # Find openmw-*.zip sitting next to this script
    $omwZip = Get-ChildItem $SCRIPT_DIR -Filter "openmw-*.zip" -ErrorAction SilentlyContinue |
              Sort-Object Name -Descending |
              Select-Object -First 1

    if ($omwZip) {
        # Derive target folder name from zip name (strip .zip)
        $omwFolderName = [System.IO.Path]::GetFileNameWithoutExtension($omwZip.Name)
        $OpenMWPath    = Join-Path $IpcDir $omwFolderName

        Write-Host "     Extracting $($omwZip.Name) to $IpcDir ..." -ForegroundColor Yellow
        Expand-Archive -Path $omwZip.FullName -DestinationPath $IpcDir -Force
        OK "Extracted to $OpenMWPath"
    } else {
        Fail "No openmw-*.zip found in $SCRIPT_DIR. Place the bundled OpenMW zip next to this script."
    }
}

$OpenMWPath = $OpenMWPath.TrimEnd('\')
$OPENMW_EXE = Join-Path $OpenMWPath "openmw.exe"

if (-not (Test-Path $OPENMW_EXE)) { Fail "openmw.exe not found in: $OpenMWPath" }
OK "OpenMW : $OpenMWPath"

# ---------------------------------------------------------------------------
# STEP 5: Install OpenMW runtime files
# ---------------------------------------------------------------------------
Step 5 "Installing OpenMW runtime files"

# Watcher script — bundled in tes3dir.zip next to this script
$tes3Zip = Join-Path $SCRIPT_DIR "tes3dir.zip"
if (-not (Test-Path $tes3Zip)) { Fail "tes3dir.zip not found in $SCRIPT_DIR" }
$tes3Tmp = Join-Path $env:TEMP "hollowind_tes3dir"
Expand-Archive -Path $tes3Zip -DestinationPath $tes3Tmp -Force
$tes3Extracted = Join-Path $tes3Tmp "tes3dir"
Get-ChildItem $tes3Extracted | Copy-Item -Destination $MorrowindPath -Recurse -Force
OK "tes3dir.zip extracted to $MorrowindPath"

# launch_openmw.exe — pre-built, extracted from build.zip in step 6
# (installed to $MorrowindPath after build.zip is extracted)

# Write hollowind_paths.cfg - used by the watcher at runtime
# Use WriteAllLines with ASCII to avoid UTF-16 BOM from Set-Content
$OMW_USER_DATA = Join-Path $IpcDir "openmw_user"
$cfgLines = @(
    "# Hollo-Wind path configuration",
    "trigger_file     = $TRIGGER_FILE",
    "shutdown_file    = $SHUTDOWN_FILE",
    "bridge_file      = $BRIDGE_FILE",
    "openmw_exe       = $OPENMW_EXE",
    "morrowind_dir    = $OpenMWPath",
    "openmw_user_data = $OMW_USER_DATA"
)
[System.IO.File]::WriteAllLines($PATHS_CFG, $cfgLines, [System.Text.Encoding]::ASCII)
OK "hollowind_paths.cfg written"

# Write openmw.cfg to the location OpenMW actually reads on Windows.
$omwCfgDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\OpenMW"
New-Item -ItemType Directory -Force $omwCfgDir | Out-Null
$dataPath = $MorrowindPath + '\Data Files'
$userCfgLines = @(
    "# Hollo-Wind OpenMW config",
    "data=`"$dataPath`"",
    "fallback-archive=Morrowind.bsa",
    "content=Morrowind.esm"
)
if (Test-Path (Join-Path $dataPath "Tribunal.bsa"))  { $userCfgLines += "fallback-archive=Tribunal.bsa" }
if (Test-Path (Join-Path $dataPath "Bloodmoon.bsa")) { $userCfgLines += "fallback-archive=Bloodmoon.bsa" }
if (Test-Path (Join-Path $dataPath "Tribunal.esm"))  { $userCfgLines += "content=Tribunal.esm";  OK "Tribunal.esm detected - added" }
if (Test-Path (Join-Path $dataPath "Bloodmoon.esm")) { $userCfgLines += "content=Bloodmoon.esm"; OK "Bloodmoon.esm detected - added" }

# Override OpenMW example suite movie fallbacks with actual Morrowind videos
$videoDir = Join-Path $dataPath "Video"
$userCfgLines += "# movies"
if (Test-Path (Join-Path $videoDir "bethesda logo.bik")) { $userCfgLines += "fallback=Movies_Company_Logo,bethesda logo.bik" }
if (Test-Path (Join-Path $videoDir "mw_logo.bik"))       { $userCfgLines += "fallback=Movies_Morrowind_Logo,mw_logo.bik" }
if (Test-Path (Join-Path $videoDir "mw_intro.bik"))      { $userCfgLines += "fallback=Movies_New_Game,mw_intro.bik" }
if (Test-Path (Join-Path $videoDir "mw_load.bik"))       { $userCfgLines += "fallback=Movies_Loading,mw_load.bik" }
if (Test-Path (Join-Path $videoDir "mw_menu.bik"))       { $userCfgLines += "fallback=Movies_Options_Menu,mw_menu.bik" }
[System.IO.File]::WriteAllLines((Join-Path $omwCfgDir "openmw.cfg"), $userCfgLines, [System.Text.Encoding]::ASCII)
OK "openmw.cfg written to $omwCfgDir"

# Extract bundled OpenMW user config (settings.cfg, input_v3.xml, shaders.yaml)
$omwUserdataZip = Join-Path $SCRIPT_DIR "openmw_userdata.zip"
if (Test-Path $omwUserdataZip) {
    Expand-Archive -Path $omwUserdataZip -DestinationPath $omwCfgDir -Force
    OK "OpenMW user config extracted to $omwCfgDir"
} else { Warn "openmw_userdata.zip not found - settings.cfg and keybindings not installed" }

# Deploy OpenMW runtime files (openmw.cfg, SDL2.dll, pipboy settings)
$omwRuntimeZip = Join-Path $SCRIPT_DIR "openmw_runtime.zip"
if (-not (Test-Path $omwRuntimeZip)) { Fail "openmw_runtime.zip not found in $SCRIPT_DIR" }
$omwRuntimeTmp = Join-Path $env:TEMP "hollowind_omwruntime"
Expand-Archive -Path $omwRuntimeZip -DestinationPath $omwRuntimeTmp -Force
$omwRuntimeExtracted = Join-Path $omwRuntimeTmp "openmw_runtime"

Copy-Item (Join-Path $omwRuntimeExtracted "openmw.cfg") $OpenMWPath -Force
OK "openmw.cfg deployed to $OpenMWPath"

Copy-Item (Join-Path $omwRuntimeExtracted "SDL2.dll") $OpenMWPath -Force
OK "SDL2.dll deployed to $OpenMWPath"

Copy-Item (Join-Path $omwRuntimeExtracted "pipboy_config\settings.cfg") $omwCfgDir -Force
OK "Pip-Boy settings.cfg deployed to $omwCfgDir"

# ---------------------------------------------------------------------------
# STEP 6: Extract pre-built DLLs from build.zip
# ---------------------------------------------------------------------------
Step 6 "Extracting pre-built DLLs from build.zip"

$buildZip = Join-Path $SCRIPT_DIR "build.zip"
if (-not (Test-Path $buildZip)) { Fail "build.zip not found in $SCRIPT_DIR" }

$buildTmp = Join-Path $env:TEMP "hollowind_build"
Expand-Archive -Path $buildZip -DestinationPath $buildTmp -Force

# zip extracts into a build\ subfolder
$buildExtracted = Join-Path $buildTmp "build"

$dllTmp = Join-Path $buildExtracted "MorrowindLauncher.dll"
if (-not (Test-Path $dllTmp)) { Fail "MorrowindLauncher.dll not found in build.zip" }
OK "MorrowindLauncher.dll extracted ($((Get-Item $dllTmp).Length) bytes)"

Copy-Item (Join-Path $buildExtracted "launch_openmw.exe") $MorrowindPath -Force
OK "launch_openmw.exe installed to $MorrowindPath"

# ---------------------------------------------------------------------------
# STEP 8: Install Fallout 4 mod files
# ---------------------------------------------------------------------------
Step 8 "Installing Fallout 4 mod files"

$fo4Data = Join-Path $FO4Path "Data"
New-Item -ItemType Directory -Force (Join-Path $fo4Data "F4SE\Plugins") | Out-Null

# Extract pre-built FO4 data zip (ESP, BA2, SWF, meshes, textures)
$fo4Zip = Join-Path $SCRIPT_DIR "fo4_data.zip"
if (-not (Test-Path $fo4Zip)) { Fail "fo4_data.zip not found in $SCRIPT_DIR" }

$fo4Tmp = Join-Path $env:TEMP "hollowind_fo4data"
Expand-Archive -Path $fo4Zip -DestinationPath $fo4Tmp -Force
$fo4Extracted = Join-Path $fo4Tmp "fo4_data"

# Copy everything except Video into Fallout 4\Data\
Get-ChildItem $fo4Extracted -Exclude "Video" | Copy-Item -Destination $fo4Data -Recurse -Force
OK "FO4 mod files extracted to $fo4Data"

# Video goes to Morrowind's Data Files\Video\
$mwVideo = Join-Path $MorrowindPath "Data Files\Video"
New-Item -ItemType Directory -Force $mwVideo | Out-Null
Copy-Item (Join-Path $fo4Extracted "Video\*") $mwVideo -Force
OK "Video files extracted to $mwVideo"


# MSVC-built DLL
Copy-Item $dllTmp (Join-Path $fo4Data "F4SE\Plugins\MorrowindLauncher.dll") -Force
OK "MorrowindLauncher.dll (MSVC build)"

# MorrowindLauncher.ini - all paths computed from install locations
$iniDst = Join-Path $fo4Data "F4SE\Plugins\MorrowindLauncher.ini"
@"
[General]
; Paths set by Hollo-Wind installer on $(Get-Date -Format 'yyyy-MM-dd HH:mm')
; OpenMWPath: executable the F4SE plugin launches when F9 is pressed
OpenMWPath=$LAUNCHER_EXE
; MorrowindDataPath: location of Morrowind.esm and game data
MorrowindDataPath=$MorrowindPath\Data Files
; ShutdownFile: plugin writes this file to signal morrowind_watcher.ps1 to kill OpenMW
ShutdownFile=$SHUTDOWN_FILE
"@ | Set-Content $iniDst
OK "MorrowindLauncher.ini written"

# ---------------------------------------------------------------------------
# STEP 9: Fallout4Custom.ini settings
# ---------------------------------------------------------------------------
Step 9 "Merging Fallout4Custom.ini settings"

$fo4IniDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\Fallout4"
$fo4Ini    = Join-Path $fo4IniDir "Fallout4Custom.ini"
New-Item -ItemType Directory -Force $fo4IniDir | Out-Null
if (-not (Test-Path $fo4Ini)) {
    Copy-Item (Join-Path $buildExtracted "Fallout4Custom.ini") $fo4Ini -Force
    OK "Fallout4Custom.ini created from reference"
}

function Set-IniKey($file, $section, $key, $value) {
    $lines = @(Get-Content $file -ErrorAction SilentlyContinue)
    $secRx = "^\[$([regex]::Escape($section))\]"
    $keyRx = "^$([regex]::Escape($key))\s*="
    $secIdx = -1; $keyIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $secRx) { $secIdx = $i }
        if ($secIdx -ge 0 -and $lines[$i] -match $keyRx) { $keyIdx = $i; break }
    }
    if ($keyIdx -ge 0) { $lines[$keyIdx] = "$key=$value" }
    elseif ($secIdx -ge 0) { $lines = $lines[0..$secIdx] + "$key=$value" + $lines[($secIdx+1)..($lines.Count-1)] }
    else { $lines += "[$section]"; $lines += "$key=$value" }
    Set-Content $file $lines
}

Set-IniKey $fo4Ini "Pipboy"  "uPipboyTargetWidth"        "1024"
Set-IniKey $fo4Ini "Pipboy"  "uPipboyTargetHeight"       "1024"
Set-IniKey $fo4Ini "Archive" "bInvalidateOlderFiles"     "1"
Set-IniKey $fo4Ini "Archive" "sResourceDataDirsFinal"    "STRINGS\, INTERFACE\"
Set-IniKey $fo4Ini "Archive" "sResourceArchiveList2"     "Fallout4 - Animations.ba2, MorrowindLauncher - Main.ba2"
Set-IniKey $fo4Ini "General" "bAlwaysActive"             "1"
OK "Fallout4Custom.ini updated"

# ---------------------------------------------------------------------------
# STEP 10: Enable ESP in Plugins.txt
# ---------------------------------------------------------------------------
Step 10 "Enabling MorrowindLauncher.esp"

$pluginsTxt = Join-Path $env:LOCALAPPDATA "Fallout4\Plugins.txt"
if (-not (Test-Path $pluginsTxt)) { New-Item -ItemType File -Force $pluginsTxt | Out-Null }
$plugins = Get-Content $pluginsTxt -ErrorAction SilentlyContinue
if ($plugins -notcontains "*MorrowindLauncher.esp") {
    Add-Content $pluginsTxt "*MorrowindLauncher.esp"
    OK "Added to Plugins.txt"
} else { OK "Already in Plugins.txt" }

# ---------------------------------------------------------------------------
# Save install manifest for uninstall
# ---------------------------------------------------------------------------
$manifestDir = Join-Path $env:LOCALAPPDATA "HolloWind"
New-Item -ItemType Directory -Force $manifestDir | Out-Null
@(
    $PATHS_CFG,
    (Join-Path $MorrowindPath "morrowind_watcher.ps1"),
    (Join-Path $MorrowindPath "launch_openmw.exe"),
    (Join-Path $fo4Data "F4SE\Plugins\MorrowindLauncher.dll"),
    (Join-Path $fo4Data "F4SE\Plugins\MorrowindLauncher.ini"),
    (Join-Path $fo4Data "MorrowindLauncher.esp"),
    (Join-Path $fo4Data "MorrowindLauncher - Main.ba2"),
    (Join-Path $fo4Data "Interface\Programs\MorrowindDisplay.swf")
) | Set-Content (Join-Path $manifestDir "install_manifest.txt")

# ===========================================================================
# Done
# ===========================================================================

Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  INSTALLATION COMPLETE!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""
Write-Host "HOW TO PLAY:" -ForegroundColor Cyan
Write-Host "  1. Launch Fallout 4 via F4SE (f4se_loader.exe)"
Write-Host "     (the watcher starts automatically with the game)"
Write-Host "  3. Load a save - Morrowind holotape appears in inventory"
Write-Host "  4. Open Pip-Boy, load the holotape"
Write-Host "  5. Press F9 - Morrowind launches on the Pip-Boy screen"
Write-Host "  6. Press F9 again or Tab to stop"
Write-Host ""
Write-Host "IPC directory : $IpcDir"
Write-Host "OpenMW        : $MorrowindPath"
Write-Host ""
