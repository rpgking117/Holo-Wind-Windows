#Requires -Version 5.0
<#
.SYNOPSIS
    Morrowind: PipBoy Edition -- Windows Installer

.DESCRIPTION
    Single-source-of-truth installer.  Once it knows where Fallout 4,
    Morrowind, and your existing OpenMW 0.50.0 install are, it computes ALL
    paths, then:
      - Patches MorrowindLauncher.dll with those exact paths
      - Writes MorrowindLauncher.ini with those exact paths
      - Writes hollowind_paths.cfg so the watcher uses the same paths
      - Deploys the SDL2 capture proxy into your OpenMW install folder
      - Deploys all Fallout 4 mod files and Papyrus scripts
      - Merges required Fallout4Custom.ini settings
      - Enables the ESP in Plugins.txt

.PARAMETER FO4Path       Override Fallout 4 install path
.PARAMETER MorrowindPath Override Morrowind install path
.PARAMETER OpenMWPath    Override OpenMW 0.50.0 install path (folder containing openmw.exe)
.PARAMETER IpcDir        Override IPC directory (default C:\HolloWind)
                         Max path lengths: trigger <= 37, shutdown <= 39, bridge <= 30
.PARAMETER SkipBuild     Skip DLL compilation (use pre-built SDL2.dll)
.PARAMETER Uninstall     Remove all installed components
#>
param(
    [string]$FO4Path       = "",
    [string]$MorrowindPath = "",
    [string]$OpenMWPath    = "",
    [string]$InstallerDir  = "",
    [switch]$SkipBuild,
    [switch]$Uninstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SCRIPT_DIR = if ($InstallerDir) { $InstallerDir } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$BUILD_DIR  = Join-Path $SCRIPT_DIR "build"

$BRIDGE_FILE = "C:\tmp\morrowind_pipboy_bridge"

# ---- Helpers ---------------------------------------------------------------

$TOTAL_STEPS = 8

function Step($n, $msg) {
    $pad = " " * (2 - "$n".Length)
    Write-Host ""
    Write-Host "  +--[ $pad$n / $TOTAL_STEPS ]  $msg" -ForegroundColor White
    Write-Host ("  |  " + ("-" * 50)) -ForegroundColor DarkGray
}
function OK($msg)   { Write-Host "  |  [+] $msg" -ForegroundColor Gray }
function Warn($msg) { Write-Host "  |  [!] $msg" -ForegroundColor Gray }
function Fail($msg) { Write-Host "  |  [X] FAILED  $msg" -ForegroundColor White; throw $msg }
function Add-Manifest($path) { $script:manifest += $path }

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


# ===========================================================================
# MAIN
# ===========================================================================

Write-Host ""
Write-Host "==========================================" -ForegroundColor White
Write-Host "  MORROWIND: PIPBOY EDITION               " -ForegroundColor White
Write-Host "  Windows Installer                       " -ForegroundColor Gray
Write-Host "==========================================" -ForegroundColor White

if ($Uninstall) {
    Write-Host "`nUninstalling..." -ForegroundColor Yellow
    $manifestFile = Join-Path $env:LOCALAPPDATA "MorrowindPipBoyEdition\install_manifest.txt"
    if (Test-Path $manifestFile) {
        Get-Content $manifestFile | ForEach-Object { Remove-Item $_ -Force -EA SilentlyContinue }
        OK "Files removed per manifest"
    } else { Warn "No manifest found - remove files manually" }
    try { schtasks /Delete /TN "MorrowindPipBoyEdition-StartWatcher" /F 2>&1 | Out-Null } catch {}
    Remove-Item (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) "Morrowind PipBoy Edition Watcher.lnk") -EA SilentlyContinue
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
        Write-Host "  |  Fallout 4 not found via Steam - opening folder browser..." -ForegroundColor Gray
        Add-Type -AssemblyName System.Windows.Forms
        $browser = New-Object System.Windows.Forms.FolderBrowserDialog
        $browser.Description = "Select your Fallout 4 install folder (the folder containing Fallout4.exe)"
        $browser.ShowNewFolderButton = $false
        if ($browser.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            $FO4Path = $browser.SelectedPath
        } else { Fail "No Fallout 4 path selected. Re-run with -FO4Path `"C:\path\to\Fallout4`"." }
    }
}
if (-not (Test-Path (Join-Path $FO4Path "Fallout4.exe"))) { Fail "Fallout4.exe not found in: $FO4Path" }
OK "Fallout 4  : $FO4Path"

if (-not $MorrowindPath) {
    $MorrowindPath = Find-Game $libs "Morrowind"
    if (-not $MorrowindPath) {
        Write-Host "  |  Morrowind not found via Steam - opening folder browser..." -ForegroundColor Gray
        Add-Type -AssemblyName System.Windows.Forms
        $browser = New-Object System.Windows.Forms.FolderBrowserDialog
        $browser.Description = "Select your Morrowind install folder (the folder containing Morrowind.exe)"
        $browser.ShowNewFolderButton = $false
        if ($browser.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            $MorrowindPath = $browser.SelectedPath
        } else { Fail "No Morrowind path selected. Re-run with -MorrowindPath `"C:\path\to\Morrowind`"." }
    }
}
if (-not (Test-Path (Join-Path $MorrowindPath "Data Files\Morrowind.esm"))) { Fail "Morrowind.esm not found in: $MorrowindPath\Data Files" }
OK "Morrowind  : $MorrowindPath"


