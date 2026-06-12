# Matrix Extension — Linux Generated-Event Results (docs/10 rows + T2-7 first half)

Date: 2026-06-12. Same binary, environment, and logging contract as results/T2-linux-generated-events.md (provenance: results/ENVIRONMENT.md; source: tests/linux-t2/ax_matrix_t2_unittest.cc). **9/9 tests pass; combined suite including the original T2 tests and the 87-test upstream baseline: 104/104.**

## T2-7, generated-event half — ANSWERED at this layer

**Single + discontiguous cell selection** (grid → row → cells; `kSelected` attribute):

```
selectedChanged on node 4            # the cell
selectedChildrenChanged on node 2    # the grid (selection container walk)
winIaccessibleStateChanged on node 4
```
Second cell selected while the first stays selected (discontiguous) produces the identical shape on the new cell — per-item events plus one container event each time.

**The handoff** — one atomic update moving the document selection out of in-cell text (anchor/focus in static text) onto the cell node, while marking the cell `kSelected=true`:

```
documentSelectionChanged on node 1
selectedChanged on node 4
selectedChildrenChanged on node 2
winIaccessibleStateChanged on node 4
```

Findings for docs/10's OPEN question: the two selection mechanisms **compose cleanly in a single event set — no collision, no suppression, no ordering hazard at the generation layer**. And `textSelectionChanged` correctly does NOT fire once the selection focus leaves the text field (no text-field ancestor). What remains for the Windows VM is only the UIA translation ordering and what NVDA announces — the generation layer is now a known quantity.

## docs/10 rows converted from graded-by-reading to verified-by-test

| Row | Result | Event log |
|---|---|---|
| Spellcheck markers | **PASS** | `spellingMarkerChanged` + `textAttributeChanged`, both on the **text-field ancestor** (node 6), not the text leaf |
| Rich formatting change (bold) | **PASS + contract finding** | `textAttributeChanged` on the text node — but **only with `State::kRichlyEditable` on it; with plain `kEditable` ancestry: zero events** (negative case verified) |
| Sort | **PASS + gating verified** | `sortChanged` on the column header; same attribute on a plain cell: zero events (source-gated by `IsTableHeader`) |
| Add rows | **PASS** | `childrenChanged` on table + `subtreeCreated` on the new row; confirmed **no row-count-attribute event exists** in the generator (no `kTableRowCount` case) — counts are announced structurally or re-queried |
| Checkable task items | **PASS** | `checkedStateChanged` on the checkbox |
| Progress trackers | **PASS** | `rangeValueChanged` on the progress indicator |
| Comment thread collapse | **PASS** | `collapsed` + `stateChanged` on the comment node |
| Kanban reorder (pos-in-set) | **PASS** | `positionInSetChanged` on the moved item |

## New bridge-contract requirement (feeds section 03 and NEXT-QUESTIONS)

**Formatting-change events require `State::kRichlyEditable` on the changed node itself.** docs/09/10 stated the role/editable-state constraint for the *field ancestor* (T2-2); this run shows text-attribute events have a second, node-level gate. A canvas producer that wants format-change announcements must set `kRichlyEditable` on its text nodes (matching what Blink does for contenteditable descendants), not just `kEditable` on the field root.

Also note for the marker row: marker events target the **field ancestor**, consistent with the editable-text events — one more confirmation that the field root is the announcement anchor for everything text-related.

## Reproduction

```
out/rel/ax_t2_unittests --gtest_filter='AXMatrixT2Test.*' --single-process-tests
out/rel/ax_t2_unittests    # full 104-test suite
```
