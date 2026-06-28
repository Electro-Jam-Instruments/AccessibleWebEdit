# 18 — Caret, selection & table-editing model (making it feel like a real editor)

**Purpose.** Specify exactly what arrow keys, Shift, Delete/Backspace, and typing should
do — in body text **and** inside a table — and how each state maps to the accessibility
layer so NVDA announces it correctly. This is the spec to implement against; it replaces
the current "cell-cursor only" stopgap (commit 12e0ac8) with a real text-caret-in-cell +
unified Shift-selection model.

Grounded by a source dive of `C:\src\chromium\src` (tag 149) — see **Part C**. Key result:
**text selection and cell selection are two INDEPENDENT a11y mechanisms the producer drives;
the editor decides which one is live based on whether the selection stays inside one cell.**

---

## Part A — Text editing (body, no table). The baseline every editor shares.

The caret is one position. A selection is an **anchor** (fixed end) + a **focus/caret**
(moving end); collapsed when anchor == focus. "Desired column" is remembered for vertical
moves so Up/Down through a short line then a long line keeps the column.

| Key | No modifier | + Shift (extend selection) | + Ctrl |
|---|---|---|---|
| **Left / Right** | move caret 1 grapheme; if a selection exists, collapse to its left/right edge | move focus 1 grapheme, keep anchor | move by **word** boundary |
| **Up / Down** | move caret 1 line at desired column; collapse selection | move focus 1 line, keep anchor | (scroll / paragraph — out of scope) |
| **Home / End** | caret to line start / end | extend to line start / end | Ctrl+Home/End = doc start / end |
| **Backspace** | delete grapheme before caret; **if selection, delete it** | — | Ctrl+Backspace = delete previous word |
| **Delete** | delete grapheme after caret; **if selection, delete it** | — | Ctrl+Delete = delete next word |
| **printable / Enter** | **if selection, replace it**, then insert | — | — |

Rules that matter:
- **Shift sets/keeps the anchor.** First Shift+arrow from a collapsed caret drops the anchor
  at the caret, then moves the focus. A non-Shift move **collapses** (drops the selection).
- **Typing/Delete/Backspace with a non-empty selection replaces/removes the selection first**
  (and the caret lands where the selection was).
- Left/Right with an active selection but no Shift collapses to the **near edge** (Left→start,
  Right→end), it does not move past it. (Word/Docs/browsers all do this.)

Current prototype gap vs. this table: no Shift handling at all; no word moves; Backspace/Delete
don't act on a selection (there is no selection yet); Home/End are buffer-wide not line-wide.

---

## Part B — Tables. The part that needs care.

Model: a document that *contains* a table (Google Docs / Word model), **not** a spreadsheet.
So a cell holds editable text and the caret lives **inside the cell's text**, exactly like a
mini text field. Two distinct selection *modes* exist and the editor switches between them.

### B.1 Caret movement inside / across cells (no Shift)
- The caret is inside cell text. **Left/Right/Up/Down/Home/End behave like Part A _within the
  cell's own text_.**