if (-not $OpenMWPath) {
    # Try registry uninstall entries
    foreach ($hive in @("HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
                        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall")) {
        if (Test-Path $hive) {
            Get-ChildItem $hive -ErrorAction SilentlyContinue | ForEach-Object {
                $props = Get-ItemProperty $_.PsPath -ErrorAction SilentlyContinue
                if (-not $props) { return }
                $dn  = $props | Select-Object -ExpandProperty DisplayName    -ErrorAction SilentlyContinue
                $loc = $props | Select-Object -ExpandProperty InstallLocation -ErrorAction SilentlyContinue
                if ($dn -like "*OpenMW*" -and $loc) {
                    $loc = $loc.TrimEnd('\')
                    if (Test-Path (Join-Path $loc "openmw.exe")) { $OpenMWPath = $loc }
                }
            }
        }
        if ($OpenMWPath) { break }
    }

    # Try common install locations across all drive letters
    if (-not $OpenMWPath) {
        $drives = (Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root)
        $omwFolders = @("OpenMW 0.50.0","OpenMW 0.50","OpenMW","Program Files\OpenMW 0.50.0","Program Files\OpenMW 0.50","Program Files\OpenMW","Program Files (x86)\OpenMW")
        :omwSearch foreach ($d in $drives) {
            foreach ($f in $omwFolders) {
                $c = Join-Path $d $f
                if (Test-Path (Join-Path $c "openmw.exe")) { $OpenMWPath = $c; break omwSearch }
            }
        }
    }

    if (-not $OpenMWPath) {
        Write-Host "  |  OpenMW 0.50.0 not found - opening folder browser..." -ForegroundColor Gray
        Add-Type -AssemblyName System.Windows.Forms
        $browser = New-Object System.Windows.Forms.FolderBrowserDialog
        $browser.Description = "Select your OpenMW 0.50.0 install folder (the folder containing openmw.exe)"
        $browser.ShowNewFolderButton = $false
        if ($browser.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            $OpenMWPath = $browser.SelectedPath
        } else { Fail "No OpenMW path selected. Re-run with -OpenMWPath `"C:\path\to\openmw`"." }
    }
}

$OpenMWPath = $OpenMWPath.TrimEnd('\')
$OPENMW_EXE = Join-Path $OpenMWPath "openmw.exe"

if (-not (Test-Path $OPENMW_EXE)) { Fail "openmw.exe not found in: $OpenMWPath" }
OK "OpenMW    : $OpenMWPath"

# ---------------------------------------------------------------------------
# STEP 2: Prepare path variables and IPC directories
# ---------------------------------------------------------------------------
Step 2 "Preparing directories"

$LAUNCHER_EXE  = Join-Path $MorrowindPath "launch_openmw.exe"
$TRIGGER_FILE  = "C:\HolloWind\.morrowind_launch"
$SHUTDOWN_FILE = "C:\HolloWind\.morrowind_shutdown"
$PATHS_CFG     = Join-Path $MorrowindPath "hollowind_paths.cfg"
$OMW_USER_DATA = Join-Path $OpenMWPath "openmw_user"

New-Item -ItemType Directory -Force "C:\HolloWind"            | Out-Null
New-Item -ItemType Directory -Force (Split-Path $BRIDGE_FILE) | Out-Null
OK "IPC dir   : C:\HolloWind"
OK "Bridge dir: $(Split-Path $BRIDGE_FILE)"

# ---------------------------------------------------------------------------
# Manifest initialisation (tracks every file the installer writes)
# ---------------------------------------------------------------------------
$manifestDir  = Join-Path $env:LOCALAPPDATA "MorrowindPipBoyEdition"
New-Item -ItemType Directory -Force $manifestDir | Out-Null
$script:manifest = @()

# ---------------------------------------------------------------------------
# STEP 3: Deploy SDL2 capture proxy
# ---------------------------------------------------------------------------
Step 3 "Deploying SDL2 capture proxy"

$patchDir = Join-Path $BUILD_DIR "OpenMW Patch"
if (-not (Test-Path $patchDir)) { Fail "'OpenMW Patch' folder not found in: $BUILD_DIR" }

$sdlProxy = Join-Path $patchDir "SDL2.dll"
$sdlOrig  = Join-Path $patchDir "SDL2_orig.dll"

if (-not (Test-Path $sdlProxy)) { Fail "SDL2.dll not found in: $patchDir" }
if (-not (Test-Path $sdlOrig))  { Fail "SDL2_orig.dll not found in: $patchDir" }

Copy-Item $sdlProxy (Join-Path $OpenMWPath "SDL2.dll")      -Force
Add-Manifest (Join-Path $OpenMWPath "SDL2.dll")
OK "SDL2.dll (capture proxy) -> $OpenMWPath"
Copy-Item $sdlOrig  (Join-Path $OpenMWPath "SDL2_orig.dll") -Force
Add-Manifest (Join-Path $OpenMWPath "SDL2_orig.dll")
OK "SDL2_orig.dll           -> $OpenMWPath"

# ---------------------------------------------------------------------------
# STEP 4: Install OpenMW runtime files
# ---------------------------------------------------------------------------
Step 4 "Installing OpenMW runtime files"

# Watcher ??? goes to OpenMW install dir so it finds hollowind_paths.cfg via $PSScriptRoot
$morrowindSrc = Join-Path $BUILD_DIR "Morrowind"
if (-not (Test-Path $morrowindSrc)) { Fail "Morrowind folder not found in $BUILD_DIR" }
Copy-Item (Join-Path $morrowindSrc "morrowind_watcher.ps1") $MorrowindPath -Force
Add-Manifest (Join-Path $MorrowindPath "morrowind_watcher.ps1")
OK "morrowind_watcher.ps1 installed to $MorrowindPath"

# Data Files\Video ??? goes to Morrowind install dir
$videoSrc = Join-Path $morrowindSrc "Data Files\Video"
if (Test-Path $videoSrc) {
    $videoDst = Join-Path $MorrowindPath "Data Files\Video"
    New-Item -ItemType Directory -Force $videoDst | Out-Null
    Get-ChildItem $videoSrc -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $videoDst $_.Name) -Force
        Add-Manifest (Join-Path $videoDst $_.Name)
    }
    OK "Video files copied to $videoDst"
}

# launch_openmw.exe ??? pre-built, copied from build\ in step 6

# Write hollowind_paths.cfg - used by the watcher at runtime
# Use WriteAllLines with ASCII to avoid UTF-16 BOM from Set-Content
$cfgLines = @(
    "# Morrowind: PipBoy Edition path configuration",
    "trigger_file     = $TRIGGER_FILE",
    "shutdown_file    = $SHUTDOWN_FILE",
    "bridge_file      = $BRIDGE_FILE",
    "openmw_exe       = $OPENMW_EXE",
    "morrowind_dir    = $OpenMWPath",
    "openmw_user_data = $OMW_USER_DATA"
)
[System.IO.File]::WriteAllLines($PATHS_CFG, $cfgLines, [System.Text.Encoding]::ASCII)
Add-Manifest $PATHS_CFG
OK "hollowind_paths.cfg written to $MorrowindPath"
[System.IO.File]::WriteAllLines((Join-Path $OpenMWPath "hollowind_paths.cfg"), $cfgLines, [System.Text.Encoding]::ASCII)
Add-Manifest (Join-Path $OpenMWPath "hollowind_paths.cfg")
OK "hollowind_paths.cfg written to $OpenMWPath"

# Write openmw.cfg to the location OpenMW actually reads on Windows.
$omwCfgDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\OpenMW"
New-Item -ItemType Directory -Force $omwCfgDir | Out-Null
$dataPath = $MorrowindPath + '\Data Files'
$userCfgLines = @(
    "# Morrowind: PipBoy Edition OpenMW config",
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
Add-Manifest (Join-Path $omwCfgDir "openmw.cfg")
OK "openmw.cfg written to $omwCfgDir"

# Copy bundled OpenMW user config (settings.cfg, input_v3.xml, shaders.yaml, combat patch, etc.)
$omwUserdataDir = Join-Path $BUILD_DIR "openmw_userdata"
if (Test-Path $omwUserdataDir) {
    Get-ChildItem $omwUserdataDir -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($omwUserdataDir.Length).TrimStart('\')
        $dst = Join-Path $omwCfgDir $rel
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst -Force
        Add-Manifest $dst
    }
    OK "OpenMW user config copied to $omwCfgDir"
} else { Warn "openmw_userdata folder not found in $BUILD_DIR - settings.cfg and keybindings not installed" }

# Deploy OpenMW runtime files (openmw.cfg, pipboy settings)
$omwRuntimeDir = Join-Path $BUILD_DIR "openmw_runtime\openmw_runtime"
if (-not (Test-Path $omwRuntimeDir)) { Fail "openmw_runtime folder not found in $BUILD_DIR" }

Copy-Item (Join-Path $omwRuntimeDir "openmw.cfg") $OpenMWPath -Force
Add-Manifest (Join-Path $OpenMWPath "openmw.cfg")
OK "openmw.cfg deployed to $OpenMWPath"

# SDL2.dll (capture proxy) was already deployed into $OpenMWPath in Step 3.

Copy-Item (Join-Path $omwRuntimeDir "pipboy_config\settings.cfg") $omwCfgDir -Force
Add-Manifest (Join-Path $omwCfgDir "settings.cfg")
OK "Pip-Boy settings.cfg deployed to $omwCfgDir"

# ---------------------------------------------------------------------------
# STEP 5: Install pre-built DLLs from build folder
# ---------------------------------------------------------------------------
Step 5 "Installing pre-built DLLs"

$dllSrc = Join-Path $BUILD_DIR "Fallout 4\Data\F4SE\Plugins\MorrowindLauncher.dll"
if (-not (Test-Path $dllSrc)) { Fail "MorrowindLauncher.dll not found in $BUILD_DIR\Fallout 4\Data\F4SE\Plugins" }
OK "MorrowindLauncher.dll found ($((Get-Item $dllSrc).Length) bytes)"

Copy-Item (Join-Path $patchDir "launch_openmw.exe") $MorrowindPath -Force
Add-Manifest (Join-Path $MorrowindPath "launch_openmw.exe")
OK "launch_openmw.exe installed to $MorrowindPath"

# ---------------------------------------------------------------------------
# STEP 6: Install Fallout 4 mod files
# ---------------------------------------------------------------------------
Step 6 "Installing Fallout 4 mod files"

$fo4Data = Join-Path $FO4Path "Data"
New-Item -ItemType Directory -Force (Join-Path $fo4Data "F4SE\Plugins") | Out-Null

# Copy pre-built FO4 data (ESP, BA2, SWF, meshes, textures)
$fo4Extracted = Join-Path $BUILD_DIR "Fallout 4\Data"
if (-not (Test-Path $fo4Extracted)) { Fail "Fallout 4\Data folder not found in $BUILD_DIR" }

Get-ChildItem $fo4Extracted -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($fo4Extracted.Length).TrimStart('\')
    $dst = Join-Path $fo4Data $rel
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item $_.FullName $dst -Force
    Add-Manifest $dst
}
OK "FO4 mod files copied to $fo4Data"


# MorrowindLauncher.ini - all paths computed from install locations
$iniDst = Join-Path $fo4Data "F4SE\Plugins\MorrowindLauncher.ini"
@"
[General]
; Paths set by Morrowind: PipBoy Edition installer on $(Get-Date -Format 'yyyy-MM-dd HH:mm')
; OpenMWPath: executable the F4SE plugin launches when F9 is pressed
OpenMWPath=$LAUNCHER_EXE
; MorrowindDataPath: location of Morrowind.esm and game data
MorrowindDataPath=$MorrowindPath\Data Files
; ShutdownFile: plugin writes this file to signal morrowind_watcher.ps1 to kill OpenMW
ShutdownFile=$SHUTDOWN_FILE
"@ | Set-Content $iniDst
Add-Manifest $iniDst
OK "MorrowindLauncher.ini written"

# ---------------------------------------------------------------------------
# STEP 7: Fallout4Custom.ini settings
# ---------------------------------------------------------------------------
Step 7 "Merging Fallout4Custom.ini settings"

$fo4IniDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\Fallout4"
$fo4Ini    = Join-Path $fo4IniDir "Fallout4Custom.ini"
New-Item -ItemType Directory -Force $fo4IniDir | Out-Null
if (-not (Test-Path $fo4Ini)) {
    Copy-Item (Join-Path $SCRIPT_DIR "Fallout4Custom.ini") $fo4Ini -Force
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
# STEP 8: Enable ESP in Plugins.txt
# ---------------------------------------------------------------------------
Step 8 "Enabling MorrowindLauncher.esp"

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
$script:manifest | Set-Content (Join-Path $manifestDir "install_manifest.txt")
OK "Install manifest saved: $($script:manifest.Count) files tracked"

# Store OpenMW path so uninstaller can restore SDL2
Set-Content (Join-Path $manifestDir "openmw_path.txt") $OpenMWPath

# ===========================================================================
# Done
# ===========================================================================

Write-Host ""
Write-Host "==========================================" -ForegroundColor White
Write-Host "  MORROWIND: PIPBOY EDITION               " -ForegroundColor White
Write-Host "  Installation complete.                  " -ForegroundColor Gray
Write-Host "==========================================" -ForegroundColor White
Write-Host ""
Write-Host "HOW TO PLAY:" -ForegroundColor White
Write-Host "  1. Launch Fallout 4 using F4SE"
Write-Host "  2. Find the Holotape in game"
Write-Host "  3. Enjoy"
Write-Host ""
Write-Host "OpenMW        : $OpenMWPath"
Write-Host "Morrowind     : $MorrowindPath"
Write-Host ""