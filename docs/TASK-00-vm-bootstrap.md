# TASK-00 — Windows VM Bootstrap for the B-lite Host

Status: ready to execute. This is the provisioning guide and bootstrap script that creates the Windows environment for Tier 2 tasks T2-1 through T2-7 (section 09).

Claude Code install and Remote Control facts below were verified against the official docs (code.claude.com/docs, retrieved 2026-06-12): native Windows install is `irm https://claude.ai/install.ps1 | iex`; Remote Control needs Claude Code v2.1.51+ and a **full-scope claude.ai OAuth login** — API keys and `claude setup-token` / `CLAUDE_CODE_OAUTH_TOKEN` tokens are inference-only and are rejected for Remote Control. That constraint forces the two-phase design described next.

## Architecture of the bootstrap (read first)

Azure Run Command executes as SYSTEM in session 0. Two consequences:

1. **UIA needs an interactive desktop session.** `ax_dump_tree`, `ax_dump_events`, Narrator, and any UIA client must run in a logged-on user session, not session 0. The script therefore configures auto-logon and registers a logon scheduled task that finishes the per-user setup and starts Claude Code in the interactive session.
2. **Remote Control cannot be authenticated unattended.** It requires `claude auth login` (claude.ai OAuth in a browser). This is a one-time, ~2-minute RDP step after first boot. Everything else — prerequisites, Claude Code binary install, GitHub auth, repo clone, Remote Control autostart — is unattended. After the one-time login, the credential persists and the VM is fully hands-off across reboots.

So: **Phase 1** (SYSTEM, via Run Command) installs machine-wide prerequisites and arms Phase 2. **Phase 2** (user session, automatic at logon) installs Claude Code per-user, wires GitHub credentials, clones the repo, and launches `claude remote-control`. **One manual step** sits between first boot and steady state: RDP in once and run `claude auth login`.

## VM provisioning

Target: 16 vCPU, 64 GB RAM, 500+ GB SSD. A Chromium checkout plus build of the 149.0.7827.115 tag wants every bit of that.

Recommended: **Windows Server 2022 Datacenter (Desktop Experience)** — UIA behaves identically to Windows 11 for our purposes (Windows Server 2019+ is a supported Claude Code platform), and the Azure licensing is simpler than Windows 11 client images, which require eligible per-user licensing. If you specifically want Windows 11 parity, use image `MicrosoftWindowsDesktop:windows-11:win11-24h2-pro:latest` instead and confirm license eligibility.

```bash
RG=awe-blite
LOC=eastus2
VM=awe-blite-vm
ADMIN=aweadmin

az group create -n $RG -l $LOC

az vm create \
  -g $RG -n $VM \
  --image MicrosoftWindowsServer:WindowsServer:2022-datacenter-azure-edition:latest \
  --size Standard_D16s_v5 \
  --os-disk-size-gb 512 \
  --storage-sku Premium_LRS \
  --admin-username $ADMIN \
  --admin-password '<STRONG-PASSWORD>' \
  --public-ip-sku Standard \
  --nsg-rule NONE

# RDP only from your IP. Do not open 3389 to the internet:
az network nsg rule create -g $RG --nsg-name ${VM}NSG -n AllowRdpFromMe \
  --priority 1000 --access Allow --protocol Tcp --destination-port-ranges 3389 \
  --source-address-prefixes <YOUR_IP>/32
```

Notes:

- `Standard_D16s_v5` = 16 vCPU / 64 GB. `D16ds_v5` adds a fast temp disk if you want the build's intermediate output off the OS disk.
- The VM must stay running with the user logged on for UIA work and for Remote Control (the local `claude` process must keep running; an extended network outage of ~10 minutes ends the session and the autostart task re-arms it at next logon/restart).
- Auto-logon stores the password in the registry. Acceptable for a disposable research VM behind a locked-down NSG; do not reuse a password you care about.

## Running the bootstrap

Save the script below as `bootstrap.ps1`, then:

```bash
az vm run-command invoke -g $RG -n $VM \
  --command-id RunPowerShellScript \
  --scripts @bootstrap.ps1 \
  --parameters "GitHubPat=<FINE_GRAINED_PAT>" "UserName=aweadmin" "UserPassword=<STRONG-PASSWORD>"
```

