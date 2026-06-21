# Expert B — AT / screen-reader interop review

Lens: NVDA + Windows UIA consumer behavior, IAccessible2, real screen-reader user experience. Source pin cross-checked: Chromium 149.0.7827.115. Three passes: AX-tree/event design vs what UIA+NVDA consume; docs/10 matrix from the AT-consumer view; verification methodology (TASK-00a).

Framing: the central thesis (a non-Blink producer drives `ui/accessibility` for full editing semantics via tree deltas) is sound and well-evidenced **at the generation layer**. The concern throughout is the gap between "the event fires" (proven) and "NVDA speaks something useful" (largely unproven, and in places likely NOT as the docs assume). Several rows graded SUPPORTED are SUPPORTED-at-generation, not SUPPORTED-for-a-user, and the matrix grade column doesn't make that distinction visible.

## PASS 1 — Does the design map to what UIA + NVDA actually consume?

### Successes
- **[HIGH] Delta-driven editing event model is correct; "no intents required" holds at source.** `FireValueInTextFieldChangedEventIfNecessary` (ax_event_generator.cc:1124) → manager finalizes `UIA_Text_TextChangedEventId` unconditionally (browser_accessibility_manager_win.cc:1312). Load-bearing claim, verified both sides.
- **[HIGH] The atomicity requirement (§3.2) matters more than the docs stress.** NVDA does not read the *content* of `UIA_Text_TextChangedEventId` — it treats it as "re-anchor" and derives speech from caret/selection movement + its own typed-char echo. T2-5 proving text-changed + selection-changed land in one finalize pass is therefore the most important coherence result: NVDA's autocorrect announcement is driven by the selection move arriving in the same pump cycle.

