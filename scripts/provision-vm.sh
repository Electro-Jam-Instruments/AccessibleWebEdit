#!/usr/bin/env bash
# Ready-to-paste provisioning for the AccessibleWebEdit Windows 11 VM (TASK-00).
#
# RUN THIS FROM YOUR OWN MACHINE, not from a Claude Code cloud session:
# the Azure management plane (management.azure.com) is firewalled out of the
# cloud sandbox (verified 2026-06-15: egress deny "host_not_allowed"), so the
# az CLI cannot reach ARM from there. Locally, where `az login` holds your
# creds, this just works.
#
# Usage:
#   az login                                  # once, in your shell
#   export ADMIN_PASSWORD='<a strong password>'
#   ./provision-vm.sh                         # or: SKU=Standard_D32s_v5 ./provision-vm.sh
#
# After it finishes, run the Phase-1 bootstrap (see docs/TASK-00-vm-bootstrap.md
# for bootstrap.ps1) via Run Command, then RDP in once for `claude auth login`.

set -euo pipefail

# --- required ---
: "${ADMIN_PASSWORD:?Set ADMIN_PASSWORD to a strong password (>=14 chars, mixed) before running}"

# --- overridable knobs (sensible defaults) ---
RG=${RG:-awe-blite}
LOC=${LOC:-eastus2}
VM=${VM:-awe-blite-vm}
ADMIN=${ADMIN:-aweadmin}
# Build host class. Disk is the binding constraint, not RAM:
#   Standard_D16s_v5  = 16 vCPU / 64 GB  (TASK-00 baseline; comfortable)
#   Standard_D32s_v5  = 32 vCPU / 128 GB (~2x faster Chromium build; this is
#                                         where you'd "get" 128 GB RAM)
SKU=${SKU:-Standard_D16s_v5}
DISK_GB=${DISK_GB:-512}     # OS disk. ~100-120 GB gclient checkout + build out/;
                            # 256 GB is the floor, 512 GB is safe. NOT 128.
IMAGE=${IMAGE:-MicrosoftWindowsDesktop:windows-11:win11-24h2-pro:latest}

# --- preflight: confirm you're logged in (this is the step that needs YOUR creds) ---
echo "== Azure account =="
az account show -o table || { echo "Run 'az login' first."; exit 1; }

# --- auto-detect your public IP so RDP is locked to you only ---
MYIP=$(curl -fsS https://api.ipify.org 2>/dev/null || curl -fsS https://ifconfig.me 2>/dev/null || true)
if [ -z "${MYIP}" ]; then
  echo "Could not auto-detect your public IP. Set MYIP=<your.ip> and re-run." >&2
  exit 1
fi
echo "== RDP will be locked to ${MYIP}/32 (3389 NOT open to the internet) =="

# --- create ---
az group create -n "$RG" -l "$LOC"

az vm create \
  -g "$RG" -n "$VM" \
  --image "$IMAGE" \
  --license-type Windows_Client \
  --size "$SKU" \
  --os-disk-size-gb "$DISK_GB" \
  --storage-sku Premium_LRS \
  --admin-username "$ADMIN" \
  --admin-password "$ADMIN_PASSWORD" \
  --public-ip-sku Standard \
  --nsg-rule NONE

# az vm create with --nsg-rule NONE makes a default NSG named ${VM}NSG.
az network nsg rule create -g "$RG" --nsg-name "${VM}NSG" -n AllowRdpFromMe \
  --priority 1000 --access Allow --protocol Tcp --destination-port-ranges 3389 \
  --source-address-prefixes "${MYIP}/32"

IP=$(az vm show -d -g "$RG" -n "$VM" --query publicIps -o tsv 2>/dev/null || echo "<see portal>")
cat <<EOF

== VM provisioned ==
  Resource group : $RG
  VM             : $VM   ($SKU, ${DISK_GB} GB Premium SSD, Win11 24H2)
  Public IP      : $IP   (RDP locked to ${MYIP}/32)

Next steps (from docs/TASK-00-vm-bootstrap.md):
  1. Save bootstrap.ps1 (the Phase-1 script in TASK-00) locally.
  2. Run it on the VM:
       az vm run-command invoke -g $RG -n $VM --command-id RunPowerShellScript \\
         --scripts @bootstrap.ps1 \\
         --parameters "GitHubPat=<FINE_GRAINED_PAT>" "UserName=$ADMIN" "UserPassword=<ADMIN_PASSWORD>"
  3. The script reboots the VM; RDP in once as $ADMIN and run:  claude auth login
  4. Remote Control comes up -> connect from claude.ai/code; a VM-local Claude
     session then drives the build/test/NVDA work.

Licensing reminder: --license-type Windows_Client attests you hold eligible
per-user Windows licensing (Windows E3/E5 or M365 E3/E5/F3). Azure does not
verify it; it's your compliance attestation.
EOF
