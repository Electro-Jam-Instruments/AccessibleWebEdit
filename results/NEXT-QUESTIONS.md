# Open Questions for the Planning Session

Date: 2026-06-12. Consolidated from the Linux generated-event runs (results/T2-linux-generated-events.md), the docs/10 grading pass, and environment work. Ordered by how much they block the bridge design.

## Bridge contract (section 03) — design decisions needed

1. **Caret bounds are a separate producer obligation.** Pinned by test: `CARET_BOUNDS_CHANGED` fires from the `kCaretBounds` IntListAttribute, not from sel_ tree-data (docs/09 line-921 reading was wrong on this point). The bridge API needs an explicit caret-bounds field the editor must update on every caret move/scroll. Decide: per-update attribute, or computed host-side from selection + layout callbacks?

2. **Two selection events exist; which feeds UIA?** Tree-data selection changes fire both `DOCUMENT_SELECTION_CHANGED` (root) and `TEXT_SELECTION_CHANGED` (text field of selection focus). docs/09 cites the manager queueing DOCUMENT_SELECTION_CHANGED for `UIA_Text_TextSelectionChangedEventId`; the role of TEXT_SELECTION_CHANGED on Windows needs VM verification (it may be IA2/Views-only). Affects which node a UIA client sees the selection event on — root vs field.

3. **Remote cursors/selections have no schema at all** (docs/10 grading). AXTreeData carries exactly one self-selection; UIA has no non-self-caret concept either. Decide: model remote selections as highlight markers + AnnotationType_Highlighted (works today, semantically lossy) or propose new schema — this is a genuine standards-track item, arguably the strongest "the web platform is missing something" exhibit we have.

3b. **Formatting events need `State::kRichlyEditable` on text nodes** (new, verified by test 2026-06-12): text-attribute change events are gated on the changed node itself being richly editable — plain `kEditable` field ancestry produces zero events. Section 03 must require canvas text nodes to carry kRichlyEditable, mirroring Blink's contenteditable descendants. See results/matrix-linux-generated-events.md.

4. **Edit-origin marking does not exist.** A remote collaborator's text delta fires the identical EDITABLE_TEXT_CHANGED as local typing; nothing in AXTreeUpdate/AXEventGenerator carries origin. Decide whether the bridge should propose an origin field (standards-track) or the editor should moderate announcements app-side (live-region etiquette) — affects the Loop/Word-Online story directly.

## Windows VM verification queue (TASK-00 blocked items)

Note (2026-06-12): AT-level verification uses **NVDA speech logs**, not Narrator (planning decision; install/log mechanics researched and documented in docs/TASK-00a-nvda-verification.md). All "Narrator-observable" items below resolve to NVDA-observable via `--log-level=12` speech logging.

5. **UIA finalize end-to-end:** rerun T2-1/2/3/5 shapes under `ax_dump_events` on Windows; confirm UIA_Text_TextChanged / TextSelectionChanged emission and Text-pattern gating (browser_accessibility_manager_win.cc:1300-1314), including question #2 above.
6. **T2-4 composition:** OnActiveComposition → GetActiveComposition/GetConversionTarget round-trip and which UIA events accompany commit. Note found during grading: committed compositions deliberately defer to the standard text-changed path (ax_platform_node_win.cc:836-848) — verify no double announcement.
7. **T2-6 action coverage** (PdfAXActionTarget mirror) remains Windows-only. **T2-7's generation half is now answered** (results/matrix-linux-generated-events.md: the two selection mechanisms compose cleanly and atomically); only UIA ordering + NVDA announcements remain for the VM.

## docs/10 grading fallout — scoping decisions

8. **BROWSER-WORK shortlist for overlay patches** (all verified small): SelectionPattern2 (ISelectionProvider2 — zero hits in UIA layer); UIA notification for TEXT_ATTRIBUTE_CHANGED (currently IA2-only, browser_accessibility_manager_win.cc:681-683); ITextProvider2/RangeFromAnnotation (TextPattern2 in the not-implemented list — corrects docs/10's seeded assumption); IAnnotationProvider get_Author/get_DateTime (empty stubs). Decide which 1-2 to prototype first as the standards exhibit.
9. **Column hide:** no schema distinguishes hidden from deleted columns and no count event fires on Windows (ROW_COUNT_CHANGED is in the unused-events block). Tree-design question for the canvas producer.
10. **Filter state:** no AX/UIA vocabulary for "N of M rows visible." Decide: ItemStatus free-text, live region, or schema proposal.

## Environment / process

11. **Linux results provenance:** produced from the official release tarball + lean build config (full ledger in results/ENVIRONMENT.md, including a 3-line native_ui_types.h stub and 4 resolution-only assert shims — none in tested code). Decide whether to re-run the suite on the Windows VM's full gclient checkout for a zero-deviation confirmation record; the test file is already registered in the canonical `accessibility_unittests` target by the preserved patch, so this is nearly free once the VM exists.
12. **Win11 Azure licensing** (TASK-00): `--license-type Windows_Client` is a compliance attestation — confirm eligible per-user licensing (Windows E3/E5 or Microsoft 365 E3/E5/F3) before first deploy.
