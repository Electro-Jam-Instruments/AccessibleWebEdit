#Requires -Version 5.1
<#
  setup-local-windows.ps1 — turn a local Windows 11 box (e.g. Minisforum MS-01)
  into the AccessibleWebEdit build/test host, skipping the Azure VM entirely.

  Run in an ELEVATED PowerShell (Admin) the first time — VS Build Tools needs it.
  Idempotent: re-running skips pieces that are already installed/checked out.

  Usage:
    powershell -ExecutionPolicy Bypass -File .\setup-local-windows.ps1
    # put the checkout on a second NVMe if the OS drive is tight:
    powershell -ExecutionPolicy Bypass -File .\setup-local-windows.ps1 -SrcRoot D:\src

  See docs/TASK-00b-local-windows-setup.md.
#>
param(
  [string]$SrcRoot   = "C:\src",
  [string]$Tag       = "149.0.7827.115",
  [switch]$NoHistory = $true
)
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Have($cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }

# --- 0. Disk space check (the binding constraint) -------------------------------
$qualifier = (Split-Path -Qualifier $SrcRoot).TrimEnd(':')
$freeGB = [math]::Round((Get-PSDrive $qualifier).Free / 1GB, 0)
Write-Host "Free space on ${qualifier}: : $freeGB GB"
if ($freeGB -lt 400) {
  Write-Warning "A gclient checkout + build wants ~400-500 GB free; $qualifier has $freeGB GB."
  Write-Warning "Use -SrcRoot on a larger drive (the MS-01 has a second NVMe slot), or expect to use the lean-subset fallback (results/ENVIRONMENT.md)."
}
New-Item -ItemType Directory -Force -Path $SrcRoot | Out-Null

# --- 1. Git for Windows ---------------------------------------------------------
if (-not (Have git)) {
  Write-Host "Installing Git for Windows..."
  winget install --id Git.Git -e --source winget --accept-package-agreements --accept-source-agreements
  $env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
              [Environment]::GetEnvironmentVariable("Path","User")
}

# --- 2. VS Build Tools: C++ workload + Windows 11 SDK ---------------------------
$vsInstalled = Test-Path "C:\BuildTools\VC\Tools\MSVC"
if (-not $vsInstalled) {
  Write-Host "Installing VS Build Tools (C++ + Win11 SDK)... this is the long pole (~10-20 min)."
  $vsBoot = Join-Path $env:TEMP "vs_buildtools.exe"
  Invoke-WebRequest "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $vsBoot
  $args = @(
    "--quiet","--wait","--norestart","--nocache","--installPath","C:\BuildTools",
    "--add","Microsoft.VisualStudio.Workload.VCTools","--includeRecommended",
    "--add","Microsoft.VisualStudio.Component.VC.ATLMFC",
    "--add","Microsoft.VisualStudio.Component.Windows11SDK.26100"
  )
  $p = Start-Process $vsBoot -ArgumentList $args -Wait -PassThru
  if ($p.ExitCode -notin 0,3010) { throw "VS Build Tools install failed: $($p.ExitCode)" }
}
[Environment]::SetEnvironmentVariable("vs2022_install","C:\BuildTools","Machine")
[Environment]::SetEnvironmentVariable("DEPOT_TOOLS_WIN_TOOLCHAIN","0","Machine")

# --- 3. depot_tools -------------------------------------------------------------
$dt = Join-Path $SrcRoot "depot_tools"
if (-not (Test-Path (Join-Path $dt "gclient.bat"))) {
  Write-Host "Installing depot_tools..."
  $zip = Join-Path $env:TEMP "depot_tools.zip"
  Invoke-WebRequest "https://storage.googleapis.com/chrome-infra/depot_tools_win.zip" -OutFile $zip
  Expand-Archive $zip -DestinationPath $dt -Force
}
# depot_tools must come FIRST on PATH so its bundled python/git win.
$machinePath = [Environment]::GetEnvironmentVariable("Path","Machine")
if ($machinePath -notlike "*$dt*") {
  [Environment]::SetEnvironmentVariable("Path", "$dt;$machinePath", "Machine")
}
$env:Path = "$dt;" + $env:Path
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
# Bootstrap depot_tools' bundled tools once.
& cmd /c "gclient --version" | Out-Null

# --- 4. Chromium checkout at the pinned tag ------------------------------------
$chromium = Join-Path $SrcRoot "chromium"
if (-not (Test-Path (Join-Path $chromium "src\.git"))) {
  Write-Host "Fetching Chromium (this is large and slow)..."
  New-Item -ItemType Directory -Force -Path $chromium | Out-Null
  Push-Location $chromium
  if ($NoHistory) { & cmd /c "fetch --no-history chromium" } else { & cmd /c "fetch chromium" }
  Pop-Location
}
Push-Location (Join-Path $chromium "src")
Write-Host "Checking out tag $Tag ..."
& cmd /c "git fetch --tags origin"
& cmd /c "git checkout $Tag"
& cmd /c "gclient sync -D"
Pop-Location

# --- 5. NVDA (verification layer; docs/TASK-00a) --------------------------------
if (-not (Test-Path "$env:ProgramFiles\nvda\nvda.exe")) {
  Write-Host "Installing NVDA (silent)..."
  $rel = Invoke-RestMethod "https://api.github.com/repos/nvaccess/nvda/releases/latest"
  $asset = $rel.assets | Where-Object { $_.name -match '^nvda_.*\.exe$' } | Select-Object -First 1
  $nvda = Join-Path $env:TEMP "nvda_installer.exe"
  Invoke-WebRequest $asset.browser_download_url -OutFile $nvda
  & $nvda --install-silent --enable-start-on-logon=False
}
New-Item -ItemType Directory -Force -Path C:\awe\logs, C:\awe\nvda-config | Out-Null

Write-Host ""
Write-Host "=== Local Windows build host ready ==="
Write-Host "Open a FRESH terminal (so PATH/env apply), then:"
Write-Host "  cd $chromium\src"
Write-Host "  gn gen out\rel --args=`"is_debug=false is_component_build=true dcheck_always_on=true`""
Write-Host "  ninja -C out\rel accessibility_unittests"
Write-Host "Then let local Claude Code drive the VM-queue work (NEXT-QUESTIONS #5-7),"
Write-Host "the B-lite host, NVDA capture (docs/TASK-00a), and the patches (patches/)."
