#Requires -Version 5.1
# Build + link blite_host_win in out\host with the FULL accessibility cone
# (ax_t2_minimal=false). The platform layer (accessibility_platform.dll) the host
# needs requires ui::ToLocalizedString (ax_enum_localization_util.cc + ui/strings),
# which the lean cone drops -> the lean dir can't link the host. Full cone is bigger
# but is the correct config for the UIA platform layer.
# Building/linking is allowed under Smart App Control; only RUNNING the exe is blocked.
# Logs to results\blite-link.log; marker scripts\blite-link.done (OK/FAIL).
param([string]$SrcRoot = "C:\src")
$ErrorActionPreference = "Stop"
$proj="C:\Dev\Projects\45 - AccWebEdit"; $scripts=Join-Path $proj "scripts"; $results=Join-Path $proj "results"
$log=Join-Path $results "blite-link.log"; $done=Join-Path $scripts "blite-link.done"
$src=Join-Path $SrcRoot "chromium\src"; $depot=Join-Path $SrcRoot "depot_tools"
$out="out\host"; $exe=Join-Path $src "out\host\blite_host_win.exe"
Remove-Item $done -ErrorAction SilentlyContinue
Set-Content -Path $log -Value "=== blite_host_win full-cone build run ==="
function Log([string]$m){ Add-Content $log ("["+(Get-Date -Format HH:mm:ss)+"] "+$m) }
function Run([string]$c){ Log ("RUN: "+$c); $q='"'+$log+'"'; cmd /c "$c >> $q 2>&1"; $e=$LASTEXITCODE; Log ("exit: "+$e); return $e }
try {
  $env:Path="$depot;"+$env:Path; $env:DEPOT_TOOLS_WIN_TOOLCHAIN="0"
  $env:vs2022_install="C:\BuildTools"; $env:GYP_MSVS_VERSION="2022"
  Set-Location $src
  Log "gn gen out\host (FULL cone: ax_t2_minimal=false, warnings non-fatal) ..."
  $e=Run ('gn gen '+$out+' --args="is_debug=false is_component_build=true symbol_level=0 dcheck_always_on=true use_remoteexec=false use_siso=false treat_warnings_as_errors=false"')
  if($e -ne 0){ throw "gn gen failed: $e" }
  Log "ninja ui/accessibility:blite_win (full cone, long) ..."
  $e=Run ("ninja -C "+$out+" ui/accessibility:blite_win")
  if($e -ne 0){ throw "ninja failed: $e" }
  if(-not (Test-Path $exe)){ throw "exe not produced: $exe" }
  Log ("=== DONE OK linked: "+$exe+" ===")
  Set-Content $done "OK linked"
} catch { $m=$_.Exception.Message; Log ("=== FAILED: "+$m+" ==="); Set-Content $done ("FAIL "+$m) }
