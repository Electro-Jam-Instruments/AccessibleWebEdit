# 00 — Index

AccessibleWebEdit: proving a custom (non-Blink) canvas editor can feed Chromium's `ui/accessibility` layer directly and get full editing semantics through a real screen reader, bypassing ARIA. Build path is **B-lite**: a standalone host using `ui/accessibility` + `AXPlatformNodeDelegate`, pinned to Chrome 149 stable (tag 149.0.7827.115).

This index orients a new reader (or a fresh session). Start here.

## Where the project stands (2026-06-15)

- **Foundation event-layer-validated on Linux:** the generated-event engine that drives all editing semantics is tested — **109 passing tests** (`tests/linux-t2/`), plus a runnable B-lite spine (`blite/`). This proves the *generation layer*, not screen-reader-observable fidelity (UIA translation + NVDA are VM-gated, not yet run).
- **Options analysis (strong, not yet falsified):** native is the path that *can* deliver full editing fidelity; the web path is **capped today** by the mirror-DOM approach (AOM virtual nodes are dead, though HTML-in-Canvas `drawElement` is an active web effort that could lift the cap). The "native is superior" claim is argued from source + the design, **not yet measured end-to-end through a screen reader** — see doc 13 and results/expert-reviews/AGGREGATE.md.
- **Critical path = the Windows VM (TASK-00):** everything screen-reader-observable (UIA finalize, IME, actions, NVDA) is written and queued but blocked on VM provisioning (your Azure step).

## Docs

| Doc | What it is |
|---|---|
| **03-bridge-contract.md** | The producer → `ui/accessibility` API contract. Every clause tagged PROVEN/SOURCE/VM. The spec to *build* real editing against. |
| **09-child-tree-editing-research.md** | Tier-1 source findings: can a non-Blink producer carry full editing semantics? (Yes.) Defines the T2 task list. |
| **10-editing-scenario-matrix.md** | Every editor interaction (text/tables/comments/collaboration/embedded) graded SUPPORTED / BROWSER-WORK / OPEN with file:line citations; 15 rows verified by test. |
| **11-cross-platform-rendering.md** | What it takes to build the sighted-facing rendering UI across Win/Mac/Linux/mobile. ~80% shared core + thin per-platform shell; IME is the hard part. |
| **12-ime-support.md** | Why IME is mandatory for a custom surface, the per-platform contract, and the accessibility tie-in. |
| **13-chrome-api-and-aom-fit.md** | How the web APIs (AOM, EditContext, ariaNotify, HTML-in-Canvas) relate to the native approach. Native vs web vs standards-play. |
| **14-drawn-demo.md** | The staged plan to make B-lite *visual* — the custom drawn edit surface. Stage 0 (lean layout→pixels+a11y, one source of truth) is DONE and runnable on Linux; Stages 1–4 lead to a Windows/Skia/NVDA "see it and hear it from one layout" demo. |
| **15-rich-editor-engines-fit.md** | Can a CKEditor 5 / Lexical–style editor use the producer API? Their model+delta layers fit the contract; the API is native-only (web is sandboxed but doesn't need it). What must still be built for a native shell, plus a no-VM "bring-your-own-model" headless-Lexical spike. |
| **16-ckeditor-integration.md** | CKEditor-5 track (web path ruled out): ideas for reusing CK5's Model engine as the producer — Differ vs operations feed, engine-only headless, schema-derived role map, markers→annotations, the layout gap, an editor-agnostic JSON delta schema, and the licensing gate. |
| **17-rendering-architectures.md** | How editor data reaches UIA. The fork: **A** own-canvas (one layout pass; headless editor) vs **B** web-render offscreen + host-owned UIA from the model + DOM geometry read-back vs **C** plain web tab (ruled out — browser owns UIA). Why "headless" applies only to A; recommendation + the make-or-break risk for B. |
| **TASK-00-vm-bootstrap.md** | Provision + bootstrap the Windows 11 VM (cloud path). |
| **TASK-00b-local-windows-setup.md** | If you have a capable local Win11 box (e.g. Minisforum MS-01): skip Azure, build/test locally. Simpler + free. Recommended when available. |
| **TASK-00a-nvda-verification.md** | NVDA (not Narrator) as the AT verification layer, via speech logs. |

## Results & evidence

| Path | What it is |
|---|---|
| results/T2-linux-generated-events.md | T2-1/2/3/5 event logs, all pass. |
| results/matrix-linux-generated-events.md | docs/10 rows verified by test (T2-7 gen half, comments, live regions, tables, etc.). |
| results/ENVIRONMENT.md | Full build provenance + the local-modifications ledger (how the pinned source was built lean). |
| results/NEXT-QUESTIONS.md | The consolidated decision/verification queue for the planning session + VM. |
| results/blite-host-run.txt | Captured run of the B-lite spine. |
| results/demo-e2e-run.txt | Captured run of the end-to-end host (`demo/`): real Chromium generated events + `kCaretBounds` per editing step. |
| results/expert-reviews/AGGREGATE.md | Three independent expert reviews + rated aggregate. |
| results/expert-reviews/REVIEW-BACKLOG.md | Status-tagged tracker: fixes DONE this session + the complete open list (VM-gated / decisions / spikes). |
| results/PUBLICATION-READINESS.md | Open-source readiness audit (secrets/PII/license/attribution) + the decisions still yours before going public. |

## Code & patches

| Path | What it is |
|---|---|
| blite/ | The runnable B-lite lean spine (surface → bridge → AXTree → AXEventGenerator) + README. |
| blite/draw/ | The drawn demo (doc 14, Stage 0): one layout pass → real rendered pixels (`sample.png`) **and** the matching a11y geometry. Standalone, builds with `g++`. |
| demo/ | The cross-platform **end-to-end** demo (doc 14, Stages 1/1b): shared core → Linux frames (`demo.gif`) + **real** Chromium `AXTree`/`AXEventGenerator` events (`results/demo-e2e-run.txt`) + an interactive Win32 window. UIA→NVDA staged for Win11. See `demo/README.md`. |
| tests/linux-t2/ | The T2 + matrix generated-event tests, the BUILD patch, and args — reproducible against the pinned tag (and registered in the canonical `accessibility_unittests` for the VM). |
| patches/ | Four BROWSER-WORK prototype patches (SelectionPattern2, ITextProvider2/RangeFromAnnotation, TEXT_ATTRIBUTE_CHANGED UIA event, annotation author/datetime) — the standards exhibits. Build on the VM. |

## The one decision that unblocks everything

Provision the TASK-00 Windows VM (and confirm the Win11 Azure licensing attestation). Once it exists, the queued work runs without further design: rebuild the tests under the canonical target, run them under `ax_dump_events` + NVDA, do T2-4/6/7, and build/measure the four prototype patches.
