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

- **[VM] V1 — the falsifying end-to-end slice (HIGH #1, #2, #7). MAJOR PROGRESS 2026-06-26 at the native-UIA-client level; NVDA leg remains.** A custom non-Blink tree now presents to a native `IUIAutomation`/CUIAutomation8 client (NVDA's own API) as a COMPLETE editable field — Edit type, value, keyboard-focusable+focused, Text pattern, degenerate caret range — and a *subscribed* client receives all four events (Focus→Edit, TextChanged, TextSelectionChanged, ValueChanged). See `results/V1-end-to-end.md`. STILL OPEN: the same run observed through **NVDA speech/braille on an unlocked desktop** (the UIAccess-signed AT), and client→provider **actions** (write) — the fidelity thesis is proven at the UIA-client layer but not yet at the AT layer.
- **[VM] V2 — the existing queue (NEXT-QUESTIONS #5–7):** UIA finalize for T2-1/2/3/5 under `ax_dump_events`; T2-4 composition round-trip + no-double-announce; T2-6 action coverage; T2-7 UIA ordering.
- **[DONE] V3 — `ITextRangeProvider` conformance (HIGH #7). RESOLVED 2026-06-26.** Range reads + cursor routing work with a native client: `DocumentRange.GetText()`='hello!', `Select()` hr=0x0 reaching `AccessibilityPerformAction(kSetSelection)` (selection round-trips). Root cause was the range **owner**, not `AXPosition`: `GetOwner()` needs a real `AXPlatformTreeManager` (`GetPlatformNodeFromTree`) — fixed via `BliteTreeManager` + `GetFromTreeIDAndNodeID` + an inline-text-box leaf. `scripts/uiaprobe_select.cpp`. (char/word/line granularity sweep + braille cursor routing under NVDA still pending = V4.)
- **[superseded] V3 (old PARTIAL note) — `ITextRangeProvider` conformance probe (HIGH #7).** A raw UIA client walking ranges by char/word/line and diffing against expected text — independent of NVDA. Caret correctness for a screen reader = `ITextRangeProvider` correctness. **Found:** `GetSelection` (read) returns the degenerate caret correctly (`scripts/uiaprobe.cpp` selRanges=1), but a client `ITextRangeProvider::Select()` (cursor routing) returns `0x80040201` = **`UIA_E_ELEMENTNOTAVAILABLE`** (corrected; not InvalidOperation) **before** reaching our `AccessibilityPerformAction(kSetSelection)` handler (which IS implemented + wired — `ApplyClientSetSelection`; the host never logs the apply). `scripts/uiaprobe_select.cpp` reproduces it. **Pinpointed:** `UIA_VALIDATE_TEXTRANGEPROVIDER_CALL()` rejects the call because the **DocumentRange's positions have null anchors** (`!start()->GetAnchor()`). The range is `get_DocumentRange` → `GetRangeFromChild(field, field)`; `GetSelection`'s range (anchored on the `kText` child) works fine. So the chase is the field's text-content anchoring for a whole-document `AXPosition` range in our standalone tree (hypertext/inline-text/child-offset setup), not `Select` itself. (Write-via-`SetValue` already round-trips — see V1.)
- **[VM] V4 — real-typing + read-back + braille capture (HIGH #7).** Drive actual typing through NVDA's keyboard hook (not just SendKeys/synthetic UIA), exercise review-cursor/say-all, and capture **braille** output (caret in the cell flow + cursor-routing round-trip via `ITextRangeProvider::Select`). Braille is currently absent from the plan entirely.
- **[VM] V5 — JAWS pass (MEDIUM, B).** Add JAWS for the editing core; the provider has JAWS-specific workarounds and a different forms/virtual-cursor model. Narrator separately is the AT that wants SelectionPattern2.
- **[VM] V6 — coalescing-under-load (MEDIUM #15, B).** Stress scenario: interleaved local+remote edits and rapid wrapped-line affinity caret moves; assert the speech log *under load*, since NVDA coalesces UIA events.
- **[VM] V7 — live-region NVDA caveats (MEDIUM #15).** Confirm the producer populates `UIA_LiveSettingPropertyId` (not just fires the event); test rapid-update coalescing; pin window-foreground (NVDA gates live regions on foreground).
- **[VM] V8 — RDP vs console artifact (MEDIUM #15).** Confirm key results on a console session, not only over RDP (RDP can change UIA delivery/focus). N/A if using the local MS-01 (TASK-00b).
- **[VM] V9 — PDF precedent re-verification (MEDIUM #17).** Re-confirm docs/09 Finding 3 line citations against a full checkout (the lean checkout lacks `components/pdf/`).
- **[VM] V10 — zero-deviation canonical run (NEXT-QUESTIONS #11).** Re-run the 109 tests under the canonical `accessibility_unittests` on a full checkout (free on the VM / TASK-00b box).

## Open — feature roadmap (new capabilities beyond the V1 read/caret core)

- **[FEATURE] R1 — rich-text run attributes (bold / italic / underline) + attribute-change detection (owner request 2026-06-26).**
  Make the editable surface expose character/run formatting through UIA TextRange attributes, and surface a
  *change* (e.g. bold → not-bold) so a screen reader announces it.
  - **UIA side:** `ITextRangeProvider::GetAttributeValue` for `UIA_FontWeightAttributeId` (bold = weight ≥ 700),
    `UIA_IsItalicAttributeId`, `UIA_UnderlineStyleAttributeId` (+ `UIA_StrikethroughStyleAttributeId`,
    `UIA_FontNameAttributeId`, color). Chromium's `AXPlatformNodeTextRangeProviderWin::GetAttributeValue`
    **already implements these** — so, as with the Text pattern, the work is feeding the AX node the data, not
    writing the provider.
  - **AX producer side:** populate the inline-text-style attributes on the text node(s): `ax::mojom::TextStyle`
    bitmask via `IntAttribute::kTextStyle` (kBold/kItalic/kUnderline/kLineThrough) and/or
    `FloatAttribute::kFontWeight`, `StringAttribute::kFontFamily`, color attrs. For mixed formatting within one
    field, split into multiple inline-text-box / static-text runs (one per attribute span) so a range query
    returns `Mixed` across the spans and the exact attribute within a span.
  - **Change detection:** on a formatting change, re-emit the run(s) and fire the text-attributes-changed path
    (`UIA_Text_TextChangedEventId`; investigate the richer `UIA_TextEdit_TextChangedEventId` /
    `TextEditChangeType_AttributeChange` — note F3 already touched a text-attribute-changed patch). Verify a
    subscribed client sees the attribute flip.
  - **Verify (autonomous, no NVDA):** extend `scripts/uiaprobe.cpp` to call `GetAttributeValue` on the field's
    `DocumentRange` for the three attribute ids and print them; assert bold/italic/underline read back, then
    that a change is observable. Then NVDA read-back of "bold"/"not bold" on the unlocked desktop.
  - Status: **[DONE] 2026-06-26** for a single uniform run. `GetAttributeValue` returns FontWeight=700,
    IsItalic=TRUE, UnderlineStyle=1 (`scripts/uiaprobe_attrs.cpp`); bold→normal change shows FontWeight 700→400
    on edit, signalled by `TextChanged`. Attributes must sit on the **field** (atomic text field ⇒ its inner
    text's `GetLowestPlatformAncestor` is the field). **Still open:** mixed-format runs within one field (needs
    a non-atomic per-run platform structure); a dedicated `UIA_TextEdit_TextChangedEventId(AttributeChange)`
    signal (we currently rely on `TextChanged`); NVDA read-back of "bold"/"not bold".

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
