#Requires -Version 5.1
# Phase 1 (static variant): same as _phase1-build.ps1 but is_component_build=false,
# into out\static. A single static test binary avoids the component-build cross-DLL
# static-initializer exit (0xc0e90002) seen when running the component build's
# ax_t2_unittests.exe. Logs to results\phase1-static.log; marker scripts\phase1-static.done.
param([string]$SrcRoot = "C:\src")
$ErrorActionPreference = "Stop"
$proj="C:\Dev\Projects\45 - AccWebEdit"; $scripts=Join-Path $proj "scripts"; $results=Join-Path $proj "results"
$log=Join-Path $results "phase1-static.log"; $done=Join-Path $scripts "phase1-static.done"
$src=Join-Path $SrcRoot "chromium\src"; $depot=Join-Path $SrcRoot "depot_tools"
$out="out\static"; $testExe=Join-Path $src "out\static\ax_t2_unittests.exe"
Remove-Item $done -ErrorAction SilentlyContinue
Set-Content -Path $log -Value "=== phase1 STATIC build run ==="
function Log([string]$m){ Add-Content $log ("["+(Get-Date -Format HH:mm:ss)+"] "+$m) }
function Run([string]$c){ Log ("RUN: "+$c); $q='"'+$log+'"'; cmd /c "$c >> $q 2>&1"; $e=$LASTEXITCODE; Log ("exit: "+$e); return $e }
try {
  $env:Path="$depot;"+$env:Path; $env:DEPOT_TOOLS_WIN_TOOLCHAIN="0"
  $env:vs2022_install="C:\BuildTools"; $env:GYP_MSVS_VERSION="2022"
  Log "gn gen out\static (is_component_build=false) ..."
  $e=Run ('gn gen '+$out+' --args="is_debug=false is_component_build=false symbol_level=0 dcheck_always_on=true use_remoteexec=false use_siso=false ax_t2_minimal=true treat_warnings_as_errors=false"')
  if($e -ne 0){ throw "gn gen failed: $e" }
  Log "ninja ax_t2_unittests (static, long) ..."
  $e=Run ("ninja -C "+$out+" ax_t2_unittests"); if($e -ne 0){ throw "ninja failed: $e" }
  if(-not (Test-Path $testExe)){ throw "exe not produced: $testExe" }
  Log "running tests ..."
  Set-Location (Join-Path $src "out\static")
  $e=Run ('"'+$testExe+'" --gtest_filter=AX*'); $texit=$e
  $passed=$null;$failed=$null
  foreach($l in (Get-Content $log)){ if($l -match '\[\s*PASSED\s*\]\s+(\d+)\s+test'){$passed=[int]$Matches[1]}; if($l -match '\[\s*FAILED\s*\]\s+(\d+)\s+test'){$failed=[int]$Matches[1]} }
  Log ("PASSED="+$passed+" FAILED="+$failed+" exit="+$texit)
  if($texit -ne 0){ throw ("tests failed (exit "+$texit+", FAILED="+$failed+")") }
  $s="OK passed="+$passed; if($failed){$s+=" failed="+$failed}
  Log ("=== DONE "+$s+" ==="); Set-Content $done $s
} catch { $m=$_.Exception.Message; Log ("=== FAILED: "+$m+" ==="); Set-Content $done ("FAIL "+$m) }
