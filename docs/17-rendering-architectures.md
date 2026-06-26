# 17 — Rendering Architectures: how to bring editor data to UIA

Date: 2026-06-26. Captures the architecture decision reached while scoping the
CKEditor 5 / Lexical tracks (docs/15/16): **given that we keep a rich web editor's
text rendering, how does the *correct* editing semantics reach a screen reader via
UIA — and where does "headless" actually apply?**

This resolves a confusion worth recording: "reuse the editor model" does **not**
imply "throw away the editor's rendering," and "headless" is only meaningful in
one of the architectures below.

Producer/UIA facts keep this repo's PROVEN/SOURCE/VM tags. Windows-embedding
mechanics for architecture B are tagged **[SOURCE-PENDING]** until the in-flight
primary-source verification lands (CEF/WebView2 offscreen rendering + UIA
ownership + suppressing the embedded engine's own a11y).

## The constraint that forces everything: who owns the UIA connection

UIA is owned at the **process/native layer**, not by page JavaScript.

- In a **browser tab**, the browser builds the UIA tree from your DOM (Blink
  `AXObject` → `AXPlatformNodeWin` → UIA). Page JS has **no API to inject its own
  model-driven UIA** — that was AOM virtual nodes, which are dead (docs/13). So
  "bring the model's data to UIA" from inside web content is **impossible today**;
  you get only what the browser computes from the contentEditable (the capped
  path, ruled out 2026-06-26).
- Therefore **custom, model-driven UIA requires a process where *you* own the UIA
  provider** — i.e. a native shell. This is the platform reason the project is
  native; it is not a preference. **[SOURCE]** (docs/13)

Once you own the UIA provider, a second question opens — *who renders the text?* —
and that is the real architecture fork.

## The three architectures

| | Renders the text | Owns UIA | Geometry source | Must build | Coupling to thesis | "Headless"? |
|---|---|---|---|---|---|---|
| **A. Own canvas** (docs/14) | we do (Skia/our raster) | us | our `LayOut()` pass | **a text layout engine** | one pass → pixels **and** a11y (purest) | **yes** — editor runs model-only, no DOM |
| **B. Web-render + native UIA** | the web engine, **offscreen** | us | DOM read-back (`getClientRects`) | bridge + geometry scrape | two sources bridged (looser) | **no** — editor renders its DOM normally |
| **C. Plain web tab** | the web engine | the **browser** | DOM (browser-mapped) | nothing | — | ruled out: can't inject UIA |

**This is the key clarification:** *headless belongs only to A.* In A we discard
the editor's contentEditable and paint ourselves, so we want the model with no DOM
("headless editor"). In B the editor renders its DOM as usual — running it
headless would gain nothing. C is off the table (the UIA-ownership constraint
above).

## Architecture A — own canvas (the docs/14 path)

`model → our LayOut() → { our pixels, our AX geometry }`. One layout pass is the
single source of truth feeding both consumers, so the painted caret and the caret
NVDA announces cannot drift. The editor (CK5/Lexical) supplies only the **document
model**; their borrowed browser layout is discarded — which is *why* A needs us to
build a real text layout engine (Skia `SkParagraph` / DirectWrite), the single
biggest cost, since neither editor ships one (docs/16 Idea 5).

- **Pro:** purest fidelity; the clean standards exhibit; geometry guaranteed
  consistent by construction.
- **Con:** we build (and maintain) full text layout — shaping, wrapping, bidi,
  fonts, IME — across platforms (docs/11/12).
- **Headless applies:** yes (run the editor engine with no contentEditable; we
  render).

## Architecture B — web-render + native UIA (reuse the browser's layout)

Keep the editor's web rendering, but own the accessibility tree:

1. Embed the web engine (CEF / WebView2) in **offscreen / windowless** mode; it
   renders the editor's contentEditable to a surface the host **composites into
   its own window**. The host gets the browser's mature text layout, shaping,
   fonts, wrapping, and IME for free. **[SOURCE-PENDING]**
2. The host **owns the window's UIA provider** and builds the AX tree from the
   **editor model** (the producer contract — CK5 differ / Lexical deltas →
   `AXTreeUpdate` → `AXTree`/`AXEventGenerator` → `AXPlatformNodeWin`/UIA). This
   yields the full native **semantics**: Text pattern, intents, markers,
   child-trees (docs/03, 09–10). **[SOURCE]** (producer half is proven on Linux)
3. **Geometry** UIA needs (caret rect, character/line bounding boxes, the
   `ITextRangeProvider::GetBoundingRectangles` and unit-navigation obligations) is
   **read back from the rendered DOM** — `Range.getClientRects()` per range, one
   rect per line box — and fed as `kCaretBounds` / range rectangles into the AX
   tree. **[SOURCE-PENDING]**

So all three things UIA needs — **semantics** (model), **geometry** (DOM
read-back), **pixels** (offscreen composite) — come through the host's bridge, and
the host assembles the native AX tree on top.

**Why B beats the plain contentEditable path:** the geometry source is the same as
the browser's own mapping (the rendered DOM), but **you own the semantics** —
expressing exactly the editing semantics the editors' models already give you,
instead of whatever Blink chooses to expose for a contenteditable. Same pixels,
same geometry, *far* richer and more controllable a11y.

- **Pro:** reuse the browser's layout/shaping/IME (skips A's biggest cost); fast
  path to a correct UIA editor for engines that have no layout of their own.
