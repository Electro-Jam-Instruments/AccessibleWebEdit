# 09 — Child-Tree Path for Full Editing Semantics: Tier 1 Source Findings

Status: Tier 1 complete (direct source reading). Open items are Tier 2 tasks for Claude Code.
Source pin: Chrome 149 stable, tag 149.0.7827.115. All file paths and line numbers refer to that tag.

## Question under test

Can a non-Blink tree producer (our canvas bridge), stitched the way PDFium stitches its accessibility tree, carry full editing semantics: caret, selection, text-changed events, autocorrect-style replacement announcements, IME composition, virtualization, and AT-initiated actions?

Tier 1 verdict: yes on the schema and event-generation layers, with two items to verify empirically (Tier 2) and one item needing explicit engineering in the B-lite host (IME wiring).

## Finding 1 — UIA editing events derive from raw tree deltas. Intents are optional.

The Windows UIA event surface is driven by AXEventGenerator, computing events purely from tree-state differences:
- Text changes under a text-field ancestor fire EDITABLE_TEXT_CHANGED and VALUE_IN_TEXT_FIELD_CHANGED. See FireValueInTextFieldChangedEventIfNecessary, ui/accessibility/ax_event_generator.cc lines 1124 to 1148. Logic walks from the changed node to GetTextFieldAncestor and fires on the ancestor.
- Selection and caret are tree-level: any change to the sel_ fields of AXTreeData fires DOCUMENT_SELECTION_CHANGED at the root (ax_event_generator.cc, OnTreeDataChanged, lines 937 to 945). CARET_BOUNDS_CHANGED also generated (line 921).
- BrowserAccessibilityManagerWin::FireGeneratedEvent queues EDITABLE_TEXT_CHANGED into text_changed_nodes_ (line 1233) and DOCUMENT_SELECTION_CHANGED into selection_changed_nodes_ (line 1239). FinalizeAccessibilityEvents fires UIA_Text_TextChangedEventId and UIA_Text_TextSelectionChangedEventId, the latter only on nodes supporting the Text pattern (ui/accessibility/platform/browser_accessibility_manager_win.cc lines 1300 to 1314). Note this manager now lives in ui/accessibility/platform, not content/, which strengthens B-lite: it can reuse the real event finalize machinery.

AXEventIntent (ui/accessibility/ax_event_intent.h) carries a rich vocabulary: Command (kInsert, kDelete, kFormat, kSetSelection, kMoveSelection, kExtendSelection, kHistory, kMarker) crossed with InputEventType including kInsertReplacementText (autocorrect and autoformat replacement), kInsertCompositionText, kInsertFromPaste, full deletion granularities, and kFormatBold through kFormatSetBlockTextDirection (ui/accessibility/ax_enums.mojom). In the Windows UIA path intents are consumed in only a few places, mainly spin-button double-fire suppression (ax_event_generator.cc lines 1138 to 1144 and 585 to 591). UIA editing events do not require intents.

Bridge contract consequences:
- The producer gets UIA text-changed and selection-changed by posting AXTreeUpdate deltas plus updated AXTreeData. No Blink editing machinery needed.
- The canvas document subtree root must carry an editable text-field role and state, because event targeting walks to GetTextFieldAncestor. Constrains the section 03 semantic model.
- Keep an optional intents field in the bridge API, fed from the editor command layer: additive for cross-platform announcement quality, not load-bearing on Windows today.

## Finding 2 — IME composition flows from the input stack, not the tree producer.

ITextEditProvider methods (GetActiveComposition, GetConversionTarget) are generic in ui/accessibility/platform/ax_platform_node_textprovider_win.cc (lines 281 to 300, helper at 367 to 393). The helper returns a range only when the owning node has focus and an active composition. Composition state arrives from the Windows text services framework: TSFTextStore calls TextInputClient::SetActiveCompositionForAccessibility (ui/base/ime/win/tsf_text_store.cc lines 1646 to 1690, covering committed and ongoing composition), routing to AXPlatformNodeWin::OnActiveComposition (ui/accessibility/platform/ax_platform_node_win.cc line 823), cached and read back by the text provider.

