# AccessibleWebEdit

Can a custom (non-Blink) canvas editor feed Chromium's `ui/accessibility` layer **directly** and get full editing semantics — caret, selection, autocorrect, IME composition, tables, comments — through a real screen reader, **bypassing ARIA and the DOM mirror**? This repository is the research, evidence, and prototypes investigating that question.

> **Status: research prototype — the core thesis is PROVEN end to end, on real hardware.** On **2026-06-27** NVDA announced our custom, non-Blink editable field — `Speaking [..., 'edit', ..., 'hello']`, role *edit* with content *hello* and no name, so it is unmistakably our surface and not a Chrome control. The full chain ran on a real Windows 11 machine: custom editor model → `Bridge` → `AXTree` → `AXPlatformNodeWin` → UIA → **NVDA speech**. The 109 generated-event tests also pass **on Windows** against the pinned Chromium tag (`[109/109] SUCCESS`), not only on Linux. See **`results/V1-end-to-end.md`** for the run, the two fixes that got it there, and what is still open.
>
> **What is not yet proven:** announcing *live* edits and caret moves as they happen (TextChanged / caret read-back), word and line navigation read-back, braille, IME composition, and the richer scenarios in `docs/10`. Claims throughout are tagged **PROVEN** (observed in a run or a passing test) / **SOURCE** (source reading) / **VM** (pending verification). See `results/expert-reviews/` for an adversarial review of exactly what is and is not proven. This is a research prototype, not a shipping product.

## Start here

**`docs/00-INDEX.md`** is the orientation map — read it first. The short version:

- **The thesis (`docs/09`):** Chromium's `AXTree → AXEventGenerator → AXPlatformNode` pipeline is producer-agnostic (web content, Views UI, and PDF all feed it). A non-Blink canvas editor can be the same kind of citizen, posting `AXTreeUpdate` deltas and receiving actions.
- **The contract (`docs/03`):** the concrete obligations a producer must satisfy, each tagged by evidence level.
- **The matrix (`docs/10`):** every editor interaction (text / tables / comments / collaboration / embedded) graded SUPPORTED / BROWSER-WORK / OPEN with file:line citations.
- **Cross-platform & web context (`docs/11`–`13`):** what a cross-platform rendering build takes, why IME is the hard part, and how the web APIs (AOM, EditContext, ariaNotify, HTML-in-Canvas) relate to the native approach.

## What's proven, and how to reproduce it

The generated-event layer is verified by 109 unit tests built against **Chromium tag `149.0.7827.115`** (`tests/linux-t2/`), plus a runnable producer spine (`blite/`). These exercise `AXEventGenerator` directly: synthetic `AXTreeUpdate` deltas in, the resulting accessibility events asserted out — no Blink, no intents required.

To reproduce, you need a Chromium `149.0.7827.115` checkout:
- **Have a Windows 11 box?** `docs/TASK-00b-local-windows-setup.md` + `scripts/setup-local-windows.ps1` — canonical build, recommended.
- **Cloud VM?** `docs/TASK-00-vm-bootstrap.md` + `scripts/provision-vm.sh`.
- The Linux-container provenance (a lean, partial-extraction build) is fully documented in `results/ENVIRONMENT.md`.

Then drop `tests/linux-t2/` into `//ui/accessibility`, apply `tests/linux-t2/ui-accessibility-BUILD.gn.patch`, and `ninja … accessibility_unittests`.

## What's not done yet

The screen-reader-observable half — UIA event finalization, IME composition end to end, AT-initiated actions, and what **NVDA** actually announces — is written up and queued but **runs only on Windows** (`docs/TASK-00a` for the NVDA verification approach). The complete, status-tagged open list is `results/expert-reviews/REVIEW-BACKLOG.md`.

## Repository layout

| Path | Contents |
|---|---|
| `docs/` | The plan, the bridge contract, the scenario matrix, cross-platform/web analysis, and the Windows setup tasks. `00-INDEX.md` is the map. |
| `tests/linux-t2/` | Generated-event unit tests + a BUILD-registration patch, against the pinned tag. |
| `blite/` | The runnable "B-lite" producer spine (surface → bridge → AXTree → AXEventGenerator). |
| `patches/` | Four prototype patches for `ui/accessibility` gaps the matrix surfaced (the "platform API outruns the browser" exhibits) — Windows-built. |
| `results/` | Test event logs, build provenance, open questions, and the expert reviews. |
| `scripts/` | Windows local-setup and Azure VM provisioning scripts. |

## License & attribution

BSD 3-Clause (`LICENSE`), chosen for consistency with Chromium, from which the tests, the spine, and the patch prototypes derive. Chromium is **not** vendored here — the patches and tests reference it by tag and file:line and you supply your own checkout. See `NOTICE.md` for the full attribution and which files are Chromium-derived.

This repository was produced with substantial AI assistance as a research effort; treat it as evidence and design analysis, not production code.
