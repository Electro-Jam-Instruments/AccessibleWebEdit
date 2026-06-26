# 16 — CKEditor 5 Integration Track: ideas for driving the native producer

Date: 2026-06-26. The **CKEditor-5-specific** track for putting a real rich-text
engine on top of this project's producer → `ui/accessibility` contract
(docs/03/09), toward the one-layout coupling of docs/14. Lexical gets its own
sibling doc (docs/17); the two are pursued **independently** by design.

**Decision recorded (2026-06-26):** the web/contentEditable path is **out of
scope**. The current web a11y primitives cannot carry the editing scenarios this
project targets (docs/13: AOM virtual nodes dead; the canvas-mirror ceiling), so
"ship on contentEditable as-is" is not a candidate. We commit to the **native
producer** path; this doc is the CKEditor half of executing it.

CKEditor internals below are confirmed from primary sources (official docs +
`github.com/ckeditor/ckeditor5`), tagged **[SOURCE-EXT]**. Chromium/producer
facts keep this repo's PROVEN/SOURCE/VM tags.

## The integration shape

```
 CK5 Model (browser-independent TS engine)
        |  commit-time change feed  (Differ diffs; or serialized operations)
        v
 [translator]  ── unified JSON delta ──►  C++ producer
        v
 AXTreeUpdate  ─►  AXTree / AXEventGenerator  ─►  AXPlatformNode  ─►  UIA/AT-SPI/...  ─► AT
```

The seam is CK5's **commit-time change feed**. Everything below is about how to
tap it, run the engine without a browser, and shape the C++ side.

## The ideas

### Idea 1 — Change feed: Differ first, operations as the OT-grade alternative

- **`model.document.differ.getChanges()`** is the recommended feed: position-
  sorted `insert`/`remove`/`attribute` items, and it is *the same diff CK's own
  downcast→view pipeline consumes* — so we ride a proven path rather than a novel
  one. **[SOURCE-EXT]**
- **Must walk inserted subtrees.** Documented limitation: for an inserted
  element only the top-most node gets a diff item; the consumer walks its
  descendants itself. Build the AX subtree by traversing the inserted root.
  **[SOURCE-EXT]**
- **Operations are the alternative feed.** Every operation is JSON-serializable
  (`toJSON` / `OperationFactory.fromJSON`) — it's literally what collaboration
  sends over the wire, so it's a stable cross-process stream. But it's
  *OT-semantic*, not literal tree mutation:
  - **Removal is a `MoveOperation` into the graveyard root — there is no
    `RemoveOperation`.** (Corrects an earlier assumption.) **[SOURCE-EXT]**
  - Attribute ops carry old+new; `split`/`merge`/`rename` are structural.
- **Idea:** use the **Differ for the tree rebuild** (simpler, matches CK's own
  view sync) and tap **operations only to derive intents** (op type →
  `AXEventIntent`: insert→kInsert, attribute→kFormat, marker→kMarker — the
  optional intent channel of docs/09 Finding 1).

### Idea 2 — Run the editor *engine* without its DOM (so our renderer can take over)

> **"Headless" here means dropping the *editor's own* contentEditable rendering —
> which we are replacing with our canvas — NOT dropping rendering.** We want the
> editor's **model/state**; our layout + raster supply the pixels (Idea 5). This
> is "headless editor," not "no pixels."

- **No supported headless editor class** — issue #438 is closed *not planned*.
  Headless is DIY. **[SOURCE-EXT]**
- Three concrete options, recommendation in order:
  1. **Engine-only (recommended):** depend on `@ckeditor/ckeditor5-engine`
     (+`-utils`), build/mutate the model through `model.change()`/the writer,
     and **never instantiate UI plugins or the editing view**. This is how
     CKEditor's own Cloud Services runs the model server-side (via the operation
     stream) — precedent that the engine runs without a DOM.
  2. **jsdom shim:** provide a fake DOM so a full `ClassicEditor` boots; heavier,
     but unlocks `*editing` plugins that transitively touch UI (`editor.ui`,
     `FocusTracker`, CSS/SVG icon imports).
  3. **Cloud Services as reference only:** it proves headless operation but the
     Node OT server is closed-source — a precedent, not an artifact we can take.
- **Idea:** start engine-only; reach for jsdom only when a needed feature plugin
  won't load without it.

