#Requires -Version 5.0
<#
.SYNOPSIS
    Morrowind: PipBoy Edition -- Watcher
    Manages OpenMW lifecycle for the Pip-Boy mod.
    Paths are read from hollowind_paths.cfg written by the installer.
#>

param(
    [string]$ModDir = $PSScriptRoot,
    [int]$FO4PID = 0
)

# ---- Load installer-written path config ------------------------------------
$cfgFile = Join-Path $ModDir "hollowind_paths.cfg"
if (-not (Test-Path $cfgFile)) {
    Write-Host "ERROR: hollowind_paths.cfg not found in $ModDir" -ForegroundColor Red
    Write-Host "Run the installer first: install_hollowind.ps1" -ForegroundColor Yellow
    exit 1
}

$cfg = @{}
Get-Content $cfgFile | ForEach-Object {
    if ($_ -match '^\s*([^#=]+?)\s*=\s*(.+)$') {
        $cfg[$Matches[1].Trim()] = $Matches[2].Trim()
    }
}

$TRIGGER       = $cfg["trigger_file"]
$SHUTDOWN      = $cfg["shutdown_file"]
$BRIDGE        = $cfg["bridge_file"]
$OPENMW_EXE    = $cfg["openmw_exe"]
$MORROWIND_DIR = $cfg["morrowind_dir"]
$LOG           = Join-Path $ModDir "pipboy_config\watcher.log"

foreach ($key in @("trigger_file","shutdown_file","bridge_file","openmw_exe","morrowind_dir")) {
    if (-not $cfg.ContainsKey($key)) {
        Write-Host "ERROR: hollowind_paths.cfg missing key: $key" -ForegroundColor Red
        exit 1
    }
}

# ---- Helpers ---------------------------------------------------------------

function Write-Log($msg) {
    $ts   = Get-Date -Format "HH:mm:ss"
    $line = "[$ts] $msg"
    Write-Host $line
    Add-Content -Path $LOG -Value $line -ErrorAction SilentlyContinue
}

# ---- Startup checks --------------------------------------------------------

New-Item -ItemType Directory -Force -Path (Split-Path $TRIGGER) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $BRIDGE)  | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $LOG)     | Out-Null

if (-not (Test-Path $OPENMW_EXE)) {
    Write-Host "ERROR: openmw.exe not found at: $OPENMW_EXE" -ForegroundColor Red
    exit 1
}

Write-Log "=== Morrowind: PipBoy Edition Watcher started ==="
Write-Log "OpenMW  : $OPENMW_EXE"
Write-Log "Trigger : $TRIGGER"
Write-Log "Bridge  : $BRIDGE"

# ---- Single-instance guard -------------------------------------------------
# If another watcher is already running, terminate it before continuing.
# This prevents multiple watcher instances from piling up across FO4 sessions.
$mutexName    = "Global\MorrowindPipBoyEditionWatcher"
$mutexCreated = $false
$mutex        = $null
try {
    $mutex = [System.Threading.Mutex]::new($true, $mutexName, [ref]$mutexCreated)
    if (-not $mutexCreated) {
        Write-Log "Another watcher instance detected - taking over"
        # Kill other powershell processes running this script
        $thisScript = $MyInvocation.MyCommand.Path
        Get-WmiObject Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" |
            Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -like "*morrowind_watcher*" } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Milliseconds 300
        # Re-acquire mutex now that the old instance is dead
        try { $mutex.Dispose() } catch {}
        $mutex = [System.Threading.Mutex]::new($true, $mutexName, [ref]$mutexCreated)
    }
} catch {
    Write-Log "WARNING: Mutex error - $($_.Exception.Message)"
}

# ---- Clear stale signal files from any previous session --------------------
Remove-Item -Path $TRIGGER  -Force -ErrorAction SilentlyContinue
Remove-Item -Path $SHUTDOWN -Force -ErrorAction SilentlyContinue
Write-Log "Cleared stale signal files"

# ---- OpenMW process management ---------------------------------------------

$omwProcess = $null