- **Crossing a cell boundary:** when a move would leave the cell (Right at end-of-cell-text,
  Left at start, Down past the cell's last line, Up past its first), the caret moves to the
  **adjacent cell** and lands at the natural edge (enter from the left → caret at that cell's
  text start; from below → end; etc.). Column is preserved for Up/Down where possible.
- **Entering the table from the body** (Down past the last body line) → caret into the first
  cell's text at offset 0. **Leaving** (Up from the top row, or Left/Up out the top-left) →
  back into the body. (Today we enter at cell-cursor level; upgrade to *caret in the cell text*.)
- Tab / Shift+Tab = next / previous cell (whole-cell jump) — the conventional table key; worth
  adding alongside arrows.

### B.2 Selection with Shift — the text↔cell transition (the user's question)
This is the crux. The behavior real editors (Docs, Word) use:

> **A Shift-selection is a TEXT selection while anchor and focus stay in the SAME cell. The
> moment the focus crosses into a DIFFERENT cell, the selection PROMOTES to a CELL-BLOCK
> selection** — the rectangle of whole cells from the anchor's cell to the focus's cell —
> and partial text highlighting is dropped.

Worked example (anchor starts mid-text in cell B1 "Apples"):
- `Shift+Right` a few times → still in B1 → **text selection** "Appl" highlighted (partial cell).
- One more `Shift+Right` past the end of B1, or `Shift+Down` into B2 → focus is now in a
  different cell → **promote**: whole **B1 and B2** cells are selected (cell-block), the
  partial-text highlight disappears.
- Continue `Shift+Down/Right` → the rectangle grows by whole cells (anchor-cell ↔ focus-cell).
- `Shift` back so focus returns into the anchor's cell → **demote** to a text selection again.

So the "specific point" where text-selection becomes cell-selection is **every cell boundary
the focus crosses**: same-cell ⇒ text, different-cell ⇒ cells. The anchor cell + focus cell
define a rectangular block (min/max row, min/max col).

A selection that spans **from body text into the table** (anchor in a paragraph, focus in a
cell): editors normally **snap the whole table into the block** (you can't half-select a table
mid-cell from outside). Simplest correct rule for us: if anchor is in body and focus in a cell
(or vice-versa), select [anchor text-run … whole table up to/including the focus cell's row].
For v1 we can restrict cross-body↔table Shift to "select to the table edge" and keep in-table
Shift as the cell-block model. (Decision D3 below.)

### B.3 Delete / Backspace / typing in a table
- **Caret in cell text, no selection:** Backspace/Delete edit the **cell's text only**.
  **Backspace at cell-text start is a NO-OP** and **Delete at cell-text end is a NO-OP** — you
  must not merge/delete across cell boundaries (a hard difference from body text, where
  Backspace at line start joins lines). This keeps the table structure intact.
- **Text selection inside a cell:** Backspace/Delete/typing replace it (Part A), bounded to the
  cell.
- **Cell-block selection:** Delete/Backspace **clears the selected cells' contents** (empties
  them) but does **not** remove rows/columns. Typing replaces the first/anchor cell's content
  (or is a no-op — Decision D4). Removing rows/columns/the table is a menu/command action, not
  a bare Delete.

---

## Part C — How this maps to the accessibility layer (source-grounded)

From the `ui/accessibility` dive (file:line in the research log; summary here):

1. **Text selection** lives in `AXTreeData` as `sel_anchor_object_id/offset` +
   `sel_focus_object_id/offset` (one global selection). `AXSelection` mirrors it. Anchor and
   focus may be **any** nodes — including anchor in a paragraph and focus inside a table cell;
   there is **no structural constraint** and **no special table handling**. UIA exposes it via
   `ITextProvider::GetSelection` → an `ITextRangeProvider` (anchor→focus). NVDA reads it as the
   text selection ("selected … <text>").

2. **`AXPosition` crosses cell/table boundaries freely** — depth-first pre-order traversal, no
   `AtCellStart`/`IsInCell` notion. So *we* must impose cell-boundary semantics in the editor
   (stop at the boundary, promote to cell selection); the a11y layer won't do it for us.

3. **Cell selection** is **per-node** `BoolAttribute::kSelected` on each cell, surfaced by
   `ISelectionProvider`/`ISelectionProvider2` + `ISelectionItemProvider` (our 4.2 patch:
   `patches/iselectionprovider2-grid-cells.patch`). NVDA reads it as cell selection.

4. **The two are fully INDEPENDENT.** Setting a text range that happens to cover cells does
   **not** auto-mark those cells `kSelected`, and vice-versa. **The producer (our editor) must
   choose and drive exactly one of the two per the Part B rule:**
   - **In-cell or in-body text selection** ⇒ set `AXTreeData` anchor/focus (text range); leave
     all `kSelected=false`. Fire `kTextSelectionChanged` (+ `kDocumentSelectionChanged`).
   - **Cell-block selection** ⇒ set `kSelected=true` on the rectangle's cells; collapse the
     text selection to the anchor (degenerate). Fire `kSelectedChildrenChanged` on the grid /
     `kSelection`/`kSelectionAdd`/`kSelectionRemove` on cells (and update `kFocus` to the focus
     cell). Leave the text range degenerate so NVDA doesn't double-announce.

   Mapping the editor's selection-mode to "which mechanism is live" is the whole game.

---

## Part D — Concrete design for AccessibleWebEdit

### D.1 Unified caret/selection state (editor model)
Replace `caret_` + the ad-hoc `in_table_/tr_/tc_` cursor with a **Position** + **anchor**:

```
struct Pos {                 // a single caret location, body OR in a cell
  bool in_table = false;
  // body: char offset into text_
  int  body_off = 0;
  // table: which cell + char offset into that cell's text
  int  row = 0, col = 0, cell_off = 0;
};
Pos   caret_;                // the moving end (focus)
Pos   anchor_;               // the fixed end; == caret_ when collapsed
bool  selecting_;            // a Shift-drag is active (anchor pinned)
int   desired_col_;          // for Up/Down
```
- **Selection mode is DERIVED**, not stored: `text` if `!anchor.in_table && !caret.in_table`
  (both body) OR `anchor.in_table && caret.in_table && same cell`; else `cells` if both in
  table different cells; else `mixed` (body↔table) → treat per D3.
- Movement keys update `caret_` (and, if not Shift, set `anchor_ = caret_`). Cell-boundary
  logic lives in `Pos`-advancing helpers (`MoveRight` stops at cell end, etc.).

### D.2 Key-handling spec (what to wire in WndProc)
Read modifier state (`GetKeyState(VK_SHIFT/VK_CONTROL) < 0`). For each arrow/Home/End:
1. compute the new focus Pos (cell-boundary aware);
2. if **not** Shift → collapse (`anchor_ = caret_`); if Shift → keep `anchor_`, set `selecting_`;
3. `RequestSync(text_changed=false)`.
Backspace/Delete/printable: if selection non-empty → delete-range first (text or clear-cells per
mode), then the edit; `RequestSync(true)`.

### D.3 Bridge: emit the live selection per mode (the important part)
In `BuildTree`, after building nodes, compute the mode from (anchor_, caret_):
- **text mode** → map anchor_ & caret_ to (node id, offset): body → that line's inline box;
  in-cell → that cell's inline box. Set `sel_anchor_*`/`sel_focus_*`. Clear all cells'
  `kSelected`. `focus_id` = the focus node (cell or field).
- **cells mode** → for every cell in the rectangle [min(row),max(row)]×[min(col),max(col)] set
  `kSelected=true`. Set `sel_*` to a **degenerate** range at the focus cell. `focus_id` = focus
  cell. (Highlight all block cells in WM_PAINT.)
- **mixed (body↔table)** (D3 decision): v1 = clamp the focus to the table boundary (treat as
  text selection up to the first cell, or promote the whole table to cells) — pick the simpler
  to ship and note it; full partial cross-in selection is a later refinement.

### D.4 Events (so NVDA actually announces the change)
`ApplyLocalEdit` currently fires field `kTextSelectionChanged` (+ cell `kFocus` from 12e0ac8).
Extend: fire `kTextSelectionChanged`+`kDocumentSelectionChanged` in **text mode**; fire the
grid `kSelectedChildrenChanged` and the focus cell `kFocus` in **cells mode**; keep the
"focus back to field on table exit" transition. Decision D4: typing with a cell-block selected
= clear contents then place caret in the anchor cell (don't insert across cells).

### D.5 Visual (one layout pass, stays coupled)
- text selection: paint a highlight behind the selected glyph run(s) (body or within a cell),
  from anchor offset to focus offset, using the same per-glyph rects `LayOut` already produces.
- cell-block selection: fill every block cell with the selection color (extend today's single-
  cell highlight to the rectangle). Caret bar only when collapsed/in text.

### D.6 Suggested phasing (each: infra → probe → NVDA → visual, like the rest of the repo)
1. **Text caret INSIDE a cell** (replace cell-cursor with cell_off; Left/Right/Home/End within a
   cell; boundary cross to adjacent cell). Probe: caret range inside the cell; NVDA reads chars.
2. **Shift-selection in body text** (anchor/focus, highlight, Backspace/Delete/replace). Probe:
   GetSelection range non-degenerate; NVDA "selected …".
3. **Shift-selection inside one cell** (same as body, bounded to the cell).
4. **Promote to cell-block** on boundary cross (mode flip; set kSelected rectangle; collapse
   text range; grid event). Probe: GetSelection (cells) returns the block; NVDA announces cells.
5. **Delete/Backspace semantics** (no-op at cell edges; clear-cells on block; word moves via
   Ctrl). 6. **Tab/Shift+Tab** cell jumps. 7. **mixed body↔table** per D3.

---

## Resolved decisions (user, 2026-06-28)
- **D1 — desired-column: YES.** Preserve the desired column best-effort on Up/Down, including
  across cell boundaries.
- **D2 — enter-from-body target cell: column-match.** Entering the table (Down past the last
  body line) lands in the cell nearest the body caret's column (consistent with D1), not always
  top-left. (Interpreting the "yes" + the column-preservation preference; trivially falls back
  to top-left when ambiguous.)
- **D3 — selection across the table edge:**
  - **From OUTSIDE in** (anchor in body, Shift-extend into the table) ⇒ **CELL selection** —
    select whole cells (snap to the cell rectangle), never a half-selected cell from outside.
  - **Starting INSIDE the table** ⇒ **TEXT selection within the cell until the focus crosses a
    cell boundary**, then **promote to CELL selection** (the anchor-cell↔focus-cell rectangle).
    This is the core same-cell⇒text / cross-cell⇒cells rule.
- **D4 — Delete/typing on a selection: clear what is selected.** Text selection ⇒ delete the
  text. CELL (block) selection ⇒ **clear the contents of the selected cell(s)** (empty them;
  keep the rows/cols). Then place the caret at the anchor cell.
- **D5 — Tab in a table = the WORD model** (researched: Microsoft Word — Tab navigates cells,
  not a tab char; sources below):
  - **Tab** ⇒ move to the **next cell** and **select that cell's entire text** (a whole-cell
    TEXT selection; typing replaces it). Order: left→right, then first cell of the next row.
  - **Shift+Tab** ⇒ move to the **previous cell** and select its text; no-op at the first cell.
  - **Tab in the LAST cell** ⇒ **append a new row** and put the caret in its first cell.
  - **Ctrl+Tab** ⇒ insert a literal **tab character** inside the current cell.
  - (Google Docs differs — Tab just places the caret at the next cell's start without selecting;
    we follow Word since the user asked for the Word behavior. The whole-cell text selection on
    Tab maps cleanly to our same-cell text-selection mode and NVDA reads "‹cell text›, selected".)

  Sources: [Set tabs in a table — Microsoft Support](https://support.microsoft.com/en-us/office/set-tabs-in-a-table-838918a7-b279-454b-b2c0-9dd10b19c984),
  [Entering Tabs in a Table — WordTips](https://wordribbon.tips.net/T012932_Entering_Tabs_in_a_Table.html),
  [How to insert a tab character in a Word table — Technolex](https://technolex.com/knowledge-hub/how-to-insert-a-tab-character-in-a-word-table/).

## Bottom line
Make the caret a real text position that can live inside a cell, drive ONE of the two
independent a11y selection mechanisms based on a single rule (**same-cell ⇒ text range;
cross-cell ⇒ cell block**), and fire the matching event. That single rule — applied at every
cell boundary the Shift-focus crosses — is what makes the table "feel like a real editor" to
both a sighted user and NVDA.