- **Con:** weaker form of the single-source thesis — pixels come from browser
  layout and a11y geometry is *scraped back* from it, so geometry sync is a
  read-back to keep tight, not a guarantee-by-construction. Carries an embedded
  Chromium.
- **Headless applies:** no (the editor renders its DOM).

### B's make-or-break risks (verification in flight)

1. **Suppressing the embedded engine's *own* UIA** so the screen reader sees the
   host's tree, not the browser's. This determines whether B is clean (host owns
   UIA outright) or requires fighting a second a11y tree. **[SOURCE-PENDING — the
   decisive item]**
2. **Geometry read-back stays synced with edits** without jank (the read-back is
   the coupling seam).

## Architecture C — plain web tab (recorded as ruled out)

Editor renders contentEditable; the **browser** owns and computes UIA from the
DOM. No way to inject model-driven semantics (docs/13). This is the capped path
the project ruled out — kept here only so the option set is complete.

## Recommendation

- **Lead with B for CK5/Lexical**, *pending* the suppression finding: it sidesteps
  building a text layout engine (the gap from docs/16 Idea 5) by reusing the
  browser's layout, while still delivering native-grade **semantics** from the
  model — which is the actual fidelity argument. Best fit for engines that bring a
  model but no layout.
- **Keep A as the purist / highest-fidelity option** and the cleaner standards
  exhibit; it's the right end-state if the read-back coupling in B proves too
  loose, or for a non-web-engine deployment.
- **A and B share the producer half.** The model → `AXTreeUpdate` → AX tree work
  (docs/03/09/16) is identical in both; they differ only in *who renders* and
  *where geometry comes from*. So the producer investment is not bet on this fork.
- **C stays out.**

## Status summary

| Claim | Evidence |
|---|---|
| Custom model-driven UIA requires owning the UIA provider (native shell) | **[SOURCE]** docs/13 |
| Producer model → AXTree → UIA semantics (Text pattern, intents, markers) | **[PROVEN]**/**[SOURCE]** docs/03, 09–10 |
| Headless applies only to A (own-canvas); B runs the editor's DOM | reasoned from the fork above |
| B: offscreen web render + host-owned UIA + DOM geometry read-back | **[SOURCE-PENDING]** (CEF/WebView2 + UIA verification in flight) |
| B: embedded engine's own a11y can be suppressed (the decisive risk) | **[SOURCE-PENDING]** |
