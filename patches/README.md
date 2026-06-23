# Prototype patches — BROWSER-WORK standards exhibits

These are the "small overlay patch" items from results/NEXT-QUESTIONS.md #8 and docs/10: cases where the **Windows UIA platform API defines a capability that Chromium does not surface**. Each is written against Chromium tag 149.0.7827.115 as a reviewable diff with a measured size. The shared thesis (docs/10): *the platform API outruns what the browser exposes, and the gaps are small.*

All four are in `#if BUILDFLAG(IS_WIN)` code, so they **build and test on the TASK-00 Windows VM**, not in the Linux container (which holds the platform layer for the VM — see results/ENVIRONMENT.md). The deliverable here is each concrete diff + size + a VM verification checklist.

| Patch | Gap closed | Size | Touches schema? |
|---|---|---|---|
| selectionprovider2-prototype.md | `ISelectionProvider2` (FirstSelected/LastSelected/Current/ItemCount — Narrator N-of-M summaries) | ~84 lines, 2 files | no |
| itextprovider2-rangefromannotation-prototype.md | `ITextProvider2::RangeFromAnnotation` (navigate comment → annotated text range) + GetCaretRange | ~64 lines (optimistic — **sketch with placeholder bodies + 2 API corrections**, see its header), 3 files | no |
| text-attribute-changed-uia-prototype.md | UIA event for `TEXT_ATTRIBUTE_CHANGED` (formatting changes currently IA2-only) | ~2 lines, 1 file | no |
| annotation-author-datetime-prototype.md | `IAnnotationProvider::get_Author`/`get_DateTime` (empty stubs → real values) | ~8 lines, 2 files | **yes** (2 new StringAttributes) |

Total surface to close all four: roughly **160 lines across 6 files**, only one of which touches the cross-platform schema. That small total is itself the exhibit.

> **Expert review 2026-06-16 — corrections applied:** the `text-attribute-changed` patch now uses `EnqueueTextChangedEvent(*wrapper)` (an earlier draft used a wrong raw `text_changed_nodes_.insert`); the `ITextProvider2` patch is explicitly labeled a sketch with two API corrections (real factory is `CreateIUnknown`, not `PatternProvider<>`; canonical gating predicate); the `SelectionProvider2` patch dropped a dead empty-namespace block. `SelectionProvider2` and `annotation author/datetime` were confirmed API-accurate. One reviewer flag — that the patches cite a stale `content/...` path — did **not** hold: they cite the bare filename, which resolves to `ui/accessibility/platform/` at tag 149.

Recommended order to prototype on the VM (per NEXT-QUESTIONS #8 — pick 1-2 first):
1. **SelectionPattern2** — biggest user-visible win (multi-cell selection summaries), no schema change, cleanest demo against NVDA.
2. **annotation author/datetime** — tiny, but it's the one that exercises the producer→schema→UIA round-trip end to end, so it doubles as a bridge-contract (docs/03) validation.

The other two follow once the first two prove the workflow.