### Open concerns
- **[HIGH] Autocorrect/replacement announcement is unproven for NVDA and likely insufficient as designed.** Real autocorrect announcement depends on `UIA_TextEdit_TextChangedEventId` with `TextEditChangeType_AutoCorrect`, which here fires *only* from `OnActiveComposition`/`FireUiaTextEditTextChangedEvent` (ax_platform_node_win.cc:835) — the IME path, not the generic delta path. App-driven autocorrect without routing through composition → NVDA says nothing or echoes only the trigger char. Matrix grades this SUPPORTED on the atomic delta; from the AT side it's **OPEN/BROWSER-WORK**. Most important miscategorization in the project.
- **[HIGH] IME composition: committed-composition path fires nothing, and the whole path is gated on `HasEventListenerForEvent`.** `FireUiaTextEditTextChangedEvent` (ax_platform_node_win.cc:845): on `is_composition_committed` it returns immediately; even ongoing composition early-outs unless NVDA registered an advise for `UIA_TextEdit_TextChangedEventId` for a custom non-Blink provider window — not something generation-layer tests can show. T2-4 as scoped passes at API level and still leaves the user hearing nothing if NVDA didn't subscribe. Downgrade IME to **BROWSER-WORK pending NVDA capture**.
- **[MEDIUM] "Which node carries the selection event" (NEXT-Q #2) is resolved in source and the framing is slightly wrong.** The manager enqueues the UIA selection event on the **focus object AND its text-field ancestor** (browser_accessibility_manager_win.cc:435-474), each gated by `IsPatternProviderSupported(UIA_TextPatternId)` at finalize (1300-1308). Not "root vs field, pick one." Good news for the editor, but the producer's **field node must advertise `UIA_TextPatternId`** or the event is dropped — a concrete obligation §2.2 omits.
- **[MEDIUM] Caret tracking via `kCaretBounds`: AT-consumption risk understated.** NVDA tracks the caret for speech via `ITextRangeProvider` (GetCaretRange/selection), not bounds. `kCaretBounds` drives magnification/focus-highlight/braille routing/candidate-window placement, not speech. Caret correctness for NVDA = `ITextRangeProvider` correctness, which is untested and not in the contract.
- **[MEDIUM] Browse vs focus mode determines whether ANY of this is heard.** NVDA's mode keys off the focused element's control type + states. Mis-role the root → browse mode → NVDA renders its own snapshot and suppresses these events. Belongs above "open item #2."

## PASS 2 — Re-grading riskiest matrix rows (AT-consumer view)
- **[HIGH] Multi-cell selection / SelectionPattern2:** NVDA doesn't depend on ISelectionProvider2 (correct), but with Selection v1 only, NVDA gives as-you-go per-cell feedback and **no coherent "what is selected now" summary**. "Functionally expressible" is true at API level, misleading as UX.
- **[HIGH] Comments/annotations:** forward exposure is real but NVDA's UIA-annotation support is partial; user hears "has comment" but **cannot navigate/read the thread inline** without `ITextProvider2::RangeFromAnnotation`. Top-line "SUPPORTED" reads as "comments work" — they don't, for the canonical workflow.
- **[HIGH] Remote cursors / presence — OPEN, and worse than "lossy."** No mechanism for NVDA to announce "Alice's cursor entered your paragraph"; naive highlight-marker modeling would spam highlight-attribute noise per remote keystroke. Live regions are the only thing that reaches an NVDA user today for presence.
- **[MEDIUM] Live regions vs ariaNotify:** `LIVE_REGION_CHANGED` → `UIA_LiveRegionChangedEventId` (browser_accessibility_manager_win.cc:536) confirmed; NVDA consumes it but only when the element advertises `UIA_LiveSettingPropertyId`, and NVDA coalesces/drops rapid updates (collaboration churn). SUPPORTED-mechanism / verify-experience.
- **[MEDIUM] Spellcheck markers:** SUPPORTED is right for the *navigation* experience ("misspelled, <word>" on arrow-over); the change-event ("squiggle appeared while idle") is far less reliably spoken. Matrix conflates the two; the kRichlyEditable-on-node gate is a valuable catch.
- **[LOW] Formatting-change notification:** correctly BROWSER-WORK. Bold-while-idle not announced; bold-on-navigation is (GetTextAttributeValue implemented) — acceptable NVDA behavior.

## PASS 3 — Verification methodology (TASK-00a)

### Successes
- **[HIGH] NVDA-over-Narrator + greppable speech log + No-speech synth is the right instrument;** "stronger, not weaker" holds.
- **[MEDIUM] Strategy A→B graduation well-judged** (SystemTestSpy is how NVDA tests itself).
- **[MEDIUM] "Not truly headless" caveat handled honestly** (needs interactive desktop; auto-logon provides it).

### Open concerns
- **[HIGH] Methodology proves "events reach NVDA," not "real editing works."** No real input loop: typed-character/word echo comes from NVDA's keyboard hook correlated with caret movement — SendKeys may not reproduce it; read-back/review-cursor/say-all exercise `ITextRangeProvider` Move/GetText that no test touches. Add an `ITextRangeProvider` conformance probe (raw UIA client walking ranges by unit).
- **[HIGH] Braille is entirely absent** — a first-class output. Caret position in braille cell flow + cursor-routing round-trip (routing key → `ITextRangeProvider::Select`) are uncaptured. SystemTestSpy can capture braille; the plan should assert it for editing scenarios.
- **[MEDIUM] Single-AT (NVDA only) under-proves interop.** Provider behaves differently per AT (JAWS virtual-cursor + kSelection workarounds in the manager; IA2-vs-UIA formatting split). Add a JAWS pass for the editing core; Narrator is the one that wants SelectionPattern2.
- **[MEDIUM] Event coalescing / timing (open item #3) not turned into a test design.** Add a stress scenario (interleaved local+remote edits, rapid wrapped-line affinity caret moves) and assert the speech log *under load*.
- **[MEDIUM] Not-headless consequence for fidelity unaddressed:** RDP changes UIA event delivery/focus vs a console session (confirm at least once on console); NVDA gates announcements on foreground state (pin window-foreground per run).
- **[LOW] Log-level 5 unredacted for typed-char verification is correctly reasoned;** keep the throwaway-VM note with any committed log.

## Biggest success / concern
- **Biggest success (HIGH):** the delta-driven producer model is correct and verified on both sides of the boundary, and pairing it with the atomicity requirement shows real insight into how the platform must hand a coherent state to the AT in one pump cycle.
- **Biggest concern (HIGH):** autocorrect/replacement and IME-composition announcements will not reach an NVDA user as the matrix's SUPPORTED grades imply — both depend on the composition-only `UIA_TextEdit_TextChangedEventId` path (early-returns on commit, gated on a listener), and no test drives real typing + read-back + braille. Before claiming success: re-grade autocorrect/IME to BROWSER-WORK/verify; add an `ITextRangeProvider` probe + real-typing/braille NVDA capture; add a JAWS pass and a coalescing-under-load scenario.