function Start-OpenMW {
    Write-Log "Trigger detected - launching OpenMW..."
    Remove-Item -Path $BRIDGE   -ErrorAction SilentlyContinue
    Remove-Item -Path $SHUTDOWN -ErrorAction SilentlyContinue

    $settingsCfg = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\OpenMW\settings.cfg"
    Set-ItemProperty $settingsCfg -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
    $pipboySettings = @(
        "# OpenMW settings for Pip-Boy streaming (Windows)",
        "[Video]",
        "resolution x = 876",
        "resolution y = 700",
        "fullscreen = false",
        "window mode = 0",
        "minimize on focus loss = false",
        "",
        "[Input]",
        "grab cursor = false",
        "",
        "[HUD]",
        "crosshair = false"
    )
    [System.IO.File]::WriteAllLines($settingsCfg, $pipboySettings, [System.Text.Encoding]::ASCII)
    Set-ItemProperty $settingsCfg -Name IsReadOnly -Value $true

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName         = $OPENMW_EXE
    $psi.WorkingDirectory = $MORROWIND_DIR
    $psi.UseShellExecute  = $false
    $psi.WindowStyle      = [System.Diagnostics.ProcessWindowStyle]::Normal

    $p = [System.Diagnostics.Process]::Start($psi)
    Write-Log "OpenMW PID: $($p.Id)"
    return $p
}

function Stop-OpenMW($proc) {
    if ($proc -and -not $proc.HasExited) {
        Write-Log "Stopping OpenMW (PID $($proc.Id))..."
        $proc.Kill()
        $proc.WaitForExit(5000) | Out-Null
    }
}

# ---- FO4 process watch -----------------------------------------------------
# Self-exit when FO4 dies so we don't linger as an orphan.
$fo4Process = $null
if ($FO4PID -gt 0) {
    try { $fo4Process = Get-Process -Id $FO4PID -ErrorAction Stop }
    catch { Write-Log "WARNING: Could not get FO4 process (PID $FO4PID) - won't auto-exit on FO4 close" }
}

Write-Log "Watching for trigger at: $TRIGGER"
if ($fo4Process) { Write-Log "FO4 PID: $FO4PID (will self-exit when FO4 closes)" }

# ---- Main loop -------------------------------------------------------------

try {
    while ($true) {
        if ($fo4Process -and $fo4Process.HasExited) {
            Write-Log "FO4 process exited - shutting down"
            break
        }

        if (Test-Path $TRIGGER) {
            Remove-Item -Path $TRIGGER -Force -ErrorAction SilentlyContinue

            if ($omwProcess -and -not $omwProcess.HasExited) {
                Stop-OpenMW $omwProcess
            }
            $omwProcess = Start-OpenMW

            while (-not $omwProcess.HasExited) {
                if ($fo4Process -and $fo4Process.HasExited) {
                    Write-Log "FO4 exited during session - killing OpenMW"
                    Stop-OpenMW $omwProcess
                    break
                }
                if (Test-Path $SHUTDOWN) {
                    Remove-Item -Path $SHUTDOWN -Force -ErrorAction SilentlyContinue
                    Write-Log "Shutdown signal received - stopping OpenMW"
                    Stop-OpenMW $omwProcess
                    break
                }
                Start-Sleep -Milliseconds 100
            }

            Write-Log "OpenMW session ended (exit: $($omwProcess.ExitCode))"
            $omwProcess = $null
            $settingsCfg = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\OpenMW\settings.cfg"
            Set-ItemProperty $settingsCfg -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
            Remove-Item -Path $TRIGGER  -ErrorAction SilentlyContinue
            Remove-Item -Path $SHUTDOWN -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 500
    }
} finally {
    Stop-OpenMW $omwProcess
    $settingsCfg = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\OpenMW\settings.cfg"
    Set-ItemProperty $settingsCfg -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
    Remove-Item -Path $SHUTDOWN -Force -ErrorAction SilentlyContinue
    if ($mutex) { try { $mutex.ReleaseMutex(); $mutex.Dispose() } catch {} }
    Write-Log "Watcher stopped."
}
