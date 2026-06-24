# CLAUDE.md — orientation for Claude Code working in this repo

You are working in **AccessibleWebEdit**. Read `docs/00-INDEX.md` first — it is the full map. This file is the short startup brief.

## What this project is
Research + prototypes testing whether a custom (non-Blink) canvas editor can feed Chromium's `ui/accessibility` layer directly and get full editing semantics through a real screen reader, bypassing ARIA/the DOM mirror. Pinned to **Chromium tag `149.0.7827.115`** (Chromium is NOT vendored here — you supply a checkout).

## Status (so you know what's done vs. to do)
- **Done / event-layer validated:** 109 generated-event tests (`tests/linux-t2/`) + a runnable producer spine (`blite/`), all passing on Linux against the pinned tag.
- **Not done — the point of running here:** everything screen-reader-observable (UIA event finalization, IME composition, AT actions, and what **NVDA** actually announces) is Windows-only and has **not run yet**. Claims are tagged PROVEN / SOURCE / VM throughout; "VM" = pending this Windows verification.
- The complete, prioritized work queue is `results/expert-reviews/REVIEW-BACKLOG.md` (start with item **V1**: the smallest end-to-end insert+caret → UIA → NVDA slice).

## Where to start on a Windows 11 machine
1. **Setup (once):** follow `docs/TASK-00b-local-windows-setup.md` — run `scripts/setup-local-windows.ps1` from an **elevated PowerShell** (Admin). It installs Git + VS Build Tools (C++ + Win11 SDK), depot_tools, fetches/syncs Chromium at the pinned tag, and installs NVDA. Needs ~500 GB free disk.
2. **Build + run the existing tests** (confirms the baseline reproduces canonically):
   - `cd <SrcRoot>\chromium\src`
   - drop `tests/linux-t2/*.cc` + `*.gn` into `//ui/accessibility/`, apply `tests/linux-t2/ui-accessibility-BUILD.gn.patch`
   - `gn gen out\rel --args="is_debug=false is_component_build=true dcheck_always_on=true"`
   - `ninja -C out\rel accessibility_unittests && out\rel\accessibility_unittests.exe --gtest_filter=AX*`
3. **Then the Windows verification queue:** `ax_dump_events`, the B-lite host, and NVDA speech capture (`docs/TASK-00a-nvda-verification.md`), plus building/measuring the four prototypes in `patches/`.

## Conventions
- Claims must stay tagged by evidence level (PROVEN/SOURCE/VM). Do not upgrade a claim to PROVEN without an actual passing test or run; "fidelity" is not proven until the NVDA end-to-end slice runs.
- The bridge contract (`docs/03`) is the spec; the scenario matrix (`docs/10`) is graded with file:line citations. Keep both honest if you change findings.
- Chromium-derived files carry the Chromium BSD header by design (see `NOTICE.md`); the repo is BSD-3-Clause (`LICENSE`).
