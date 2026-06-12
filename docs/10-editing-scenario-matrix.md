# 10 — Editing Scenario Matrix: Loop, Word Online, Docs

Status: seeded. Selection-pattern mapping confirmed at source level (Chrome 149, tag 149.0.7827.115). Full matrix grading is a future pass.

## Purpose

Inventory every editing interaction a modern collaborative editor exposes, then grade each: SUPPORTED (expressible through the AX tree today), BROWSER-WORK (schema or UIA translation gap in Chromium), or OPEN (needs research). This is both the engineering checklist and the standards-argument evidence.

## Headline question answered: multi-cell table selection and SelectionPattern2

Scenario: focus in a table, text selected inside a cell, then a whole cell selected, then a range or discontiguous group of cells.

Source findings:
- Item-level selection is fully generic. ISelectionItemProvider is implemented on the platform node (ui/accessibility/platform/ax_platform_node_win.cc lines 3127 to 3236): get_IsSelected reads node selection state; AddToSelection, RemoveFromSelection, Select route back as actions to the producer. Discontiguous selection is the selected attribute on arbitrary cells.
- Container-level selection is UIA Selection pattern v1. ISelectionProvider (lines 3238 to 3285): GetSelection enumerates selected descendants; CanSelectMultiple from the multiselectable state; IsSelectionRequired. Exposed on containers with selectable children (line 8652).
- Table and TableItem patterns ride along on table-like roles and cells (lines 8658 to 8689): headers and coordinates.
- ISelectionProvider2 is NOT implemented anywhere in the UIA layer. SelectionPattern2 adds FirstSelectedItem, LastSelectedItem, CurrentSelectedItem, ItemCount — what Narrator uses for rich N-of-M selection summaries and traversal.

Verdict: the scenario is functionally expressible today through SelectionItem plus Selection v1 plus Table patterns with two-way actions. SelectionPattern2 is a genuine BROWSER-WORK item, and small: GetSelectedItems already enumerates the selection, so the four properties derive from existing data. Ideal overlay patch and a strong standards exhibit: the platform API outruns what the browser surfaces.

OPEN: when text selection inside a cell (tree-data selection, Text pattern) coexists or transitions with cell-level selection (selected attributes, SelectionItem pattern), what should ATs receive and in what order? Both mechanisms exist independently; the handoff is the test-matrix item T2-7.

## Scenario inventory (to be graded)

Text core: character, word, line, paragraph insert and delete; undo and redo (kHistoryUndo, kHistoryRedo intents exist); autocorrect and autoformat replacement (kInsertReplacementText; see 09 T2-1, T2-5); IME composition (09 finding 2); rich attribute editing bold, italic, underline, strikethrough, super and subscript, justification, indent (kFormat intents exist for all); find and replace; spellcheck markers and suggestion menus (Command kMarker; UIA annotations and RangeFromAnnotation).

Tables (Loop tables and boards, Word tables): cell text editing (Text pattern inside cell); single cell, row, column, rectangular range, discontiguous cells (graded above); add, delete, reorder rows and columns; column hide; sort and filter (Loop ships filters, sorting, hidden columns, formula columns); header announcement during navigation (TableItem present).

Comments and annotations (Loop cell comments, Word comments): comment anchored to a text range (RangeFromAnnotation in ITextProvider2; annotation roles exist); comment anchored to a table cell or board field (Loop ships this via right-click cell, New comment, comment icon on the cell); threaded replies, edit and delete own comment, keyboard shortcuts into the comment pane; mentions in comments and body.

Collaboration presence: remote cursors and remote selections; attribution of who edits what region; live remote changes arriving while local caret is mid-edit.

Structured and embedded content: task lists with checkable items, progress trackers, voting tables; code blocks; links; dates; embedded components inside text flow (ITextChildProvider territory); kanban boards (selection plus drag reordering).

## Next actions when picked up

1. Grade every row SUPPORTED, BROWSER-WORK, or OPEN with file citations, same method as section 09.
2. Prototype the SelectionPattern2 patch as an overlay change; measure size.
3. T2-7 covers the cell-versus-text selection handoff empirically.
4. Dump live UIA trees of Loop and Word Online tables during the multi-cell scenario for the current-ceiling baseline (Tier 3).
