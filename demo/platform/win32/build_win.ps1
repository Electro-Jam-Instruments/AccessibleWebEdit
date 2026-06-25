# Build the Win32 interactive demo on Windows 11.
#
# Prereqs: VS Build Tools with the C++ workload (the same install
# scripts/setup-local-windows.ps1 provisions). Run from a "Developer PowerShell
# for VS" so cl.exe is on PATH, or this script will locate it via vswhere.
#
# Usage:  pwsh -File build_win.ps1   (then run .\win_demo.exe)

$ErrorActionPreference = "Stop"

function Find-Cl {
  if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return "cl.exe" }
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) {
    throw "cl.exe not found and vswhere missing. Open a 'Developer PowerShell for VS' and retry."
  }
  $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
  Write-Host "Importing MSVC environment from $vcvars"
  cmd /c "`"$vcvars`" && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
  }
  return "cl.exe"
}

$cl = Find-Cl
$core = Join-Path $PSScriptRoot "..\..\core"

Write-Host "Compiling win_main.cc ..."
& $cl /nologo /std:c++17 /EHsc /O2 /DUNICODE /D_UNICODE `
  "/I$core" `
  (Join-Path $PSScriptRoot "win_main.cc") `
  /Fe:win_demo.exe `
  /link user32.lib gdi32.lib

Write-Host "Built win_demo.exe. Run it, then type in the window."
