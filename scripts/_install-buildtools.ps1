#Requires -Version 5.1
# Focused elevated install: VS Build Tools (C++ + Win11 SDK) only.
# Launched elevated (RunAs) so the single UAC prompt covers the one admin step.
# Logs to scripts\buildtools-install.log and writes scripts\buildtools-install.done.

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = "C:\Dev\Projects\45 - AccWebEdit\scripts"
$log  = Join-Path $root "buildtools-install.log"
$done = Join-Path $root "buildtools-install.done"
Remove-Item $done -ErrorAction SilentlyContinue

function Log([string]$m) {
  $stamp = Get-Date -Format "HH:mm:ss"
  Add-Content -Path $log -Value ("[" + $stamp + "] " + $m)
}

try {
  Log "=== VS Build Tools install starting, elevated ==="

  if (Test-Path "C:\BuildTools\VC\Tools\MSVC") {
    Log "VS Build Tools already present at C:\BuildTools, skipping install."
  } else {
    Log "Downloading vs_buildtools bootstrapper ..."
    $vsBoot = Join-Path $env:TEMP "vs_buildtools.exe"
    Invoke-WebRequest "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $vsBoot
    Log "Running bootstrapper, C++ workload + Win11 SDK + ATL/MFC, about 10-20 min ..."
    $a = @(
      "--quiet","--wait","--norestart","--nocache","--installPath","C:\BuildTools",
      "--add","Microsoft.VisualStudio.Workload.VCTools","--includeRecommended",
      "--add","Microsoft.VisualStudio.Component.VC.ATLMFC",
      "--add","Microsoft.VisualStudio.Component.Windows11SDK.26100"
    )
    $p = Start-Process $vsBoot -ArgumentList $a -Wait -PassThru
    $code = $p.ExitCode
    Log ("Bootstrapper exit code: " + $code)
    if ($code -ne 0 -and $code -ne 3010) { throw ("VS Build Tools install failed: " + $code) }
  }

  Log "Setting machine env vars vs2022_install and DEPOT_TOOLS_WIN_TOOLCHAIN=0 ..."
  [Environment]::SetEnvironmentVariable("vs2022_install","C:\BuildTools","Machine")
  [Environment]::SetEnvironmentVariable("DEPOT_TOOLS_WIN_TOOLCHAIN","0","Machine")

  Log "=== DONE OK ==="
  Set-Content -Path $done -Value "OK 0"
} catch {
  $msg = $_.Exception.Message
  Log ("=== FAILED: " + $msg + " ===")
  Set-Content -Path $done -Value ("FAIL " + $msg)
}
