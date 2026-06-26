# tests/nvda-config — pinned NVDA config profile (committed for reproducibility)

This directory holds the **AccessibleWebEdit** NVDA configuration profile, committed
so every Windows build/test host reproduces the *identical* AT configuration (per
`docs/TASK-00a-nvda-verification.md`, "Project config profile").

## What it pins

`nvda.ini` selects the built-in **No speech** synthesizer (`[speech] synth = silence`)
and disables everything that would interfere with an unattended capture run:

- update checks (`general.checkForUpdatesOnStartup = False`, `update.autoCheck = False`)
- the welcome dialog (`showWelcomeDialogAtStartup = False`)
- the exit-confirm prompt and start/exit sounds (`askToExit`, `playStartAndExitSounds`)

The **No speech** synth is the key choice: speech still flows through NVDA's pipeline
and into the session log, so every utterance is captured as greppable text, but
nothing needs an audio device (ideal for a headless-style, no-audio host).

## How it maps to `C:\awe\nvda-config`

NVDA reads its config from the directory passed via `-c` / `--config-path`, expecting
`nvda.ini` at the root of that directory. The project's runners (and
`docs/TASK-00a`, `blite/blite_host_win.README.md`) use the fixed path
**`C:\awe\nvda-config`**. So this committed `nvda.ini` is deployed there once:

```powershell
# one-time deploy of the pinned profile to the path the runners expect
New-Item -ItemType Directory -Force -Path C:\awe\nvda-config | Out-Null
Copy-Item "C:\Dev\Projects\45 - AccWebEdit\tests\nvda-config\nvda.ini" `
          "C:\awe\nvda-config\nvda.ini" -Force
```

After that, NVDA launches against it:

```powershell
& "$env:ProgramFiles\nvda\nvda.exe" -m -c C:\awe\nvda-config `
    --log-level=12 --log-file C:\awe\logs\nvda-<label>.log
```

`scripts/_nvda-capture.ps1` assumes this deployment is already done (it checks for
`C:\awe\nvda-config` and fails with a pointer here if missing).

## Caveats

- The config schema and the `Speaking [...]` log format are NVDA **implementation
  details**, not a stable API — they can drift across NVDA versions
  (`docs/TASK-00a` open item #1). On the first real run, launch NVDA once
  interactively against this config, confirm the No-speech synth is selected and
  the keys above took effect, and pin/adjust to the installed version. If you
  edit settings live in NVDA's GUI, copy the regenerated `nvda.ini` back here so
  the committed profile stays the source of truth.
- This profile is intentionally minimal — only the keys that matter for an
  unattended No-speech capture are pinned; everything else uses NVDA defaults.