The PAT needs `contents: read/write` on `Electro-Jam/AccessibleWebEdit` only. Run Command has a ~90-minute ceiling; the VS Build Tools install is the long pole (~10–20 min). The script restarts the VM at the end to trigger auto-logon and Phase 2.

**After the restart:** RDP in once as `aweadmin`, open the `Claude Remote Control` console window (Phase 2 opens it automatically; it will be waiting on auth), run `claude auth login`, complete the browser flow. Remote Control comes up and prints a session URL / QR code; from then on the session is reachable from claude.ai/code or the Claude mobile app, and the autostart task re-establishes it on every logon.

## bootstrap.ps1

```powershell
param(
    [Parameter(Mandatory)] [string]$GitHubPat,
    [Parameter(Mandatory)] [string]$UserName,
    [Parameter(Mandatory)] [string]$UserPassword,
    [string]$RepoUrl  = "https://github.com/Electro-Jam/AccessibleWebEdit.git",
    [string]$SrcRoot  = "C:\src"
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$Boot = "C:\bootstrap"
New-Item -ItemType Directory -Force -Path $Boot, $SrcRoot | Out-Null
Start-Transcript -Path "$Boot\phase1.log" -Append

# ---------------------------------------------------------------- Phase 1: machine-wide (SYSTEM)

# --- Git for Windows (silent, latest release) ---
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    $rel = Invoke-RestMethod "https://api.github.com/repos/git-for-windows/git/releases/latest"
    $asset = $rel.assets | Where-Object { $_.name -match "^Git-.*-64-bit\.exe$" } | Select-Object -First 1
    $gitExe = "$Boot\git-installer.exe"
    Invoke-WebRequest $asset.browser_download_url -OutFile $gitExe
    Start-Process $gitExe -ArgumentList "/VERYSILENT","/NORESTART","/NOCANCEL","/SP-" -Wait
}
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine")

# --- VS Build Tools: C++ workload + Windows 11 SDK (Chromium 149 requirements) ---
$vsBoot = "$Boot\vs_buildtools.exe"
Invoke-WebRequest "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $vsBoot
$vsArgs = @(
    "--quiet","--wait","--norestart","--nocache",
    "--installPath","C:\BuildTools",
    "--add","Microsoft.VisualStudio.Workload.VCTools",
    "--includeRecommended",
    "--add","Microsoft.VisualStudio.Component.VC.ATLMFC",
    "--add","Microsoft.VisualStudio.Component.Windows11SDK.26100"
)
$p = Start-Process $vsBoot -ArgumentList $vsArgs -Wait -PassThru
if ($p.ExitCode -notin 0,3010) { throw "VS Build Tools install failed: $($p.ExitCode)" }

# --- depot_tools ---
$dt = "$SrcRoot\depot_tools"
if (-not (Test-Path "$dt\gclient.bat")) {
    $zip = "$Boot\depot_tools.zip"
    Invoke-WebRequest "https://storage.googleapis.com/chrome-infra/depot_tools_win.zip" -OutFile $zip
    Expand-Archive $zip -DestinationPath $dt -Force
}
$mp = [Environment]::GetEnvironmentVariable("Path","Machine")
if ($mp -notlike "*$dt*") {
    # depot_tools must precede any other python/git on PATH
    [Environment]::SetEnvironmentVariable("Path", "$dt;$mp", "Machine")
}
[Environment]::SetEnvironmentVariable("DEPOT_TOOLS_WIN_TOOLCHAIN","0","Machine")
[Environment]::SetEnvironmentVariable("vs2022_install","C:\BuildTools","Machine")

# --- Auto-logon (required: UIA tooling and Remote Control live in the interactive session) ---
$wl = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon"
Set-ItemProperty $wl -Name AutoAdminLogon  -Value "1"
Set-ItemProperty $wl -Name DefaultUserName -Value $UserName
Set-ItemProperty $wl -Name DefaultPassword -Value $UserPassword
Remove-ItemProperty $wl -Name AutoLogonCount -ErrorAction SilentlyContinue

# --- Stash the PAT for Phase 2, readable only by the target user and admins ---
$patFile = "$Boot\github.pat"
Set-Content -Path $patFile -Value $GitHubPat -NoNewline
$acl = Get-Acl $patFile
$acl.SetAccessRuleProtection($true,$false)
foreach ($id in @($UserName,"BUILTIN\Administrators","NT AUTHORITY\SYSTEM")) {
    $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($id,"FullControl","Allow")))
}
Set-Acl $patFile $acl

# ---------------------------------------------------------------- Phase 2 payload (runs at user logon)

$phase2 = @'
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Start-Transcript -Path "C:\bootstrap\phase2.log" -Append
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
            [Environment]::GetEnvironmentVariable("Path","User")

# Claude Code: current official native Windows install (per code.claude.com/docs/en/setup)
$claude = "$env:USERPROFILE\.local\bin\claude.exe"
if (-not (Test-Path $claude)) {
    irm https://claude.ai/install.ps1 | iex
}
& $claude --version

# GitHub auth: user-level credential store fed from the ACL'd PAT file
$pat = (Get-Content "C:\bootstrap\github.pat" -Raw).Trim()
git config --global credential.helper store
git config --global user.name  "AWE Build VM"
git config --global user.email "awe-vm@electro-jam.com"
Set-Content -Path "$env:USERPROFILE\.git-credentials" -Value "https://x-access-token:$pat@github.com" -NoNewline

# Clone the planning repo
$repo = "C:\src\AccessibleWebEdit"
if (-not (Test-Path "$repo\.git")) {
    git clone https://github.com/Electro-Jam/AccessibleWebEdit.git $repo
}

# Start Claude Code with Remote Control (server mode keeps running and waits for connections).
# First boot only: this blocks until `claude auth login` has been completed once over RDP --
# Remote Control requires full-scope claude.ai OAuth; setup-token/API keys are inference-only.
Set-Location $repo
& $claude remote-control --name "AWE B-lite VM"
Stop-Transcript
'@
Set-Content -Path "$Boot\phase2.ps1" -Value $phase2

# Logon task: runs Phase 2 in the interactive session, visible console window, restarts on logon
$action  = New-ScheduledTaskAction -Execute "powershell.exe" `
           -Argument "-ExecutionPolicy Bypass -NoExit -File $Boot\phase2.ps1"
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $UserName
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) `
            -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 2)
Register-ScheduledTask -TaskName "Claude Remote Control" -Action $action -Trigger $trigger `
    -Settings $settings -User $UserName -RunLevel Highest -Force

