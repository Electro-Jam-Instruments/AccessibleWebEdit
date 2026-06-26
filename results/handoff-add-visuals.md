# Handoff — adding the visuals to the a11y work

Date: 2026-06-26. For the agent working the current a11y / producer side. This is
the "make it visible" update set: how to add the rendered text (pixels) so it is
**coupled to** the accessibility geometry you're already producing, plus the
architecture decisions that constrain how. Read alongside new docs **15, 16, 17**
(and the existing **11, 14**).

## The one non-negotiable

**Pixels and a11y geometry must come from the *same* layout pass.** The caret/
selection/character rectangles a screen reader announces (`kCaretBounds`, range
rects) must be the exact coordinates painted. This is the docs/11/14 thesis and
the whole point of the project — do not build a second layout for the renderer.

## What already exists (reuse it, don't reinvent)

The `demo/` tree already couples both consumers off one pass for a flat text
field — use it as the template:

- `demo/core/layout_engine.h` — `LayOut(doc) -> Layout` (glyph rects, caret rect,
  selection rect). **The single source of truth.**
- `demo/core/raster.h` + `demo/platform/linux_headless/demo_render.cc` — paints
  the `Layout` to PPM pixels (the visual half), Linux, plain `g++`.
- `demo/platform/linux_headless/demo_e2e.cc` — feeds the *same* `Layout` into a
  real `AXTree` + `AXEventGenerator`, writing `kCaretBounds` from it (the a11y
  half). Captured run: `results/demo-e2e-run.txt`.

Run the visual half right now to see the coupling:
```sh
cd demo/platform/linux_headless
g++ -std=c++17 -O2 -I../../core demo_render.cc -o /tmp/demo_render
/tmp/demo_render /tmp/frames
python3 ../../tools/make_media.py /tmp/frames .   # -> demo.gif + contact_sheet.png
```

## The architecture decision that tells you HOW to add visuals

See docs/17. Two viable architectures; they add visuals differently. **Confirm
which one this a11y work is targeting before wiring pixels.**

- **A — own canvas (docs/14 path).** We render the text ourselves from the model
  via our layout engine. Visuals = extend the demo's renderer. Geometry for a11y
  comes from our `LayOut()`. *This is the path the demo already embodies.*
- **B — web-render + native UIA (docs/17).** The web engine renders the editor's
  contentEditable **offscreen**; the host composites those pixels AND owns the
  UIA tree from the model. Here the "visuals" are the composited browser surface,
  and a11y **geometry is read back from the DOM** (`Range.getClientRects()`), not
  from our `LayOut()`. **Verified clean (2026-06-26):** in offscreen (CEF OSR) /
  composition (WebView2) hosting the embedded engine exposes no AT-reachable a11y
  tree (no HWND root), so the host owns UIA outright. **Hard rule: offscreen/
  composition hosting, never windowed.** See docs/17.

**Headless note:** "run the editor model headless (no contentEditable)" applies
**only to A** (we render, so we don't want the editor's DOM). In B the editor
renders its DOM normally — headless gains nothing. Don't conflate them.

## Concrete work to add visuals — Architecture A (Linux, now, no VM)

1. **Grow the layout from flat to block.** The demo's `LayOut()` is single-line
   monospace. To show a CK5/Lexical-style document you need **multi-line / block**
   layout (paragraphs, headings, list items, wrapping). Keep the *contract*
   identical: `LayOut(model) -> Layout` where `Layout` carries per-line glyph
   rects, caret rect, and selection rects. Keep it CPU/std-only so it runs on
   Linux; Skia/DirectWrite is the Phase-2 fidelity swap (docs/14 Stage 2), not
   needed to prove the coupling.
2. **Drive both consumers from that one `Layout`.** Paint it (raster) AND write
   `kCaretBounds` + selection from it into the AXTree — exactly as
   `demo_render.cc` and `demo_e2e.cc` already do, just for the richer `Layout`.
3. **Assert they can't drift.** Add a check that the painted caret rect == the
   `kCaretBounds` written to the AXNode (the demo does this for the flat case).
4. **Model feed (when integrating a real editor):** consume the **editor-agnostic
   JSON delta** (docs/16 Idea 6) — `{node add/remove/update, text, selection,
   markers}` — to build the document, then `LayOut()`. Same schema for CK5 and
   Lexical so this stays editor-neutral.

## Geometry obligations the visuals must honor (so they match UIA)

Whatever you paint must line up with what UIA reports (docs/03 §2; UIA Text
pattern verified separately):

- **Caret:** a caret rect at the focus offset → `kCaretBounds`.
- **Selection:** UIA `GetBoundingRectangles` returns **one rect per line** for a
  multi-line range — so a wrapped selection is *several* rects, not one union.
  Paint per-line selection rects and expose the same set (the current flat demo's
  single union rect is a Phase-1 simplification to fix when you go multi-line).
- **Per-character/line bounds:** keep per-glyph rects in `Layout` so word/line
  navigation and range rectangles can be derived.
- Coordinates: layout px in the demo; for real UIA they must map to **screen**
  coordinates at the provider boundary.

## Pointers

- docs/14 — the staged Stage 0→4 plan (this is Stage 1→2 work).
- docs/16 — CKEditor track; Idea 5 (our layout is the source of truth), Idea 6
  (the JSON delta schema you'll consume).
- docs/17 — A vs B vs C; read before choosing how geometry is sourced.
- docs/03 §2 — selection/caret contract; §1 — node schema (roles).

## Open items / waiting on

- **Architecture B is verified clean** (docs/17) — offscreen/composition hosting,
  host owns UIA, never windowed. Two residual `[VM]` checks need a running NVDA
  (no competing child HWND; hit-test resolves to host provider). If on B, geometry
  comes from DOM `getClientRects` via the engine's `ExecuteScript`, not `LayOut()`.
- **Lexical track doc (18)** — not yet written; the JSON-delta spike (Lexical
  first, headless turnkey) proves the model→layout→{pixels, a11y} swap.
