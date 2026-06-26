#Requires -Version 5.1
# NVDA No-speech capture for one editing scenario (docs/TASK-00a Strategy A).
#
# Launches NVDA with the pinned No-speech config against a running host exe,
# lets the host inject its edit + caret events, then greps the NVDA session log
# for "Speaking [...]" lines (the NVDA-observable utterance record) and copies
# the captured log to results\nvda-<label>.log for committing alongside the
# Linux event logs.
#
# >>> RUN-ONLY -- DO NOT AUTO-FIRE <<<
# NVDA is a GUI screen reader and REQUIRES an interactive desktop session
# (it cannot run in session 0 / headless -- docs/TASK-00a "Headless / dev-mode").
# This script must be RUN by a human (or the interactive Claude Code session) on
# the logged-in Windows box; it is authored here, never executed during prep.
# It also assumes one-time setup is done: the pinned config at C:\awe\nvda-config
# (synth = No speech) -- see tests\nvda-config\ and TASK-00a "Project config profile".
param(
  # Path to the host exe that stands up the HWND + UIA provider (e.g. blite_host_win).
  [Parameter(Mandatory = $true)]
  [string]$HostExe,

  # Short scenario label used for the log filenames (e.g. "v1-insert-caret").
  [Parameter(Mandatory = $true)]
  [string]$Label,

  # Seconds to let NVDA attach + the host fire its events before grepping.
  [int]$DwellSeconds = 8
)

$ErrorActionPreference = "Stop"

# --- fixed locations (match TASK-00a + blite_host_win.README) ----------------
$nvda      = Join-Path $env:ProgramFiles "nvda\nvda.exe"
$config    = "C:\awe\nvda-config"                       # pinned No-speech profile
$aweLogs   = "C:\awe\logs"
$nvdaLog   = Join-Path $aweLogs ("nvda-" + $Label + ".log")

$proj      = "C:\Dev\Projects\45 - AccWebEdit"
$results   = Join-Path $proj "results"
$outLog    = Join-Path $results ("nvda-" + $Label + ".log")

New-Item -ItemType Directory -Force -Path $aweLogs | Out-Null
New-Item -ItemType Directory -Force -Path $results | Out-Null

Write-Host ("NVDA capture for scenario: " + $Label)

# --- preflight ---------------------------------------------------------------
if (-not (Test-Path $nvda))    { throw ("NVDA not installed at " + $nvda + " -- run the setup script first") }
if (-not (Test-Path $config))  { throw ("pinned NVDA config missing at " + $config + " -- copy tests\nvda-config there (see its README)") }
if (-not (Test-Path $HostExe)) { throw ("host exe not found: " + $HostExe) }

# --- launch NVDA: No speech, input/output log level (12) ---------------------
# -m  --minimal       : no sounds, no interface, no start message
# -c  --config-path   : project-pinned No-speech profile
# --log-level=12      : input/output -- logs spoken utterances + input gestures
# --log-file          : per-scenario log path
$nvdaArgs = @(
  "-m",
  "-c", $config,
  "--log-level=12",
  "--log-file", $nvdaLog
)
Write-Host ("Launching NVDA: " + $nvda + " " + ($nvdaArgs -join " "))
$nvdaProc = Start-Process -FilePath $nvda -ArgumentList $nvdaArgs -PassThru

# Give NVDA time to fully initialize its UIA client before the host appears.
# VALIDATED 2026-06-25: NVDA 2026.1.1 "slow starting core" ~21-27s on this box
# (RDP) before it is NVDA-initialized and logging "Speaking [...]" lines. Waiting
# only a few seconds races the startup and captures nothing. 30s is a safe floor.
Start-Sleep -Seconds 30

# --- launch the host (it injects the edit + caret move, fires UIA events) ----
Write-Host ("Launching host: " + $HostExe)
$hostProc = Start-Process -FilePath $HostExe -PassThru

# Bring the host window forward so NVDA sees focus on it. On a real run you may
# instead drive caret moves with SendKeys / keybd_event here (TASK-00a "Scenario
# driving"); for the V1 slice the host has already injected its single insert +
# caret move, so a dwell is enough to let the UIA events surface.
Start-Sleep -Seconds $DwellSeconds

# --- extract the utterance sequence ------------------------------------------
if (Test-Path $nvdaLog) {
  Write-Host ("--- Speaking lines from " + $nvdaLog + " ---")
  $spoken = Select-String -Path $nvdaLog -Pattern "Speaking"
  if ($spoken) {
    $spoken | ForEach-Object { Write-Host $_.Line }
  } else {
    Write-Host "(no 'Speaking' lines found -- check NVDA version's log format, TASK-00a open item #1)"
  }
  # Commit the full session log under results\ alongside the Linux event logs.
  Copy-Item -Path $nvdaLog -Destination $outLog -Force
  Write-Host ("Copied NVDA log to " + $outLog)
} else {
  Write-Host ("WARNING: NVDA log not found at " + $nvdaLog + " -- did NVDA start on the interactive desktop?")
}

# --- teardown ----------------------------------------------------------------
# Stop the host, then NVDA. (Left non-fatal so a stuck handle does not mask the
# capture result.) NVDA can also be quit with: nvda.exe -q
try { if ($hostProc -and -not $hostProc.HasExited) { Stop-Process -Id $hostProc.Id -Force } } catch {}
try { & $nvda -q } catch {}
try { if ($nvdaProc -and -not $nvdaProc.HasExited) { Stop-Process -Id $nvdaProc.Id -Force } } catch {}

Write-Host ("NVDA capture for '" + $Label + "' complete.")
