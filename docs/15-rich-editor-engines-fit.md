# 15 — Rich-Editor Engines (CKEditor 5, Lexical) and the Producer Contract

Date: 2026-06-25. Answers a direct question: a "CK5/Lexical type of editing
experience" — can it use **this project's API** (the producer → `ui/accessibility`
bridge of docs/03/09, the one-layout-pass coupling of docs/14), or would it need
something else?

Companion to docs/13 (web vs native a11y doors) and docs/11 (cross-platform
rendering). Web-API status carries over from docs/13; the two editors' internals
below are confirmed from primary sources (official docs + repo source), tagged
**[SOURCE-EXT]** to distinguish third-party sourcing from this repo's
PROVEN/SOURCE/VM Chromium evidence.

## TL;DR

- **The hard part is already done in both editors.** CKEditor 5 (*Model*) and
  Lexical (*EditorState*) each own a custom document tree that is the single
  source of truth, decoupled from the DOM, and **both already emit structured
  per-transaction deltas** — the exact shape `AXTreeUpdate` wants. This is a
  *better* starting point than the demo's flat `TextDocument`, not a worse one.
- **But the API is native-only and below the web sandbox.** A web-deployed
  CK5/Lexical *cannot* reach `ui/accessibility` (the AOM virtual-node door is
  dead, docs/13) — and *doesn't need to*, because it renders a **real
  contentEditable** and gets a real platform Text pattern from the browser. That
  is **not** the capped mirror-DOM case; the docs/13 ceiling is about *canvas*
  editors, which these are not.
- **The fit is in a native (or native-shell) deployment.** Reuse the editor's
  *model + delta* layer as the producer; this API is then the right target and
  delivers fidelity the contentEditable path can't guarantee across
  browsers/ATs. What must still be built ("something else"): a **tree-shaped
  producer** (node→AX-role mapping, vs. the flat field), a **real layout engine**
  (these editors borrow the browser's; the one-layout coupling needs you to own
  it), a **JS↔C++ delta bridge**, and **native IME** (docs/12).
- **Cheap, runnable next step (no VM, no canvas):** drive the existing Linux
  `demo_e2e` AXTree producer from **`@lexical/headless`** deltas in Node — a
  "bring-your-own-model" spike that proves a real third-party rich-text engine
  feeds our producer contract.

## What CK5 and Lexical actually are (sourced)

Both are **contentEditable** rich-text frameworks with a model/view split. Neither
renders to `<canvas>` (repo-wide searches for canvas/getContext return nothing in
Lexical; no CK5 source references canvas — both state contentEditable explicitly).
**[SOURCE-EXT]**

| Concept | CKEditor 5 | Lexical |
|---|---|---|
| Source-of-truth model | **Model**: DOM-like tree, elements + text **with attributes**, separate from DOM | **EditorState**: immutable node tree (`RootNode`→`ElementNode`/`TextNode`…) + selection |
| Mutation API | `model.change()` → **operations** (OT-derived) | `editor.update()` → `getWritable()` clones; double-buffered current/pending |
| Render path | Model → **view** (virtual DOM) → `ViewRenderer`/`DomConverter` → contentEditable | EditorState → **reconciler** diff → contentEditable |
| Per-change delta | **`differ.getChanges()`**: position-sorted `insert`/`remove`/`attribute` items; `change:data` event | **mutation listener** (`created`/`updated`/`destroyed`) + `dirtyLeaves`/`dirtyElements` + `prevEditorState`/`editorState` |
| Selection | `ModelSelection` (ranges + direction) | RangeSelection/NodeSelection, reconciled to DOM selection |
| A11y today | browser contentEditable mapping; **`AriaLiveAnnouncer`** for *discrete state* (autosave/validation), not content | browser contentEditable mapping; **no** core aria-live, just ARIA props on the editable |
| Browser-independence | model + view are DOM-free TS; DOM contact isolated to `Renderer`/`DomConverter`. Headless possible but **not turnkey** (UI barrels leak in) | **`@lexical/headless`** is an officially supported DOM-free mode; reconciler is the only DOM-coupled layer |

