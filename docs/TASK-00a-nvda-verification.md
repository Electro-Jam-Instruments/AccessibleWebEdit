# TASK-00a — NVDA as the AT Verification Layer (replaces Narrator)

Status: researched 2026-06-12 against the official NVDA user guide (source: github.com/nvaccess/nvda, user_docs/en/userGuide.md — nvaccess.org blocks automated fetches, but the guide in the repo is the authoritative source the site renders). Decision from planning: Narrator will NOT be used; NVDA is the screen reader for all AT-level verification on the TASK-00 VM.

## Why NVDA is actually the better instrument for this project

1. **Machine-readable speech.** NVDA writes a session log that, at input/output level, records every utterance it speaks and every input gesture it receives. That turns "what would a screen reader user hear?" into a greppable text file — no audio, no human listener, fully automatable by the Claude Code session driving the VM. Narrator has no equivalent accessible log.
2. **No audio device required.** NVDA's synthesizer list includes a built-in **"No speech"** option ("allows you to use NVDA with no speech output whatsoever" — user guide, Speech settings). Speech still flows through the pipeline and into the log; nothing needs a sound card. Ideal for an unattended Azure VM.
3. **Speech Viewer** (NVDA menu → Tools → Speech Viewer) shows live utterances in a window, with a "Show speech viewer on startup" persistence checkbox — useful during interactive RDP debugging.
4. NVDA is the most-used screen reader for Windows browser testing and exercises UIA hard; its behavior is what the standards argument will be judged against.

## Installation (verified switches)

The downloaded NVDA launcher executable accepts NVDA's documented command line options. Unattended install:

```powershell
# Download: nvaccess publishes stable builds at
#   https://www.nvaccess.org/download/  ->  https://download.nvaccess.org/releases/stable/nvda_<version>.exe
# The same installer exes are attached to GitHub releases (github.com/nvaccess/nvda/releases),
# which is the more scriptable source:
$rel = Invoke-RestMethod "https://api.github.com/repos/nvaccess/nvda/releases/latest"
$asset = $rel.assets | Where-Object { $_.name -match '^nvda_.*\.exe$' } | Select-Object -First 1
Invoke-WebRequest $asset.browser_download_url -OutFile "$env:TEMP\nvda_installer.exe"

# Verified switches (user guide, "Command line options"):
#   --install-silent                  Silently installs NVDA (does not start the newly installed copy)
#   --enable-start-on-logon=False     Do not start NVDA on the Windows sign-in screen
& "$env:TEMP\nvda_installer.exe" --install-silent --enable-start-on-logon=False
# Installs to %ProgramFiles%\nvda ; user config lives in %APPDATA%\nvda
# Record the installed version in results (Help > About, or the log header).
```

Silent uninstall, if ever needed: `"%ProgramFiles%\nvda\uninstall.exe" /S`.

## Logging — the end-to-end verification channel (verified)

- Default log: **`%TEMP%\nvda.log`**, recreated each NVDA start; the previous session is rotated to `%TEMP%\nvda-old.log`.
- Both path and verbosity are controllable per run:

```powershell
& "$env:ProgramFiles\nvda\nvda.exe" `
    -m `                                  # --minimal: no sounds, no interface, no start message
    -c C:\awe\nvda-config `               # --config-path: project-pinned config profile
    --log-level=12 `                      # input/output level: logs speech + input gestures
    --log-file=C:\awe\logs\nvda-run.log
```

- Verified log levels: `5` debug **unredacted**, `10` debug, `12` input/output, `15` debug warning, `20` info, `100` disabled. `--debug-logging` forces debug for one run and overrides any other level flag.
- For test runs use **12** (speech utterances + gestures — the announcement record). Use **5** when typed-character echo must be visible verbatim: higher levels redact typed text for privacy; this is a throwaway research VM, so unredacted is acceptable and should be noted per run.
- **How the agent connects to it:** the Claude Code session on the VM (Remote Control, per TASK-00) reads the file directly — `Get-Content C:\awe\logs\nvda-run.log -Wait -Tail 50` for live tailing during a scenario, or post-run grep for `Speaking` lines to extract the utterance sequence. Each T2 scenario's utterance log gets committed under `results/` exactly like the Linux event logs.

## Project config profile (one-time, then pinned)

Create `C:\awe\nvda-config` once by launching NVDA with `-c C:\awe\nvda-config`, then in settings: Speech → synthesizer = **No speech**; Tools → Speech Viewer on (optional, RDP debugging); General → no update checks. The resulting config directory is plain files — commit a copy to the repo (`tests/nvda-config/`) so every VM rebuild reproduces the identical AT configuration.

