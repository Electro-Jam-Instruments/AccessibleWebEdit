#Requires -Version 5.1
# Phase 1 -- canonical baseline build + run of the preserved T2 / matrix tests
# inside a real Chromium 149.0.7827.115 checkout, on Windows 11.
#
# Mirrors the other scripts/_*.ps1 helpers:
#   - all output appended to results\phase1-build.log
#   - a scripts\phase1-build.done marker holds OK / FAIL at the end
#   - native commands (gn/ninja/the test exe) run through cmd-level redirection
#     (cmd /c "<cmd> >> <logQ> 2>&1") so their stderr is logged and NEVER trips
#     PowerShell as a terminating error -- judged by exit code only (see
#     scripts\_fetch-chromium.ps1 for the same Run() pattern).
#
# Idempotent: a re-run re-copies the test sources (harmless), re-applies the
# BUILD patch only if not already applied, re-runs gn gen / ninja (incremental),
# and re-runs the tests.
#
# AUTHOR-ONLY NOTE: this is a build driver. It is meant to be RUN on a Windows
# box once the Chromium checkout is complete; it is not auto-fired here.
param(
  [string]$SrcRoot = "C:\src"
)

$ErrorActionPreference = "Stop"

# --- repo-relative locations -------------------------------------------------
$proj    = "C:\Dev\Projects\45 - AccWebEdit"
$scripts = Join-Path $proj "scripts"
$results = Join-Path $proj "results"
$t2dir   = Join-Path $proj "tests\linux-t2"

$log  = Join-Path $results "phase1-build.log"
$done = Join-Path $scripts "phase1-build.done"

# --- checkout locations ------------------------------------------------------
$src      = Join-Path $SrcRoot "chromium\src"
$depot    = Join-Path $SrcRoot "depot_tools"
$axDir    = Join-Path $src "ui\accessibility"
$buildGn  = Join-Path $axDir "BUILD.gn"
$outDir   = "out\rel"                                  # relative to $src for gn/ninja
$testExe  = Join-Path $src "out\rel\ax_t2_unittests.exe"
$fetchDone = Join-Path $scripts "chromium-fetch.done"