Sources (high-value): CK5 editing-engine docs + `differ.ts`/`document.ts`/
`arialiveannouncer.ts` (github.com/ckeditor/ckeditor5); CK5 lead architect on
headless (ckeditor5#438). Lexical docs `concepts/editor-state|nodes|transforms|
listeners` + `LexicalReconciler.ts`/`LexicalUpdates.ts`/`LexicalEditor.ts`/
`lexical-headless` (github.com/facebook/lexical; lexical.dev mirrors these).

## The three deployment scenarios (the real answer)

The question "can it use this API" only resolves once you fix *where the editor
runs*. There are three cases and they give three different answers.

### Scenario 1 — Web app, contentEditable (what CK5/Lexical are today)

- **Use this API? No — and it doesn't need to.** Page JS has no door into
  `ui/accessibility` (docs/13: virtual nodes dead). But because the editor
  renders a *real* contentEditable, the browser maps it to a real platform Text
  pattern. This is the genuine article (if browser/AT-inconsistent), **not** the
  capped canvas-mirror case.
- **"Something else"** = exactly what they do now: contentEditable + ARIA +
  (CK5) live regions for discrete state. The project's API is irrelevant here.

### Scenario 2 — Web app, canvas-rendered (a Google-Docs-style migration)

- **Use this API? No — still sandboxed.** If the editor moves rendering off
  contentEditable onto canvas (for pagination/layout fidelity/perf), it *loses*
  the free contentEditable a11y and lands squarely under the **docs/13 mirror-DOM
  ceiling**.
- The model→delta seam makes the canvas **port mechanically feasible** (the same
  `differ`/mutation feed drives both the canvas painter and an offscreen mirror),
  but a11y is capped. The only web path that could lift it is **HTML-in-Canvas**
  (`drawElement`, docs/13) — early and flag-gated.
- **"Something else"** = mirror DOM (capped) or HTML-in-Canvas (emerging). Not
  this API.

### Scenario 3 — Native shell reusing the editor's model (docs/11 Strategy A territory)

This is where **this API is the answer.** Run the editor's *model engine* (which
is browser-independent in both — Lexical headless; CK5 model/view) inside a native
host, and translate its commit-time deltas into `AXTreeUpdate`. You get the full
UIA Text pattern, delta-driven editing events, and caret/selection geometry the
project validated at the generation layer (docs/09–10, T2 tests) — the fidelity
the contentEditable path can only *approximate per browser*.

**Why the delta mapping is clean** (this is the load-bearing fit):

| Producer contract (docs/03) | CK5 source | Lexical source |
|---|---|---|
| `AXTreeUpdate` node add/remove/update | `differ` `insert`/`remove`/`attribute` (position-sorted) | mutation listener `created`/`destroyed`/`updated` + dirty sets |
| `AXTreeData` sel_ fields → `DOCUMENT_SELECTION_CHANGED` + caret (§2) | `ModelSelection` ranges + direction | RangeSelection (prev + next available) |
| atomic multi-part update (§ T2-5) | one `Batch` per `model.change()` | one `editor.update()` commit (double-buffered) |
| optional intents (docs/09 Finding 1) | operation/command layer | update `tags` |

The editors hand you, post-commit, *precisely* the "post tree deltas + tree data"
surface docs/09 calls the whole web-facing API. The translation layer is a diff
re-encoder, not a rewrite.

## What still has to be built in Scenario 3 ("something else")

The model plugs in; four things do **not** come for free.

1. **A tree-shaped producer.** The demo's `TextDocument` is a flat string → one
   `kTextField` node. CK5/Lexical are rich block/inline trees (paragraphs,
   headings, lists, tables, links, embeds). The producer must map node types →
   `AXNodeData` roles (paragraph→`kParagraph`, heading→`kHeading`, list→`kList`,
   cell→`kCell`, embed→U+FFFC per docs/03 §1.4) and manage per-node text offsets.
   **The architecture already covers this** — docs/09–10 establish structured
   editing (tables, comments, child-tree stitching) and that editing events
   target via `GetTextFieldAncestor` — but the demo's producer must grow from
   string → tree. This is the largest *modeling* task.

2. **A real layout engine — the precondition for the coupling.** The project's
   thesis is "*one* layout pass feeds both pixels and a11y geometry" (docs/14).
   CK5/Lexical have **no pixel layout engine** — they delegate layout to the
   browser's contentEditable. So a native shell must *add* one (Skia
   `SkParagraph`/DirectWrite, docs/14 Stage 2) to produce caret/selection/
   per-character geometry. **This is the biggest gap: their model is reusable,
   their layout is not** (it's the browser's). Without it you can still emit a
   correct *semantic* AX tree (roles, text, selection offsets), but not the
   geometry-coupled `kCaretBounds` story that makes "see it and hear it from one
   layout" true.

3. **A JS↔C++ delta bridge.** Their engines are TS/JS; the producer is C++. The
   seam is well defined: serialize the post-commit delta (CK5 `getChanges()` /
   Lexical mutation listener) across an FFI/IPC boundary to a C++ translator that
   emits `AXTreeUpdate`. This is the VS Code/Electron shape, but feeding a native
   AX bridge instead of Chromium's web stack — an integration boundary, not a
   port.

4. **Native IME.** docs/12 — TSF→`OnActiveComposition` wiring is yours in a
   native shell (on the web, EditContext would ride IME but does nothing for
   a11y, docs/13). Same task the B-lite host already has queued.

## Recommendation

- **Web product:** keep CK5/Lexical on contentEditable (Scenario 1). Don't reach
  for this API — you'd lose a real Text pattern to gain an unreachable one. If
  canvas rendering becomes a requirement, accept the docs/13 ceiling and track
  HTML-in-Canvas.
- **Native product (full editing fidelity):** reuse the editor's **model + delta**
  layer as the producer (Scenario 3). It is a *strong* fit and arguably the
  fastest route to a rich, real-world editor on top of the producer contract —
  you inherit a battle-tested document model and its structured diff, and write
  the tree-producer + layout + bridge around it.
- **Do not** try to make a *web-sandboxed* CK5/Lexical talk to `ui/accessibility`
  directly. That door is closed (docs/13) and reopening it is a standards play,
  not an integration.

## Concrete next step — the "bring-your-own-model" spike (no VM, no canvas)

A runnable Linux exhibit that de-risks the whole Scenario-3 mapping without the
Windows VM or a renderer:

1. Stand up an `@lexical/headless` editor in Node (DOM-free, officially
   supported). Run an edit script analogous to `demo/core/demo_script.h`.
2. On each `registerUpdateListener`/mutation callback, serialize the delta
   (created/updated/destroyed keys + text + selection) to stdout/JSON.
3. Feed that into a small translator that emits `AXTreeUpdate` and drives the
   **existing** `demo/platform/linux_headless/demo_e2e.cc` AXTree +
   `AXEventGenerator` (the producer we already run on Linux).
4. Assert the generated-event set matches the demo's expectations
   (`editableTextChanged`/`valueInTextFieldChanged` + selection/caret on the
   right nodes).

Result: proof that a **real third-party rich-text engine** drives this project's
producer contract end to end — on Linux, today. Lexical first (headless is
turnkey); CK5 is possible but needs UI-plugin stubbing (ckeditor5#438), so it's a
second pass. This would upgrade the Scenario-3 delta mapping above from
**[SOURCE-EXT]** (argued from their APIs) to a locally reproduced result.

## Status summary

| Claim | Evidence |
|---|---|
| CK5/Lexical own a DOM-decoupled source-of-truth model | **[SOURCE-EXT]** official docs + repo source |
| Both emit structured per-transaction deltas matching `AXTreeUpdate` shape | **[SOURCE-EXT]** `differ.getChanges()` / mutation+update listeners |
| Both render only to contentEditable, never canvas | **[SOURCE-EXT]** (negative claim; searches + explicit docs) |
| Web-sandboxed editors cannot reach `ui/accessibility` | **[SOURCE]** docs/13 (AOM virtual nodes dead) |
| contentEditable editors get a real Text pattern (not the mirror ceiling) | **[SOURCE]** docs/13 distinction |
| Producer contract carries structured/tree editing | **[PROVEN]**/**[SOURCE]** docs/03, docs/09–10, T2 tests |
| A real engine's deltas drive our producer end to end | **[VM/SPIKE]** — the headless-Lexical spike above, not yet run |