## End-to-end loop on the VM

```
B-lite host (canvas tree producer)
        │  AXTreeUpdate deltas → ui/accessibility → UIA provider
        ▼
NVDA (UIA client; focus + browse/focus-mode handling)
        │  utterances
        ▼
C:\awe\logs\nvda-run.log   ←── read/asserted by the Claude session, committed to results/
```

Scenario driving: focus the host window and inject input via PowerShell SendKeys / `keybd_event` (caret moves, typing) or via UIA client actions (ITextRangeProvider::Select etc., exercising T2-6's action channel); after each step the log yields what an NVDA user would have heard. `ax_dump_events` runs alongside for the Chromium-side event record — log + dump together give the full causal chain: tree delta → generated event → UIA event → announcement.

## Headless / dev-mode operation (verified 2026-06-16, nvaccess/nvda repo)

**Important caveat: NVDA is not *truly* headless.** It is a GUI screen reader and requires an interactive desktop session — it cannot run in Windows session 0 or with no desktop. The TASK-00 auto-logon (interactive user session) is exactly what makes NVDA usable; "headless" in this project means **no audio, no human watcher, output captured as data** — not "no desktop." Do not expect NVDA to run as a pure background service.

Two ways to capture what NVDA would speak, in increasing robustness:

### Strategy A — Log-driven (default; lowest setup, what the rest of this doc describes)
Install the release binary, run `nvda.exe -m -c <config> --log-level=12 --log-file=<path>` with the **No speech** synth, and parse the log for spoken output. Pros: trivial setup, uses the shipping build. Cons: the `Speaking [...]` log format is an implementation detail (Open item #1) — regex-fragile across NVDA versions. Use this for the first VM session and the initial T2 announcement captures.

### Strategy B — SystemTestSpy + Robot Framework (robust, programmatic; NVDA's own mechanism)
NVDA's source tree (`tests/system/`, run via `runsystemtests.bat`, Robot Framework) drives a **`SystemTestSpy`** global plugin that captures NVDA's spoken/braille output as **structured data**, asserted directly in tests — no log-parsing. This runs against a **source copy of NVDA** (developer build: clone nvaccess/nvda, `scons`, `runnvda.bat`), not the installed binary. Pros: version-stable, CI-grade, captures the exact speech sequence as objects; this is how NVDA verifies its own announcements. Cons: requires building NVDA from source (Python + scons) on the VM — heavier than Strategy A. Graduate to this if/when we want a durable automated regression suite for the editing scenarios, or if Strategy A's log-parsing proves fragile.

### Driving NVDA (not capturing): Controller Client API
`nvdaControllerClient.dll` (API v2.0 in NVDA 2024.1: `nvdaController_speakText`, `speakSsml`, `getProcessId`) lets an external program *make NVDA speak* arbitrary text/braille. This is the inverse of what we need (we capture NVDA's UIA-derived announcements, not feed it text), so it is **not** our verification path — but it is handy as a one-call sanity check that the speech pipeline + synth are alive on the VM before a run.

### Recommendation
First VM session: **Strategy A** (binary + log, fastest to first result). If the editing-scenario captures become a maintained regression suite, move to **Strategy B** (source build + SystemTestSpy) for stability. Both rely on the interactive desktop session from TASK-00; neither needs audio (No speech synth) or a GPU.

## What changes in the existing plan

- Every "Narrator-observable" phrase in docs/09, results/ENVIRONMENT.md, and NEXT-QUESTIONS now reads as "NVDA-observable via speech log". The verification is *stronger*, not weaker: logged utterances are diffable evidence; Narrator listening sessions were never going to be.
- TASK-00's Phase 2 bootstrap gains the NVDA silent-install block (added in this commit).
- SelectionPattern2 note: NVDA does not depend on ISelectionProvider2 the way Narrator does for N-of-M summaries — when we prototype that patch, the consumer-side check should use NVDA's actual selection announcements plus a raw UIA client probe, and the standards exhibit should cite Narrator's documented dependence rather than local observation.

## Open items for the first VM session

1. Confirm the exact `Speaking` line format of the installed NVDA version at level 12 (it is an implementation detail, not API) and pin a parse regex in the test driver — or skip log-parsing entirely by using Strategy B (SystemTestSpy) if a durable suite is wanted.
2. Decide focus-mode vs browse-mode handling for the B-lite host window (NVDA treats Win32 apps in focus mode by default — likely the right behavior for an editor; verify).
3. Check NVDA's UIA event coalescing under rapid edits (relates to T2-5/T2-7 timing questions).
