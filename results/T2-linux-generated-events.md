# T2 Linux Generated-Event Results — T2-1, T2-2, T2-3, T2-5

Date: 2026-06-12. Binary: `ax_t2_unittests`, built from Chromium tag 149.0.7827.115 (provenance and full modification ledger: results/ENVIRONMENT.md; test sources and build config preserved in tests/linux-t2/). Layer under test: `AXEventGenerator` — the cross-platform engine that, per docs/09 Finding 1, drives the Windows UIA event surface. No event intents were supplied in any test; everything below is produced purely by `AXTreeUpdate` deltas and `AXTreeData` changes.

**Soundness baseline: the same binary runs upstream's full `AXEventGeneratorTest` suite — 87/87 pass.** Our harness reproduces all of upstream's expected behavior before adding the T2 scenarios.

## Verdict summary

| Task | Claim under test (docs/09) | Result |
|---|---|---|
| T2-1 | Text deltas alone fire editable-text events on the field ancestor, no intents needed | **PASS** (3 edit shapes) |
| T2-2 | Without an editable text-field ancestor, editable-text events stop | **PASS** |
| T2-3 | Caret and selection via AXTreeData sel_ fields fire selection events (incl. backward, affinity-only) | **PASS** (4 shapes) + caret-bounds correction pinned |
| T2-5 | One atomic update carrying text delta + selection produces a single coherent event set, no duplicates | **PASS** |

Tree shape in all tests: `root(1, kRootWebArea) → field(2) → staticText(3 …)`, field(2) being an atomic text field (role kTextField + State::kEditable) except in T2-2.

## T2-1 — Delta-only editing events: PASS

**Character insert** (name "hello" → "hellox" on node 3, single-node update):

```
editableTextChanged on node 2
nameChanged on node 3
valueInTextFieldChanged on node 2
```

**Word delete** (remove static-text child node 4 "world" by updating field's child_ids):

```
childrenChanged on node 2
editableTextChanged on node 2
valueInTextFieldChanged on node 2
```

**Autocorrect-shape replacement, atomic** (one update: node 3 name "teh" → "the " AND tree-data caret 3→4):

```
documentSelectionChanged on node 1
editableTextChanged on node 2
nameChanged on node 3
textSelectionChanged on node 2
valueInTextFieldChanged on node 2
```

All five events arrive in the single event set of one `Unserialize` pass — the announcement-coherence shape a canvas producer needs for autocorrect.

## T2-2 — Text-field ancestor requirement: PASS

Identical character-insert delta, but node 2 is `kGenericContainer` without `State::kEditable`:

```
nameChanged on node 3
```

No `editableTextChanged`, no `valueInTextFieldChanged`, no `textSelectionChanged` anywhere in the set. Validates the section 03 constraint: the canvas document subtree root MUST carry an editable text-field role/state or all editing semantics silently vanish.

## T2-3 — Selection and caret via AXTreeData: PASS

Four consecutive tree-data-only deltas on "hello world" (anchor/focus on node 3); every shape produced exactly:

```
documentSelectionChanged on node 1
textSelectionChanged on node 2
```

Shapes verified: collapsed caret move (0→5); forward range (5→11); backward range (anchor 11, focus 5, sel_is_backward=true); **affinity-only change** (identical offsets, sel_focus_affinity downstream→upstream — the wrapped-line caret case). The affinity-only result matters: a producer can drive wrapped-line caret distinctions purely through tree data and the event still fires.

**Correction to docs/09 pinned by test:** `CARET_BOUNDS_CHANGED` is NOT produced by sel_ field changes. It fires from the `kCaretBounds` IntListAttribute (`OnIntListAttributeChanged`, ax_event_generator.cc:920-921):

```
caretBoundsChanged on node 2     # after AddIntListAttribute(kCaretBounds, {10,4,1,12})
```

Bridge-contract consequence: caret bounds are a separate producer obligation (node attribute), not a free side effect of selection tree-data. Section 03's bridge API needs an explicit caret-bounds field.

**Addition to docs/09:** selection changes fire TWO events — `DOCUMENT_SELECTION_CHANGED` at the root and `TEXT_SELECTION_CHANGED` on the text field containing the selection focus (ax_event_generator.cc:945-960). Which one `BrowserAccessibilityManagerWin` translates to `UIA_Text_TextSelectionChangedEventId` is a Windows-VM verification item (NEXT-QUESTIONS #2).

## T2-5 — Atomic multi-part update: PASS

One update carrying: delete static-text node 4 ("teh"), insert node 5 ("the"), move tree-data caret to node 5 — the structural form of a real autocorrect:

```
childrenChanged on node 2
documentSelectionChanged on node 1
editableTextChanged on node 2
subtreeCreated on node 5
textSelectionChanged on node 2
valueInTextFieldChanged on node 2
```

All six in one event set from one `Unserialize` pass, and **`editableTextChanged` appears exactly once** despite both the deletion path (`OnSubtreeWillBeDeleted`) and creation path (`OnNodeCreated`) calling `FireValueInTextFieldChangedEventIfNecessary` — the generator deduplicates per node within a pass. No interleaved partial states are observable.

## What these results do NOT show

The UIA finalize layer (UIA_Text_TextChangedEventId / TextSelectionChangedEventId emission, Text-pattern gating at browser_accessibility_manager_win.cc:1300-1314) runs only on Windows. These results de-risk the generated-event layer it consumes; T2-1..T2-5 must still be confirmed end-to-end with `ax_dump_events` on the TASK-00 VM, plus T2-4 (composition) and T2-6/T2-7 which are Windows-only by nature.

## Reproduction

```
out/rel/ax_t2_unittests --gtest_filter='AXEventGeneratorT2Test.*' --single-process-tests
out/rel/ax_t2_unittests --gtest_filter='AXEventGeneratorTest.*'   # 87/87 upstream baseline
```

Test source: tests/linux-t2/ax_event_generator_t2_unittest.cc (drop into ui/accessibility/ and apply tests/linux-t2/ui-accessibility-BUILD.gn.patch; args in tests/linux-t2/args.gn). On a full Windows checkout the same file is registered in `accessibility_unittests` by the patch, so it runs under the canonical target there.
