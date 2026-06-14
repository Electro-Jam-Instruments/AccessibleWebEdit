# 03 — Bridge Contract: The Producer → ui/accessibility API

Date: 2026-06-14. The concrete contract a non-Blink canvas editor (the "producer") must satisfy to drive Chromium's `ui/accessibility` layer for full editing semantics. This is the API spec that the docs/09 source reading and the T2 + matrix tests (results/) turned into hard requirements. It supersedes the loose "section 03 semantic model" references in docs/09–10 with tested obligations.

Status of each clause is marked: **[PROVEN]** verified by a passing generated-event test; **[SOURCE]** established by source reading; **[VM]** still to confirm on the Windows VM.

## 0. Shape of the contract

The producer owns an accessibility tree and pushes changes to it; the platform layer derives all events and exposes the platform-native AT API. The surface is intentionally small (docs/09 standards-track note): **post tree deltas + tree data; receive actions.**

```
producer (canvas editor)
   | 1. AXTreeUpdate  (node deltas + AXTreeData)   --->  AXTree / AXEventGenerator --> AXPlatformNode --> UIA/AT-SPI/...
   | 2. caret bounds  (kCaretBounds attribute)
   | 3. composition   (OnActiveComposition, input stack)
   ^ 4. actions       (AXActionTarget: setSelection, scroll, context-menu, ...)
```

The producer does **not** fire events manually — events are computed from tree-state deltas by AXEventGenerator. **[PROVEN]** (T2-1/3/5).

## 1. Node schema obligations

1.1 **The editable document root must carry a text-field role and editable state.** Editing events (`EDITABLE_TEXT_CHANGED`, `VALUE_IN_TEXT_FIELD_CHANGED`) target the nearest text-field ancestor (`GetTextFieldAncestor`). Without `Role::kTextField` (or another atomic/non-atomic text-field role) **plus** `State::kEditable` on the document root, all editing events silently vanish. **[PROVEN]** (T2-2: negative case verified — zero editing events without the role/state.)

1.2 **Text nodes that need formatting-change announcements must carry `State::kRichlyEditable` on the node itself.** Text-attribute change events (`TEXT_ATTRIBUTE_CHANGED`) are gated on the *changed node* being richly editable; plain `kEditable` ancestry produces zero events. Mirror what Blink does for contenteditable descendants: set `kRichlyEditable` on the canvas text nodes. **[PROVEN]** (matrix test FormatBold_RequiresRichlyEditable, positive + negative.)

1.3 **Markers (spelling/grammar/suggestion) attach to text and announce on the field ancestor.** Use `IntListAttribute::kMarkerTypes` / `kMarkerStarts` / `kMarkerEnds`; adding a `MarkerType::kSpelling` fires `SPELLING_MARKER_CHANGED` + `TEXT_ATTRIBUTE_CHANGED` on the text-field ancestor. **[PROVEN]** (matrix Markers_SpellingAdded.)

1.4 **Nested non-text objects use the embedded-object character.** A non-text child appears in the hypertext of its container as U+FFFC; the producer must keep hypertext and child structure consistent. **[SOURCE]** (docs/10 embedded-content row.)

1.5 **Child-tree stitching is a node attribute, not a root mechanism.** To embed a sub-document (e.g., a comment thread tree or an embedded component), set the child node's child-tree-id; any node can host a child tree (PDF precedent). **[SOURCE]** (docs/09 Finding 3.)

## 2. Selection and caret (AXTreeData)

2.1 **Selection lives in `AXTreeData`**, not on nodes: `sel_anchor_object_id` / `sel_anchor_offset` / `sel_focus_object_id` / `sel_focus_offset` / `sel_is_backward` / affinity. Driving these fields produces selection events for every shape — collapsed caret, forward/backward range, and **affinity-only changes at a wrapped line** (offsets identical, affinity flipped, still fires). **[PROVEN]** (T2-3, all four shapes.)

