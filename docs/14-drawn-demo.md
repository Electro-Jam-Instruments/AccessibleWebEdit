# 14 — The Drawn Demo: from headless spine to a visible canvas

Date: 2026-06-25. The concrete, staged milestone for making B-lite *visual* —
the "custom drawn edit surface" track. Companion to `docs/11` (the
cross-platform rendering analysis this operationalizes) and `blite/draw/` (the
lean first stage, already runnable).

## The thesis being made visible

Doc 11's non-negotiable constraint is that **one layout engine is the single
source of truth feeding both the painter and the AX tree** — so the caret/
word/line geometry a screen reader announces is the exact pixels painted. The
drawn demo exists to make that constraint *executable* and then *observable*: a
keystroke moves a caret that you can both **see** (pixels) and **hear** (NVDA),
driven from the same layout pass.

## Stage 0 — DONE (lean, Linux, no dependencies)

`blite/draw/` — a standalone C++17 program (own 5x7 font + CPU raster) that runs
the single layout pass and drives the resulting `Layout` into BOTH a real image
(PPM/PNG) AND the `kCaretBounds`/selection/per-character geometry that maps to
`AXNodeData`. Verified: builds with plain `g++`, renders "Hello, World!" with a
live selection + caret, prints AX geometry that is *identical* to the painted
coordinates (`results/blite-draw-run.txt`). This proves the coupling end to end
without Skia, a window, or the Chromium tree.

What Stage 0 deliberately fakes: real glyph shaping/fonts, a GPU surface, a
window/event loop, IME, and the *live* AX tree (it prints the geometry rather
than feeding a real `AXTree`). Those are the next stages.

## Stage 1 — DONE: fuse the drawn layout into the real AX tree (Linux, in-tree)

The accessibility half is now real, built and run on Linux: `demo/` (the
cross-platform end-to-end). `demo/platform/linux_headless/demo_e2e.cc` feeds the
shared core (`demo/core/`) into a live `AXTree` + `AXEventGenerator` and emits
the **real** generated event set per editing step, with `kCaretBounds` written
on the field node from the same `Layout` the renderer paints. Captured run:
`results/demo-e2e-run.txt`. The event sets are semantically correct per step —
a pure selection or caret move emits selection/`caretBoundsChanged` only (no
spurious text-changed), and text edits emit the full
`editableTextChanged`/`valueInTextFieldChanged`/`nameChanged` set. Same lean
build cone as `blite_host` (`//ui/accessibility:accessibility_internal` +
`//base`), wired via the `//ui/accessibility/t2:t2` GN root group.

## Stage 2 — real pixels (Windows-first, the doc-11 milestone)

Swap the lean renderer for the production stack, Windows first (it doubles as
the UIA proof platform):

- **Window + GPU surface:** Win32 + ANGLE/D3D (a single top-level window with a
  paint loop).
- **Text shaping + fonts:** HarfBuzz shaping + Skia `SkParagraph`/`SkShaper`,
  DirectWrite font enumeration. Replaces `font5x7.h` + the monospace metrics in
  `layout_engine.h`; the `LayOut() -> Layout` *contract* stays.
- **Skia paint:** glyph runs + caret + selection to the GPU surface instead of
  the CPU `Image` in `blite_draw.cc`.

The `DocModel -> LayOut() -> {paint, AX geometry}` shape is unchanged; only the
two consumers are upgraded.

## Stage 3 — input + IME (the genuinely hard part, per doc 11/12)

Hook real text input so the demo is *interactive*, not a static frame:

- **Windows:** TSF (`ui/base/ime` if building strategy (A), else a thin TSF
  client). Commit + composition.
- The composition path is where accessibility (`T2-4`) and rendering meet — a
  composition updates the layout, which updates both the painted text and the
  AX tree in lockstep. This is the milestone that actually exercises the
  single-source design under live IME.

## Stage 4 — close the loop with NVDA

Run Stage 2/3 under NVDA (`docs/TASK-00a`): type a character, confirm NVDA
speaks the inserted text + caret movement that the pixels show. This is the
first time "see it and hear it from one layout" is true end to end — and the
point at which a fidelity claim can move from VM to PROVEN.

## Build-strategy note

Stages 2–4 inherit the `docs/11` strategy decision (A) in-Chromium-tree vs (B)
standalone. (A) is recommended because it brings Skia + `ui/base/ime` + the live
`AXPlatformNode`/UIA provider already integrated — exactly the pieces Stages 2–3
otherwise rebuild. The Stage 0 demo is intentionally strategy-agnostic: its
shared `layout_engine.h` is the seam that survives either choice.

## Status summary

| Stage | What | State |
|---|---|---|
| 0 | Lean layout → pixels + printed AX geometry | **DONE** (`blite/draw/`, runs on Linux) |
| 1 | Same layout → real generated events + `kCaretBounds` in a live AXTree | **DONE** (`demo/`, `results/demo-e2e-run.txt`) |
| 1b | Interactive Win32 window + keyboard, same core (standalone, no Chromium) | **DONE** (`demo/platform/win32/win_main.cc`, builds on Win11) |
| 2 | Skia + DirectWrite for production text (real shaping/fonts) | Windows VM |
| 3 | TSF input + IME composition | Windows VM (hard part) |
| 4 | UIA (`AXPlatformNodeWin`) + NVDA: see it + hear it from one layout | Windows VM — `demo/platform/win32/uia_bridge.md`, proves fidelity |

The cross-platform end-to-end demo lives in `demo/` (its own README has the
architecture diagram and per-OS run steps). `blite/draw/` remains the minimal
Stage-0 seed.