Stop-Transcript
Restart-Computer -Force
```

## Post-bootstrap verification checklist

In the RDP session after the one-time `claude auth login`:

1. `claude --version` reports v2.1.51 or later (native installs auto-update, so this will hold).
2. The `Claude Remote Control` console shows server mode running with a session URL; the session appears at claude.ai/code with a green dot. Press spacebar in that console for the QR code.
3. `git -C C:\src\AccessibleWebEdit pull` succeeds without prompting (PAT wired).
4. `gclient --version` works in a fresh shell and `where python` resolves inside `C:\src\depot_tools` first.
5. `echo %DEPOT_TOOLS_WIN_TOOLCHAIN%` prints `0` (use Google-internal toolchain off; local VS Build Tools at `C:\BuildTools` via `vs2022_install`).

## First work order for the VM session

From claude.ai/code, connect to the `AWE B-lite VM` session and kick off the Chromium fetch — it is hours of wall clock and ~200 GB, so it goes first:

```
mkdir C:\src\chromium && cd C:\src\chromium
fetch --no-history chromium        # then:
cd src && git fetch origin tag 149.0.7827.115 && git checkout 149.0.7827.115 && gclient sync -D
```

Then proceed to T2-1 (section 09): build the `ax_dump_tree` / `ax_dump_events` targets and the B-lite host skeleton.

## Known limitations / honesty section

- **The one manual step is real.** Anthropic's docs are explicit that Remote Control rejects inference-only credentials ("Remote Control requires a full-scope login token"), so no PAT-style secret can replace the browser login. It is once per VM, not once per session.
- On Team/Enterprise plans an admin must enable the Remote Control toggle at claude.ai/admin-settings/claude-code before the login will work.
- Auto-logon + stored password trades security for unattended operation; mitigate with the single-IP NSG rule and treat the VM as disposable.
- Azure Run Command output is capped (~4 KB); full logs are in `C:\bootstrap\phase1.log` and `phase2.log`.
- If the Run Command times out during the VS Build Tools step, re-invoke the same script: every step is idempotent (install checks, `-Force` task registration, clone guard).