New-Item -ItemType Directory -Force -Path $results | Out-Null
Remove-Item $done -ErrorAction SilentlyContinue
# Fresh log each run so a detached run is easy to verify.
Set-Content -Path $log -Value ("=== phase1 build run ===")

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
  # --- depot_tools on PATH, Windows toolchain off (use local VS) ------------
  $env:Path = "$depot;" + $env:Path
  $env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
  # Point Chromium's vs_toolchain.py at our local VS Build Tools (verified: finds
  # VS 2022 + Win11 SDK 10.0.26100). Without this, gn gen fails "No supported VS".
  $env:vs2022_install = "C:\BuildTools"
  $env:GYP_MSVS_VERSION = "2022"

  Log "=== Phase 1 canonical baseline build starting ==="
  Log ("SrcRoot      = " + $SrcRoot)
  Log ("chromium src = " + $src)
  Log ("depot_tools  = " + $depot)

  # (a) verify checkout exists and the fetch completed OK ---------------------
  if (-not (Test-Path $src)) {
    throw ("chromium src not found at " + $src + " -- run scripts\_fetch-chromium.ps1 first")
  }
  if (-not (Test-Path $fetchDone)) {
    throw ("fetch marker missing: " + $fetchDone + " -- chromium fetch not confirmed complete")
  }
  $fetchState = (Get-Content $fetchDone -Raw).Trim()
  Log ("chromium-fetch.done = " + $fetchState)
  if ($fetchState -notmatch '^OK') {
    throw ("chromium fetch did not finish OK (marker: " + $fetchState + ")")
  }

  # (b) copy the T2 / matrix test sources into ui/accessibility/ --------------
  # Only the .cc test sources + the minimal test main belong in the accessibility
  # dir. The *.gn files in tests\linux-t2 are NOT copied here: args.gn is a gn
  # args file (consumed via --args / the build dir) and t2-root-BUILD.gn is a
  # standalone root group for the lean ax_t2_unittests cone -- neither overwrites
  # ui/accessibility/BUILD.gn. The patch's ax_t2_unittests target picks up these
  # sources (step c); that target = the 109 tests.
  Log "Copying T2 test sources into ui/accessibility/ ..."
  $cc = Get-ChildItem -Path $t2dir -Filter *.cc -File
  foreach ($f in $cc) {
    Copy-Item -Path $f.FullName -Destination $axDir -Force
    Log ("  copied " + $f.Name)
  }
  if ($cc.Count -eq 0) { throw ("no .cc test sources found in " + $t2dir) }

  # (c) apply the BUILD.gn patch (adds ax_t2_minimal arg + the test targets) --
  # git apply from the src dir; --check first so an already-applied patch is a
  # clean skip rather than a hard failure (idempotent re-runs).
  $patch = Join-Path $t2dir "ui-accessibility-BUILD.gn.patch"
  if (-not (Test-Path $patch)) { throw ("patch not found: " + $patch) }
  Set-Location $src

  Log "Checking whether the BUILD.gn patch is already applied ..."
  $patchQ = '"' + $patch + '"'
  $alreadyApplied = $false
  # --reverse --check succeeds (exit 0) when the patch is ALREADY in the tree.
  cmd /c "git apply -p0 --reverse --check $patchQ >> `"$log`" 2>&1"
  if ($LASTEXITCODE -eq 0) {
    $alreadyApplied = $true
    Log "BUILD.gn patch already applied -- skipping."
  }

  if (-not $alreadyApplied) {
    Log "Applying ui-accessibility-BUILD.gn.patch via git apply ..."
    $ec = Run ("git apply -p0 --verbose " + $patchQ)
    if ($ec -ne 0) {
      # FALLBACK NOTE: if `git apply` fails (e.g. line-ending or context drift on
      # a re-synced tree), apply manually instead:
      #   - from <src>:  git apply --3way <patch>     (uses blob context to merge)
      #   - or patch -p1 < <patch>  (GnuWin/Git-bundled patch.exe)
      #   - or hand-edit ui/accessibility/BUILD.gn per the patch hunks:
      #       declare_args { ax_t2_minimal = false }, the two !ax_t2_minimal
      #       guards, and the test("ax_t2_unittests") / test("accessibility_unittests")
      #       blocks. Verify with: git apply --reverse --check <patch>
      throw ("git apply failed (exit " + $ec + ") -- see FALLBACK NOTE in this script")
    }
  }

  # (d) gn gen -- canonical args: release, component, dchecks on ---------------
  # Force a deterministic LOCAL build: Chromium 149 defaults to siso + remote
  # exec (RBE), which needs Google-internal auth we do not have. use_remoteexec=false
  # + use_siso=false => classic local ninja. Binaries/test results are identical;
  # this is a local-build choice, not a source deviation (V10 zero-deviation intact).
  # ax_t2_minimal=true builds the LEAN cone (drops ui/base/ui/strings; keeps
  # base:i18n) -- the exact config the proven Linux 109 used. It also avoids
  # ui/gfx->skia->dawn(WebGPU)->dxil.dll, which the full cone pulls and which
  # the local Win11 SDK 26100 ships WITHOUT (dxil.dll missing; SDK dir not
  # user-writable). Same 109 tests, no graphics. The full-canonical build (V10)
  # is deferred until dxil.dll is placed in the SDK (needs elevation) or Dawn disabled.
  Log "Running gn gen out\rel (lean cone, local build: no RBE, classic ninja) ..."
  $ec = Run ('gn gen ' + $outDir + ' --args="is_debug=false is_component_build=true dcheck_always_on=true use_remoteexec=false use_siso=false ax_t2_minimal=true"')
  if ($ec -ne 0) { throw ("gn gen failed: " + $ec) }

  # (e) ninja -- build the patch's ax_t2_unittests target. THIS target (not
  # accessibility_unittests) bundles the upstream ax_event_generator_unittest
  # baseline + the T2/matrix tests = the 109. Confirmed against the patch:
  # accessibility_unittests does NOT contain the T2 sources (trailing diff context).
  Log "Running ninja ax_t2_unittests (long) ..."
  $ec = Run ("ninja -C " + $outDir + " ax_t2_unittests")
  if ($ec -ne 0) { throw ("ninja failed: " + $ec) }

  # (f) run the tests, AX* filter, capture the pass count --------------------
  if (-not (Test-Path $testExe)) { throw ("test exe not produced: " + $testExe) }
  Log "Running accessibility_unittests --gtest_filter=AX* ..."
  $ec = Run ('"' + $testExe + '" --gtest_filter=AX*')
  $testExit = $ec

  # Parse the gtest summary out of the log: "[  PASSED  ] N tests." (and FAILED).
  $passed = $null
  $failed = $null
  $logText = Get-Content $log
  foreach ($line in $logText) {
    if ($line -match '\[\s*PASSED\s*\]\s+(\d+)\s+test') { $passed = [int]$Matches[1] }
    if ($line -match '\[\s*FAILED\s*\]\s+(\d+)\s+test') { $failed = [int]$Matches[1] }
  }
  Log ("gtest PASSED = " + $passed + " ; FAILED = " + $failed + " ; exit = " + $testExit)

  # (g) record result --------------------------------------------------------
  if ($testExit -ne 0) {
    throw ("tests reported failures (exit " + $testExit + ", FAILED=" + $failed + ")")
  }
  $summary = "OK passed=" + $passed
  if ($failed) { $summary = $summary + " failed=" + $failed }
  Log ("=== DONE " + $summary + " ===")
  Set-Content -Path $done -Value $summary
} catch {
  $msg = $_.Exception.Message
  Log ("=== FAILED: " + $msg + " ===")
  Set-Content -Path $done -Value ("FAIL " + $msg)
}