2.2 **Two selection events fire, on different nodes.** A sel_ change fires `DOCUMENT_SELECTION_CHANGED` at the tree root **and** `TEXT_SELECTION_CHANGED` on the text field containing the selection focus. When the focus leaves any text field (e.g., onto a table cell), `TEXT_SELECTION_CHANGED` correctly stops while `DOCUMENT_SELECTION_CHANGED` still fires. **[PROVEN]** (T2-3, T2-7 handoff). Which one the Windows manager translates to `UIA_Text_TextSelectionChangedEventId` is **[VM]** (NEXT-QUESTIONS #2/#5).

2.3 **Caret bounds are a SEPARATE producer obligation.** `CARET_BOUNDS_CHANGED` does **not** come from sel_ changes — it fires from the `IntListAttribute::kCaretBounds` attribute. The producer must update `kCaretBounds` on every caret move/scroll (the editor knows the on-screen rect; the tree does not derive it). The bridge API needs an explicit caret-bounds field. **[PROVEN]** (T2-3 caret-bounds test corrects docs/09's original reading.)

## 3. Edit deltas and atomicity

3.1 **A single character insert / word delete is a single-node name delta (or child add/remove).** It fires `EDITABLE_TEXT_CHANGED` + `VALUE_IN_TEXT_FIELD_CHANGED` on the field, plus `nameChanged` on the text leaf. No intents required. **[PROVEN]** (T2-1.)

3.2 **One AXTreeUpdate must carry the whole atomic edit** (text delta + tree-data selection move) so the platform finalizes a single coherent event set, not interleaved partial states — required for autocorrect/replacement announcement coherence. Within one pass, per-node events are deduplicated (e.g., `EDITABLE_TEXT_CHANGED` fires once even when both the deletion and creation paths request it). **[PROVEN]** (T2-5.)

3.3 **Intents are an optional, additive field.** UIA editing events do not require `AXEventIntent`; keep an optional intents field fed from the editor command layer for cross-platform announcement quality (and as the natural extension point for a future web API). **[SOURCE]** (docs/09 Finding 1.)

## 4. Actions (producer must accept)

4.1 **Provide an `AXActionTarget`-style action sink.** AT-initiated actions route back to the producer: set selection, scroll-to-make-visible, show-context-menu, and the actions behind `ITextRangeProvider::Select` / `ScrollIntoView` / `ShowContextMenu`. Mirror `PdfAXActionTarget`'s coverage. **[SOURCE]** (docs/09 Finding 3); end-to-end behavior is **[VM]** (T2-6).

## 5. IME / composition (input stack, not the tree)

5.1 **Composition does not flow through the tree** — it rides the OS input stack. The producer (host) must call `AXPlatformNodeWin::OnActiveComposition` (minimum viable) or wire `TSFTextStore` (full fidelity) so the UIA `ITextEditProvider` exposes the active composition. Committed compositions deliberately defer to the standard text-changed path (no double-announce). **[SOURCE]** (docs/09 Finding 2, docs/12); round-trip is **[VM]** (T2-4). Per-platform contract: docs/12 table.

## 6. Tables, structure, widgets (matrix-verified)

- Cell/row/column/discontiguous selection via `BoolAttribute::kSelected`: fires `SELECTED_CHANGED` on the cell + `SELECTED_CHILDREN_CHANGED` on the selection container. **[PROVEN]**
- Add rows: `CHILDREN_CHANGED` + `SUBTREE_CREATED`; **no row-count event exists** — counts are structural or re-queried. **[PROVEN]**
- Sort: `IntAttribute::kSortDirection` fires `SORT_CHANGED`, **only on table/grid header roles**. **[PROVEN]**
- Checkable items / progress: `kCheckedState` → `CHECKED_STATE_CHANGED`; `kValueForRange` → `RANGE_VALUE_CHANGED`. **[PROVEN]**
- Expand/collapse (comment threads) + pos-in-set (kanban reorder): `kExpanded` → `EXPANDED`/`COLLAPSED`; `kPosInSet` → `POSITION_IN_SET_CHANGED`. **[PROVEN]**
- Comments/annotations anchor via `IntListAttribute::kDetailsIds` → `DETAILS_CHANGED`. **[PROVEN]** (matrix extension, 2026-06-14.)
- Live announcements (collaboration presence, status): a live-region root (`kLiveStatus`) with descendants carrying `kContainerLiveStatus` fires `LIVE_REGION_CHANGED` on the root + `LIVE_REGION_NODE_CHANGED` on the changed node. **[PROVEN]** (matrix extension.) On the web the equivalent is `ariaNotify` (docs/13).

## 7. Known gaps the contract cannot express today (feed planning)

- **Remote cursors/selections:** AXTreeData carries exactly one (self) selection; no schema for non-self carets. Model as highlight markers (lossy) or propose schema. **[SOURCE]** (NEXT-QUESTIONS #3.)
- **Edit origin:** no field distinguishes remote vs local edits; both fire identical `EDITABLE_TEXT_CHANGED`. **[SOURCE]** (NEXT-QUESTIONS #4.)
- **Column hide / filter state:** no schema distinguishes hidden vs deleted; no "N of M shown". **[SOURCE]** (NEXT-QUESTIONS #9/#10.)

## 8. Minimum producer checklist (to build a real editing surface)

1. Build a tree: editable text-field root (1.1) → richly-editable text nodes (1.2) → static text leaves.
2. On every edit, post ONE AXTreeUpdate with the node delta + updated AXTreeData selection (3.2).
3. On every caret move/scroll, update `kCaretBounds` (2.3).
4. Wire `OnActiveComposition` for IME (5.1).
5. Implement the action sink (4.1).
6. Optionally populate intents (3.3) and live regions / ariaNotify (6) for announcement quality.

Everything in §1–3 and §6 is verified at the generated-event layer (104 + matrix tests). §4–5 and the UIA-translation half of §2/§6 are the Windows-VM verification queue (NEXT-QUESTIONS #5–7).