Consequences: in a real browser a canvas editor using EditContext gets this for free (EditContext rides the normal IME pipeline; strengthens the standards pairing). In the B-lite standalone host there is no TSF wiring unless built: minimum viable is calling OnActiveComposition directly from host input handling; full fidelity is integrating TSFTextStore. New explicit build-plan task.

## Finding 3 — The PDF producer proves the full cross-tree contract, including actions back into the producer.

components/pdf/renderer/pdf_accessibility_tree.cc is a complete production non-Blink producer:
- Owns its AXTreeData: sets tree_id and focus_id (lines 431 to 438), computes full selection including direction and node-plus-offset endpoints (lines 688 to 707), exposed via GetTreeData (lines 856 to 868). Tree-level caret and selection from a non-Blink producer is established practice.
- AT actions route back through an AXActionTarget adapter (CreateActionTarget, header line 123; PdfAXActionTarget) and HandleAction (line 978), covering selection setting, scrolling, show-context-menu (lines 941 to 951). The action channel needed for ITextRangeProvider Select, ScrollIntoView, ShowContextMenu exists and is symmetric.
- Trees nest arbitrarily: PdfAccessibilityTree::SetChildTree (lines 953 to 977) stitches a further child tree by adding a child-tree id to a node. Stitching is a node attribute, not a root-only mechanism.

Remaining cross-tree risk is narrow: not whether focus and selection can live in a child tree (production fact), but whether UIA text range navigation behaves at the host-child boundary during rapid edits. Targeted empirical question, not architectural.

## Revised risk register

- R1 intents: closed for Windows UIA. Downgraded to cross-platform announcement quality. Bridge keeps optional intents field.
- R2 IME: mechanism understood. New B-lite engineering task (TSF wiring or direct OnActiveComposition). Empirical verification needed.
- R3 cross-tree: architecture proven by PDF. Narrow empirical question remains under rapid edits.

## Tier 2 tasks for Claude Code (against the 149.0.7827.115 checkout)

T2-1. Delta-only editing events. In B-lite, post AXTreeUpdate deltas simulating single character insert, word deletion, and a replacement (autocorrect shape: delete word, insert different word, move caret, one atomic update). Run ax_dump_events. Confirm UIA Text_TextChanged and Text_TextSelectionChanged fire with no intents supplied. Record exact event sequences per edit shape.

T2-2. Text-field ancestor requirement. Repeat T2-1 with the document root missing the editable text-field role and state. Confirm events stop firing. Validates the section 03 role constraint.

T2-3. Selection and caret via AXTreeData. Drive sel_ fields through caret moves and range selections, including backward selection and affinity at a wrapped line. Confirm DOCUMENT_SELECTION_CHANGED timing and GetCaretRange correctness from a UIA client.

T2-4. Composition simulation. Call AXPlatformNodeWin::OnActiveComposition from the host during a fake composition. Confirm GetActiveComposition and GetConversionTarget return correct ranges; identify which UIA events accompany composition commit.

T2-5. Atomic multi-part updates. Verify one AXTreeUpdate carrying text delta plus tree-data selection change produces text-changed and selection-changed in a single finalize pass, not interleaved partial states. Autocorrect announcement coherence test.

T2-6. Read PdfAXActionTarget and mirror its action coverage in the B-lite delegate: set selection, scroll-to-make-visible, show-context-menu.

T2-7. Cell-versus-text selection handoff (from section 10): when text selection inside a table cell transitions to cell-level selection and multi-cell selection, record which UIA events fire and in what order.

## Standards-track notes

- The delta-driven event model means a web-facing API could be as small as: post tree deltas plus tree data, receive actions. The intent vocabulary already exists in the mojom as the natural extension point.
- EditContext pairing is stronger than assumed: composition accessibility requires the input-stack path EditContext already rides, so a text provider API assuming EditContext closes the IME gap by construction.