### Idea 3 — Auto-derive the node→AX role map from the schema

- `editor.model.schema` is fully introspectable at runtime:
  `getDefinitions()`, per-item `isBlock()/isInline()/isObject()/isLimit()/
  isSelectable()`, and `allowAttributes`. **[SOURCE-EXT]**
- **Idea:** generate the `AXNodeData` role mapping *from schema flags* instead of
  hand-maintaining it, so it stays correct as plugins add element types:
  - `isObject` → self-contained widget; emit the embedded-object character
    U+FFFC in the container hypertext (docs/03 §1.4).
  - `isBlock` → block role by element name (paragraph→`kParagraph`,
    heading1–6→`kHeading`+level, listItem→`kListItem`, blockQuote→`kBlockquote`,
    table/tableRow/tableCell→`kTable`/`kRow`/`kCell`).
  - `isInline` → text run; map attributes (bold/italic/linkHref) to text
    attributes, setting `kRichlyEditable` on text nodes for formatting-change
    announcements (docs/03 §1.2).
  - `isLimit` → boundary/region (selection/navigation boundary).
- A small element-name→role table covers the well-known set; the schema flags
  catch everything else generically.

### Idea 4 — Markers → accessibility annotations (CK's structural-editing win)

- `model.markers` is a range-based `MarkerCollection`: markers live outside the
  tree, ride **live ranges** so they survive edits, and (when operation-managed)
  participate in undo and collaboration sync. **[SOURCE-EXT]**
- **Comments** are markers carrying a thread id; **track-changes/suggestions**
  are `Suggestion` markers. **[SOURCE-EXT]**
- **Idea:** map markers onto the producer's existing marker contract
  (`kMarkerTypes`/`kMarkerStarts`/`kMarkerEnds`, docs/03 §1.3) and stitch comment
  threads as **child trees** (docs/03 §1.5). CK's marker model is a near-exact
  match for the comments/suggestions rows already graded in docs/10 — this is
  where CKEditor's rich editing maps most cleanly to the AX scenarios.
- *Caveat:* comments/track-changes are paid features (Idea 7).

### Idea 5 — Our layout is the single source of truth (rendering runs *with* a11y from day one)

- CK5 has **no pixel layout engine** — it borrows the browser's contentEditable
  layout, **which is exactly the rendering we are replacing**. **[SOURCE-EXT]** So
  the integration is *not* "reuse the model and add a11y"; it is: **the editor
  supplies the document model/state, and *our* layout engine is the single source
  of truth feeding BOTH the painted pixels and the AX geometry from one pass**
  (the docs/14 thesis). The editor's own layout is discarded.
- **Consequence: rendering and a11y must run together from the first spike —
  coupling them *is* the point, not a later phase.** (This corrects an earlier
  draft that staged a geometry-free "Phase 1"; a no-pixels spike skips the one
  thing this project exists to prove.)
- **What changes by phase is layout *fidelity*, not whether pixels exist** — and
  neither phase needs the VM to show the coupling:
  - **Phase 1 (Linux, now):** a lean CPU **block** layout (extend the demo's
    single-line `layout_engine.h` to multi-line/blocks) → CPU raster pixels (PPM,
    like `demo_render`) **and** real AX geometry/events into a live `AXTree`
    (like `demo_e2e`) — all from one `LayOut()` pass, driven by the rich model.
    No window, GPU, or VM. This is "headless of a window," **with** real pixels.
  - **Phase 2 (VM/Skia):** swap the lean layout for Skia `SkParagraph` +
    DirectWrite for production text fidelity; the `LayOut() → {paint, AX}`
    *contract* is unchanged. NVDA then closes the loop (docs/14 Stages 2–4).

### Idea 6 — JS↔C++ bridge, with an editor-agnostic delta schema

- The CK engine is TS/JS; the producer is C++. Either embed a JS runtime
  (V8/Node) in the native host, or run the engine as a **sidecar Node process**
  streaming JSON deltas over IPC. Operations are JSON-serializable by design, so
  the IPC contract is nearly free for the operations route; Differ items we
  serialize ourselves.
- **Idea (the unification point with the Lexical track):** define **one small,
  stable JSON delta schema** — `{node add/remove/update, text, selection,
  markers}` — that *both* CK and Lexical emit, so the **C++ translator is
  editor-agnostic**. CK and Lexical differ only in the thin JS adapter that
  produces this schema; the producer below it is shared.

