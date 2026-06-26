# Visuals coupled to a11y — the single-layout thesis, proven on Windows through UIA

Date: 2026-06-26. This closes the Architecture-A deliverable from
`results/handoff-add-visuals.md`: the B-lite UIA host (`blite/blite_host_win.cc`)
now **paints** the editor and exposes the **same** geometry through UIA, from one
layout pass — so the painted caret/text and the caret/text a screen reader
announces cannot drift.

## The one rule, satisfied by construction
`LayOut(editor) -> Layout` (per-glyph rects + caret rect, client px) is a pure
function of the editor. BOTH consumers call it on the same editor state:
- **Pixels:** `WM_PAINT` draws each glyph at its `Layout` x and the caret at
  `Layout.caret` (GDI, a Consolas run whose bold/italic/underline come from the
  same flags exposed as UIA attributes).
- **A11y geometry:** `kCaretBounds` (on the field) is set from `Layout.caret`;
  `BliteNodeDelegate::GetBoundsRect` and `GetInnerTextRangeBoundsRect` return
  screen rects from the same metrics (client px -> `ClientToScreen`).

Because there is exactly one `LayOut`, there is no second layout to drift from.

## Verified through the native UIA client (the API NVDA uses)
Window painted "hello" (bold/italic/underline) with the caret at offset 5; client
origin on screen `138,161`, so the layout text-area `(12,16)` maps to screen
`150,177`, and the caret `(12+5*14)` to screen x `220`:

| What | Source | Value |
|---|---|---|
| Edit element bounds | `get_CurrentBoundingRectangle` | `[150,177 70x22]` |
| Caret rect | `ITextRangeProvider::GetBoundingRectangles` (degenerate selection) | `[220,177 1x22]` |
| Full-text range rect | `GetBoundingRectangles` after `Select()` | `[150,177 70x22]` |
| Painted text / caret | `WM_PAINT` from `LayOut` | text `[150,177 70x22]`, caret x `220` |

Every UIA-reported rect equals the painted location. Screenshot evidence:
`results/blite-rendered.png`. Probes: `scripts/uiaprobe.cpp` (element bounds),
`scripts/uiaprobe_select.cpp` (caret + range rects).

## Key mechanics found
- The element rect path needs `AXPlatformNodeDelegate::GetBoundsRect` (base
  returns empty). The text-range rect path needs `GetInnerTextRangeBoundsRect`,
  AND it must set `*offscreen_result = kOnscreen` or `AXRange::GetRects` discards
  the rect (ax_range.h:668). A degenerate caret must return **width 0** — AXRange
  widens it to a 1px caret bar itself (and DCHECKs width==0 first).

## Scope / next (Phase-2 refinements, per the handoff)
- Single-line monospace layout (matches the flat field). Multi-line/block layout
  with per-line selection rects (UIA returns one rect per line) is the next step.
- Coordinates assume 100% DPI (physical px == client px). A DPI-scale map at the
  provider boundary is a refinement.
- Skia/DirectWrite fidelity swap keeps this `LayOut` contract (docs/14 Stage 2).
