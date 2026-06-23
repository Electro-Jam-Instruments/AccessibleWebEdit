# TASK-00b — Local Windows build/test host (skip Azure)

Date: 2026-06-16. If you have a Windows 11 machine with enough disk (the Minisforum MS-01: i9-13900H ~14C/20T, 64 GB RAM, dual NVMe), use **it** as the build/test host instead of provisioning the Azure VM. This is the recommended path when a capable local Win11 box exists.

## Why this is simpler than the Azure VM (TASK-00)

Everything in TASK-00 that looks complicated exists only because an Azure VM is headless and provisioned via Run Command (SYSTEM, session 0). A machine you're already logged into interactively drops all of it:

| TASK-00 (Azure) needs… | Local Win11 box |
|---|---|
| Auto-logon (UIA needs interactive session) | already interactive — none |
| Scheduled task to start in user session | none |
| Remote Control + one-time `claude auth login` | none — your **local Claude Code already has direct access** |
| `az` provisioning, NSG, licensing attestation | none |
| Egress firewall workarounds | none — full network |
| Cost | none |

So this task is just: install the toolchain, check out Chromium at the pinned tag, install NVDA. Then the local Claude Code session runs the canonical `accessibility_unittests` + B-lite host + `ax_dump_events` + NVDA capture, and builds the four prototype patches (patches/). **No lean shims** (unlike the Linux container in results/ENVIRONMENT.md): a full local checkout has the DEPS-pinned `gn`, all source, and no disk/network constraints, so the build is canonical and zero-deviation — which also satisfies NEXT-QUESTIONS #11 (the zero-deviation confirmation run) for free.

## Prerequisites / disk

- **Disk is the binding constraint.** A `gclient` checkout is ~100–120 GB; checkout + build wants **~400–500 GB free**. Verify before starting. If the OS drive is tight, put `$SrcRoot` on the second NVMe (the MS-01 has slots) — set `-SrcRoot D:\src` when running the script.
- 64 GB RAM is comfortable; ~14C/20T gives a solid build (a full Chromium build is hours, but we build only the accessibility targets + host + ax_dump tools, which is far smaller than `chrome.exe`).
- No GPU needed (UIA/NVDA/ax_dump are not GPU-dependent; software rendering suffices even for the visual B-lite render).

## Setup

Run `scripts/setup-local-windows.ps1` from an **elevated PowerShell** (Admin — VS Build Tools needs it) the first time. It is idempotent (re-running skips installed pieces). It:

1. Checks free disk on the target drive (warns if < 400 GB).
2. Installs **Git for Windows** (via winget) and **VS Build Tools** (Desktop C++ workload + Windows 11 SDK + ATL/MFC) via the official bootstrapper.
3. Installs **depot_tools**, prepends it to PATH (must precede other python/git), sets `DEPOT_TOOLS_WIN_TOOLCHAIN=0` and `vs2022_install`.
4. `fetch --no-history chromium` → `git checkout 149.0.7827.115` → `gclient sync -D` into `$SrcRoot\chromium`.
5. Silently installs **NVDA** (per docs/TASK-00a).

After it finishes, open a fresh terminal (so PATH/env take effect) and let local Claude Code drive the build:

```bat
cd C:\src\chromium\src
gn gen out\rel --args="is_debug=false is_component_build=true dcheck_always_on=true"
ninja -C out\rel accessibility_unittests
out\rel\accessibility_unittests.exe --gtest_filter=AX*
```

Then the VM-queue items (NEXT-QUESTIONS #5–7) run here: register the preserved T2 + matrix tests (tests/linux-t2/ — already wired into `accessibility_unittests` by the BUILD patch), run them under `ax_dump_events`, drive the B-lite host, capture NVDA speech (docs/TASK-00a Strategy A or B), and build/measure the patches (patches/).

## What runs where

- **This local box:** the full Windows verification queue — UIA finalize (T2-1/2/3/5 under `ax_dump_events`), IME composition (T2-4), action round-trip (T2-6), cell↔text selection UIA ordering (T2-7), NVDA end-to-end announcements, and the four prototype patches.
- **The existing Linux work** (109 generated-event tests, blite spine) stays as-is; this box adds the platform/UIA half on top, against the same pinned tag.

## Notes

- The script does a `--no-history` fetch to save ~tens of GB; drop `-NoHistory` if you want full git history.
- If disk turns out too small even on the second NVMe, the fallback is the lean-subset build from results/ENVIRONMENT.md (tarball + selective extract) — but on a real local box with adequate disk, do the canonical checkout; it's cleaner and shim-free.
- NVDA needs the interactive desktop (it's not truly headless — docs/TASK-00a); on your own logged-in machine that's automatic.