### Idea 7 — Licensing reality (a product gate, not a technical one)

- Core engine: **GPL 2+**, with `config.licenseKey` **mandatory since v44.0.0
  (Dec 2024)** — open-source use sets `licenseKey: 'GPL'` and shows a "Powered by
  CKEditor" logo. **[SOURCE-EXT]**
- **Every rich scenario we'd showcase for a11y — comments, track changes,
  real-time collaboration, export, pagination — is commercial/paid.**
  **[SOURCE-EXT]**
- **Implication:** an *open-source* CK-based a11y exhibit can use **core editing
  only**; the comment/suggestion a11y mapping (Idea 4) needs a commercial
  license to demo against real features. This is a real differentiator vs.
  Lexical (MIT) when choosing the open exhibit — capture it in the docs/17
  comparison.

## Risks (CKEditor-specific)

| ID | Risk | Mitigation |
|---|---|---|
| R-CK1 | Headless is DIY (#438 not planned) | Engine-only layer; jsdom fallback. Engineering, not architecture. |
| R-CK2 | OT semantics leak if using operations (remove = graveyard move) | Prefer Differ for the tree; operations only for intents. |
| R-CK3 | Differ collapses inserted subtrees | Walk the inserted root's descendants. |
| R-CK4 | Rich scenarios are paid | Open exhibit = core editing; price a commercial license for comments/track-changes a11y. |
| R-CK5 | TS→C++ marshalling cost (shared) | Sidecar + stable JSON delta schema (Idea 6). |

## First spike (CKEditor) — model swapped into the *coupled* demo, rendering included

The spike is the existing `demo/` architecture with the document model swapped
from the flat `TextDocument` to a CK5-backed model — so pixels **and** a11y run
together from one layout, exactly as the product must.

1. Run CK's model **engine-only** (`@ckeditor/ckeditor5-engine`, no UI/
   contentEditable) to hold document state + deltas; build paragraph/heading/
   list/table via the writer, run an edit script analogous to
   `demo/core/demo_script.h`. On `change:data`, read `differ.getChanges()` (with
   the subtree walk) + `model.document.selection`; emit the **unified JSON delta**
   (Idea 6).
2. Feed that delta into the demo core in place of `TextDocument`, and drive
   **our** `LayOut()` so **one pass** produces *both* the CPU raster pixels (PPM
   frames, like `demo_render`) **and** the AX geometry/events into the real
   `AXTree` (like `demo_e2e`).
3. Assert the painted pixels and `kCaretBounds` come from the **same** `Layout`
   (the demo already checks this for the flat model) and that the generated-event
   set matches.

This runs together **on Linux today** — rendering + a11y from one layout, no VM.
Skia/DirectWrite is the Phase-2 *fidelity* upgrade (Idea 5), not a prerequisite
for the coupling.

**Sequencing:** CK's headless friction (R-CK1) makes it the *second* spike. Run
the **Lexical** spike first (docs/17 — `@lexical/headless` is turnkey) to prove
the shared layout-swap + the unified JSON delta schema against the coupled demo;
CK then only needs the engine-only feed adapter onto that proven schema.

## Status summary

| Claim | Evidence |
|---|---|
| Model is a DOM-decoupled source-of-truth tree | **[SOURCE-EXT]** |
| Differ gives position-sorted insert/remove/attr diffs (collapses inserted subtrees) | **[SOURCE-EXT]** |
| Operations are JSON-serializable; remove = move-to-graveyard (no RemoveOperation) | **[SOURCE-EXT]** |
| Headless is DIY (#438 closed not planned); engine runs server-side in collab | **[SOURCE-EXT]** |
| Schema is runtime-introspectable for a role map | **[SOURCE-EXT]** |
| Markers back comments/track-changes; range-based, survive edits | **[SOURCE-EXT]** |
| Core GPL (licenseKey required since v44); rich scenarios paid | **[SOURCE-EXT]** |
| Producer contract carries tree/markers/child-trees | **[PROVEN]**/**[SOURCE]** docs/03, 09–10 |
| CK model feed drives our producer end to end | **[SPIKE]** — not yet run (first spike above) |
