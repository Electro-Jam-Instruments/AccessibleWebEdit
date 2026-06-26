#Requires -Version 5.1
# Non-elevated: fetch Chromium at the pinned tag and sync. Runs for hours.
# Logs to scripts\chromium-fetch.log and writes scripts\chromium-fetch.done at the end.
#
# Native commands (gclient/fetch/git) are run via cmd-level redirection so their
# stderr (gclient prints informational warnings there) is written to the log and
# NEVER surfaces to PowerShell as a terminating error. Success is judged by the
# process exit code only.
param(
  [string]$SrcRoot = "C:\src",
  [string]$Tag     = "149.0.7827.115"
)
$root = "C:\Dev\Projects\45 - AccWebEdit\scripts"
$log  = Join-Path $root "chromium-fetch.log"
$done = Join-Path $root "chromium-fetch.done"
Remove-Item $done -ErrorAction SilentlyContinue
# Fresh log each run so a detached/scheduled run is easy to verify.
Set-Content -Path $log -Value ("=== fetch run (detached) ===")

function Log([string]$m) {
  $stamp = Get-Date -Format "HH:mm:ss"
  Add-Content -Path $log -Value ("[" + $stamp + "] " + $m)
}

# Run a native command line through cmd, append all output to the log, return exit code.
function Run([string]$cmdline) {
  Log ("RUN: " + $cmdline)
  $logQ = '"' + $log + '"'
  cmd /c "$cmdline >> $logQ 2>&1"
  $ec = $LASTEXITCODE
  Log ("exit code: " + $ec)
  return $ec
}

try {
  $dt = Join-Path $SrcRoot "depot_tools"
  $env:Path = "$dt;" + $env:Path
  $env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
  $env:DEPOT_TOOLS_UPDATE = "1"

  Log "=== chromium fetch starting ==="
  Log ("depot_tools on PATH: " + $dt)

  Log "Bootstrapping depot_tools via gclient --version ..."
  Run "gclient --version" | Out-Null   # bootstrap; warnings on stderr are fine

  $chromium = Join-Path $SrcRoot "chromium"
  New-Item -ItemType Directory -Force -Path $chromium | Out-Null

  if (-not (Test-Path (Join-Path $chromium "src\.git"))) {
    Log "Fetching chromium with --no-history. Large and slow, ~100 GB, hours ..."
    Set-Location $chromium
    $ec = Run "fetch --no-history chromium"
    if ($ec -ne 0) { throw ("fetch failed: " + $ec) }
  } else {
    Log "chromium src .git already present, skipping fetch."
  }

  Set-Location (Join-Path $chromium "src")
  # Targeted depth-1 fetch of ONLY the pinned tag. Do NOT use `git fetch --tags`:
  # on a --no-history (shallow) clone it un-shallows nearly all history (observed a
  # 36 GB runaway pack). A single-tag depth-1 fetch pulls just that snapshot.
  Log ("Targeted depth-1 fetch of tag " + $Tag)
  $ec = Run ("git fetch --depth 1 origin tag " + $Tag)
  if ($ec -ne 0) { throw ("tag fetch failed: " + $ec) }
  $ec = Run ("git checkout " + $Tag)
  if ($ec -ne 0) { throw ("checkout failed: " + $ec) }

  # gclient sync can hit transient HTTP 429 (rate limit) on googlesource deps.
  # Retry with increasing backoff so the detached task self-heals.
  Log "Running gclient sync -D, runtime hooks, long ..."
  $attempt = 0; $ec = 1
  while ($attempt -lt 5 -and $ec -ne 0) {
    $attempt++
    if ($attempt -gt 1) {
      $back = 90 * ($attempt - 1)
      Log ("gclient sync attempt " + $attempt + " (backoff " + $back + "s after a failure, likely 429) ...")
      Start-Sleep -Seconds $back
    }
    $ec = Run "gclient sync -D"
  }
  if ($ec -ne 0) { throw ("gclient sync failed after " + $attempt + " attempts: " + $ec) }

  Log "=== DONE OK ==="
  Set-Content -Path $done -Value "OK 0"
} catch {
  $msg = $_.Exception.Message
  Log ("=== FAILED: " + $msg + " ===")
  Set-Content -Path $done -Value ("FAIL " + $msg)
}
