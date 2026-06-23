# Review Backlog — what's resolved, what's still open

Date: 2026-06-16. The complete, deduplicated, status-tagged list derived from the three expert reviews (`AGGREGATE.md` and the per-expert reports). Numbers in parentheses reference AGGREGATE.md findings. This is the living tracker of "anything else to review."

Legend: **[DONE]** fixed this session · **[VM]** can only close on the Windows VM · **[DECISION]** strategic/planning judgment, no clean technical answer · **[SPIKE]** needs an investigation before it can be decided.

## Resolved this session (the "fixable now" list)

- **[DONE] F1 — §1.1 role-only correction + isolation tests (HIGH #3, LOW #24).** docs/03 §1.1 rewritten: the editable-root gate is the text-field **role**, not `State::kEditable`. Added and ran two isolation tests — **T2-2b** (drop role, keep `kEditable` → events vanish) and **T2-2c** (keep `kTextField`, drop `kEditable` → events still fire). Now empirically **[PROVEN]**, suite 109/109. (tests/linux-t2/ax_event_generator_t2_unittest.cc; results/matrix-linux-generated-events.md.)
- **[DONE] F2 — autocorrect & IME re-graded (HIGH #2).** docs/10 downgraded both from SUPPORTED to **BROWSER-WORK / verify**: real announcement rides the composition-only `UIA_TextEdit_TextChangedEventId` path (early-returns on commit, gated on `HasEventListenerForEvent`), not the generic delta path. Added a grade caveat (SUPPORTED-at-generation ≠ SUPPORTED-for-user).
- **[DONE] F3 — text-attribute-changed patch fixed (HIGH #4).** Now uses `EnqueueTextChangedEvent(*wrapper)` (Text-pattern-resolved) instead of the wrong raw `text_changed_nodes_.insert(wrapper)`.
- **[DONE] F4 — §2.2 producer obligation added (MEDIUM #11).** The field node must advertise `UIA_TextPatternId` or the selection event is dropped at finalize; corrects NEXT-QUESTIONS #2's "root vs field" framing (it's both, gated on Text-pattern support).
- **[DONE] F5 — overstated language softened (HIGH #1 framing, MEDIUM #9).** 00-INDEX/docs/03/docs/10/docs/13 no longer say "PROVEN fidelity / Options SETTLED / permanently capped." Now: "event-layer validated; fidelity pending VM," and "capped today (HTML-in-Canvas `drawElement` could lift it)."
- **[DONE] F6 — ITextProvider2 patch corrected (MEDIUM #13).** Labeled a sketch; recorded the two API fixes (real factory `CreateIUnknown` not `PatternProvider<>`; canonical gating predicate `IsPlatformDocument()||IsTextField()||IsText()`).
- **[DONE] F7 — test count reconciled (LOW #22).** Authoritative via `--gtest_list_tests`: **109** (21 project: 9 `AXEventGeneratorT2Test` + 12 `AXMatrixT2Test`; + 88 upstream baseline). All "104/107" references updated.
- **[DONE] F8 — empty namespace block removed from SelectionProvider2 patch (LOW #23).**
- **[DONE] F9 — quick doc caveats added:** 80/20 split is an estimate not a derivation (HIGH #5, docs/11); mobile fidelity likely not parity (MEDIUM #21, docs/11); PDF precedent not locally reproducible from the lean checkout (MEDIUM #17, docs/09 Finding 3).
- **[RESOLVED/moot] — stale `content/` patch path (MEDIUM #14).** Investigated: the patches cite the **bare filename**, which resolves to `ui/accessibility/platform/` at tag 149. The reviewer's path concern did not hold; no change needed.

## Open — VM-gated verification (cannot close on Linux)

- **[VM] V1 — the falsifying end-to-end slice (HIGH #1, #2, #7).** Run one insert + one caret move through `AXPlatformNodeWin` → UIA → NVDA on a real Windows box. Treat *this*, not the generated-event tests, as the moment the fidelity thesis is proven or broken. Until then, "native delivers real fidelity" stays unproven.
- **[VM] V2 — the existing queue (NEXT-QUESTIONS #5–7):** UIA finalize for T2-1/2/3/5 under `ax_dump_events`; T2-4 composition round-trip + no-double-announce; T2-6 action coverage; T2-7 UIA ordering.
- **[VM] V3 — `ITextRangeProvider` conformance probe (HIGH #7).** A raw UIA client walking ranges by char/word/line and diffing against expected text — independent of NVDA. Caret correctness for a screen reader = `ITextRangeProvider` correctness, currently untested and not in the bridge contract.
- **[VM] V4 — real-typing + read-back + braille capture (HIGH #7).** Drive actual typing through NVDA's keyboard hook (not just SendKeys/synthetic UIA), exercise review-cursor/say-all, and capture **braille** output (caret in the cell flow + cursor-routing round-trip via `ITextRangeProvider::Select`). Braille is currently absent from the plan entirely.
- **[VM] V5 — JAWS pass (MEDIUM, B).** Add JAWS for the editing core; the provider has JAWS-specific workarounds and a different forms/virtual-cursor model. Narrator separately is the AT that wants SelectionPattern2.
- **[VM] V6 — coalescing-under-load (MEDIUM #15, B).** Stress scenario: interleaved local+remote edits and rapid wrapped-line affinity caret moves; assert the speech log *under load*, since NVDA coalesces UIA events.
- **[VM] V7 — live-region NVDA caveats (MEDIUM #15).** Confirm the producer populates `UIA_LiveSettingPropertyId` (not just fires the event); test rapid-update coalescing; pin window-foreground (NVDA gates live regions on foreground).
- **[VM] V8 — RDP vs console artifact (MEDIUM #15).** Confirm key results on a console session, not only over RDP (RDP can change UIA delivery/focus). N/A if using the local MS-01 (TASK-00b).
- **[VM] V9 — PDF precedent re-verification (MEDIUM #17).** Re-confirm docs/09 Finding 3 line citations against a full checkout (the lean checkout lacks `components/pdf/`).
- **[VM] V10 — zero-deviation canonical run (NEXT-QUESTIONS #11).** Re-run the 109 tests under the canonical `accessibility_unittests` on a full checkout (free on the VM / TASK-00b box).

## Open — strategic / planning decisions

- **[DECISION] C1 — Strategy A (in-tree) vs B (standalone), with a real cost model (HIGH #6, MEDIUM #8).** The *proven* artifact is the standalone B-lite spine (Strategy B works), yet docs recommend in-tree A. Before committing to A, build a maintenance model: security-update rebase cadence vs a frozen fork; the four patches as permanent rebase liabilities unless upstreamed; the true size of the GPU/viz/Ozone dependency cone (open decision #4). Resolve the A-vs-B contradiction.
- **[DECISION] C2 — "Why canvas at all, for an a11y-first product?" (MEDIUM #20).** A DOM/contenteditable editor gets the full Text pattern free via Blink and has no mirror-DOM ceiling; the entire IME + native-shell + in-tree-build cost exists *because* of the canvas choice. Make this premise explicit and defend it (or revisit).
- **[DECISION] C3 — product vs standards as the primary goal (MEDIUM #19).** They're in timeline tension: if native ships and works, the standards motivation weakens; if standards is the goal, the heavy Strategy-A build is an expensive PoC. Decide which is primary.
- **[DECISION/VM] C4 — measure native-vs-mirror-DOM head-to-head (MEDIUM #10).** The "native is superior" claim is asserted, not measured. Compare native (AXPlatformNode) vs a mirror-DOM canvas editor through a real screen reader before claiming superiority. (Needs the VM for the native half.)
- **[DECISION] C5 — Win11 Azure licensing attestation (NEXT-QUESTIONS #12).** Confirm eligible per-user licensing before any Azure deploy. (Moot if using the local MS-01.)

## Open — needs a spike (investigation before decision)

- **[SPIKE] S1 — SkParagraph a11y-geometry granularity (MEDIUM #16, docs/11 open #2).** Determine whether `SkParagraph` exposes per-char/word/line bounds at the granularity UIA Text + braille need. If not, the "one layout engine" constraint forces a custom HarfBuzz layout — which invalidates the "80% shared / well-trodden Skia text" assumption (S1 gates the C1 cost model).
- **[SPIKE] S2 — ariaNotify platform coverage (LOW #4 / MEDIUM #15).** Confirm the macOS "method exposed but not reliably spoken" status and decide the announcement fallback for platform #2.

## How to use this list

- The **[DONE]** items are closed; verify in the diff if needed.
- The **[VM]** items are the real verification gate — V1 first; everything screen-reader-observable hangs off the VM (TASK-00 or, recommended, the local TASK-00b box).
- The **[DECISION]** items are for the planning session — none are blocked on engineering, and C1/C2 are the two that could most change the project's direction.
