# Expert Review — Aggregate Report

Date: 2026-06-16. Three independent Chrome-accessibility expert reviews of the AccessibleWebEdit implementation, each with a distinct lens and three review passes, cross-checked against Chromium 149.0.7827.115. Individual reports: `expert-A-chromium-internals.md`, `expert-B-at-interop.md`, `expert-C-standards-architecture.md`.

- **Expert A — Chromium `ui/accessibility` internals:** correctness of tests, bridge contract, and patches against source (line-level).
- **Expert B — AT / screen-reader interop (NVDA/UIA consumer):** will it actually work for a blind user; is the verification methodology sound.
- **Expert C — web-platform / standards architect:** is the strategic thesis sound and defensible.

## Headline: the three reviewers independently agree

**Biggest shared success.** The producer-agnostic core — `AXTree → AXEventGenerator → AXPlatformNode(Delegate)` driven by tree deltas alone — is **correct and verified at the generated-event layer**. All three confirmed it independently (A at line level against the generator; B confirmed the Windows manager finalizes `UIA_Text_TextChangedEventId` from those deltas with no intents; C confirmed the load-bearing `browser_accessibility_manager_win.cc` relocation into `ui/accessibility/platform/` that makes the "same citizen as Blink/PDF" claim true). The PROVEN/SOURCE/VM evidence ledger is unusually honest and makes weak points findable rather than hidden.

**Biggest shared concern.** The headline conclusion is asserted **one full layer above where it is proven**, and the experiment that could falsify it — the Windows VM run (UIA translation → NVDA speech) — **has never run**. Every "PROVEN" is at the `AXEventGenerator` layer on Linux; the product claim ("native delivers *real* editing fidelity for a blind user") is about screen-reader-observable behavior that is entirely VM-gated. B and C both say, in different words: stop saying "PROVEN/SETTLED" about *fidelity*; say "designed and event-layer-validated; fidelity pending the VM." A's fencing of off-Windows claims says the same implicitly.

Most actionable single recommendation (C, echoed by B): before further design, run the smallest end-to-end slice — one insert + one caret move through `AXPlatformNodeWin` → UIA → NVDA on a real Windows box — and treat *that*, not the generated-event tests, as the moment the thesis is proven or broken.

## Open concerns, rated HIGH → LOW (deduplicated; (X) = which expert)

### HIGH
1. **Fidelity is unproven; "PROVEN/SETTLED" overstates.** All PROVEN tags are event-layer on Linux; UIA-translation + NVDA-speech are unverified and VM-gated, with known Windows-specific open questions (selection-event mapping #2/#5, composition double-announce) the Linux tests structurally cannot answer. (B, C)
2. **Autocorrect & IME announcements likely will NOT reach an NVDA user as graded SUPPORTED.** Real autocorrect/composition announcement rides `UIA_TextEdit_TextChangedEventId`, which in this codebase fires *only* from the composition hook (`OnActiveComposition`, ax_platform_node_win.cc:835), returns early on commit, and is gated on `HasEventListenerForEvent`. The generic delta path gives NVDA a contentless TextChanged + caret move. → Re-grade autocorrect and IME from SUPPORTED to **BROWSER-WORK / verify**. (B)
3. **docs/03 §1.1 is false as written: the editable root does NOT need `State::kEditable`.** `IsTextField()`/`GetTextFieldAncestor` are **role-only** (ax_node_data.cc:839-850, ax_node.cc:2475). The T2-2 negative test flips role→kGenericContainer AND drops kEditable simultaneously, so it cannot isolate the gate — the [PROVEN] tag is unearned and the contract's most foundational clause is overstated + under-tested. (A)
4. **`text-attribute-changed` patch uses the wrong enqueue API.** It does `text_changed_nodes_.insert(wrapper)` (raw leaf); the real path is `EnqueueTextChangedEvent(*wrapper)` which inserts `GetUiaTextPatternProvider(node)`. Since finalize fires `UIA_Text_TextChangedEventId` unconditionally (no Text-pattern guard, unlike the selection set), the raw insert risks firing on a node with no Text pattern. → Fix to `EnqueueTextChangedEvent`. (A)
5. **"~80% shared core + thin per-platform shell" is optimistic and self-contradictory.** The docs also say IME is "the single biggest" cost and "eats cross-platform editor projects" — you can't call the shell "thin (20%)" while it holds the dominant risk. The 80/20 number is asserted, not derived, and 00-INDEX promotes it without caveat. (C)
6. **In-tree (Strategy A) maintenance/rebase/security cost is under-examined.** Pinned to a stable tag, a shipping product needs security updates → continuous rebase of the editor + the four platform patches against a moving `ui/accessibility`, or a frozen vulnerable fork. The patches are permanent rebase liabilities unless upstreamed (a multi-quarter process). The "dependency cone" is most of a browser. Needs a real maintenance-cost model, not a one-line note. (C)
7. **Verification methodology proves "events reach NVDA," not "editing works for a user."** No test drives **real typing + read-back + braille** through NVDA's keyboard hook and `ITextRangeProvider`; caret correctness for NVDA = `ITextRangeProvider` correctness, which is untested and not even in the bridge contract. → Add an `ITextRangeProvider` conformance probe, real-typing + braille capture, a **JAWS** pass, and a **coalescing-under-load** scenario. (B)

### MEDIUM
8. **Strategy A (recommended) vs Strategy B (actually executed) tension is unresolved.** The proven artifact is the *standalone* B-lite spine (links just the a11y library, 107 tests, runs) — i.e., Strategy B works — yet docs/11–13 recommend in-tree Strategy A. Either B is more viable than docs admit (evidence says so) or the recommendation should be revisited. (C)
9. **"AOM virtual nodes permanently capped" is overstated.** The docs' own HTML-in-Canvas (`drawElement`, shipping behind a flag ~147) is actively solving the mirror-sync problem the ceiling argument rests on. Honest framing: "capped today, one active web effort could lift it." (C)
10. **Mirror-DOM ceiling magnitude is asserted, not measured.** Google Docs/Figma ship canvas editors via the mirror pattern at scale; the "native is *superior*" claim needs a head-to-head native-vs-mirror measurement through a real screen reader, which doesn't exist. (C)
11. **Selection-event delivery resolved in source — and the contract is missing an obligation.** The manager enqueues the UIA selection event on **both** the focus object and its text-field ancestor, each gated by `IsPatternProviderSupported(UIA_TextPatternId)` at finalize (browser_accessibility_manager_win.cc:1300-1308). So the producer's field node **must advertise `UIA_TextPatternId`** or the event is silently dropped — add to bridge §2.2 / producer checklist. (B)
12. **Multi-cell selection & comments: SUPPORTED at API ≠ good AT experience.** NVDA announces per-cell selection as-you-go but gives **no coherent "what is selected" summary** from Selection v1; comments are discoverable ("has comment") but **not navigable/readable** without the missing `ITextProvider2::RangeFromAnnotation`. The matrix's top-line SUPPORTED reads as "works." (B)
13. **`ITextProvider2` patch has API errors and is a sketch.** Uses `PatternProvider<ITextProvider>` (real text factory is `AXPlatformNodeTextProviderWin::CreateIUnknown`, ax_platform_node_win.cc:8700); wrong gating predicate vs canonical `IsPlatformDocument()||IsTextField()||IsText()` (:8699); method bodies are `/* ... */` placeholders → ~64-line estimate optimistic. (A)
14. **All four patches cite the manager at the stale `content/` path;** at tag 149 it's `ui/accessibility/platform/browser_accessibility_manager_win.cc` (line numbers match, path misleads). (A)
15. **Live regions / ariaNotify caveats:** NVDA gates on `UIA_LiveSettingPropertyId` (producer must populate the *property*, not just fire the event) and coalesces rapid updates (collaboration churn); ariaNotify is unreliable on macOS — a hole on platform #2. (B, C)
16. **SkParagraph a11y-geometry risk could blow up the cost model.** If SkParagraph doesn't expose per-char/word/line bounds at the granularity UIA Text + braille need, the "one layout engine" constraint forces a custom layout engine — invalidating the "well-trodden Skia text / 80% shared" assumption. (C)
17. **PDF precedent (docs/09 Finding 3) unverifiable from the lean checkout** (`components/pdf` absent). Very likely true, but the project's stated "spot-check against source" method can't validate its most important precedent from the shipped artifact. (C)
18. **Browse-vs-focus mode determines whether ANY of this is heard** and depends on the root's role/states; in browse mode NVDA suppresses many of these events. Belongs above "open item #2." (B)
19. **Standards-vs-product timeline tension:** if native ships and works, the standards motivation weakens ("why fix the web when we have a product?"); if standards is the goal, the heavy Strategy-A build is an expensive PoC. Presented as complementary without examining the conflict. (C)
20. **Unexamined premise: "why canvas at all?"** An a11y-first product could use DOM/contenteditable (full Text pattern free via Blink) and avoid the entire IME + native-shell + in-tree-build mountain, which exists *because* of the canvas choice. The question that would most change the answer isn't asked. (C)
21. **Mobile fidelity likely overstated:** Android `AccessibilityNodeInfo` / iOS `UIAccessibility` expose a weaker text API than UIA Text pattern; "full fidelity, all 5 platforms" is probably false for 2 of 5. (C)

### LOW
22. **Test-count inconsistency:** binary has 87 upstream + 7 T2 + 12 matrix = **106**, but docs say "104." Reconcile. (A)
23. **SelectionProvider2 patch ships an empty `namespace { }` "helper" block** — dead scaffolding. (A)
24. **T2-2 comment misattributes suppression to "editable=false"** (real gate is the role change) — documentation clarity. (A)

## Successes, rated HIGH → LOW (aggregate)

### HIGH
- Producer-agnostic pipeline correct & event-layer verified, on both generation and provider sides. (A, B, C)
- The **atomicity requirement** (§3.2 / T2-5) is the key insight for AT coherence — NVDA derives announcements from the selection move landing in the same pump cycle as the text change. (A, B)
- Line-level-correct findings: caret-bounds correction (kCaretBounds, not sel_), two selection events, richly-editable gating, atomic dedup — all hold to scrutiny. (A)
- **EditContext-is-input-only** distinction (+ Monaco/VS Code corroboration) and the **privacy-asymmetry** argument (web-origin fingerprinting doesn't transfer to native) — sharpest strategic moves. (C)
- **IME-is-the-hard-part** judgment and the **one-layout-engine-feeds-both** non-negotiable constraint. (C)
- Honest PROVEN/SOURCE/VM ledger and self-labeled "written, not compiled" patches. (A, B, C)

### MEDIUM
- NVDA-over-Narrator + speech-log + SystemTestSpy methodology choice is the right instrument; Strategy A→B graduation well-judged; not-truly-headless caveat handled. (B)
- IME→Strategy-A linkage (`ui/base/ime` already has both the TextInputClient impls and the a11y hook) is the best argument for in-tree. (C)
- Delta-based child-tree API is a more credible standards proposal than virtual nodes; remote-cursor/edit-origin gaps are the strongest "web is missing something" exhibits, found by construction. (C)
- SelectionProvider2 and annotation author/datetime patches are API-accurate and correctly sized. (A)

## Consolidated action list (what to do about it)

**Fixable now, no VM (factual corrections the review surfaced):**
- A1. docs/03 §1.1: remove the false "plus `State::kEditable`"; state the gate is **role-only** (`IsTextField()`); note T2-2 confounds role+state and split/relabel the test.
- A2. Patch fix: `text-attribute-changed` → use `EnqueueTextChangedEvent(*wrapper)`.
- A3. Patch fix: correct the stale `content/...` path → `ui/accessibility/platform/...` in all four patches; mark the `ITextProvider2` patch as a sketch with the `CreateIUnknown`/predicate corrections; drop the empty namespace block.
- A4. Re-grade docs/10: autocorrect & IME → BROWSER-WORK/verify (announcement depends on the TextEdit path); add the "SUPPORTED-at-generation ≠ SUPPORTED-for-user" caveat column; note multi-cell/comments UX limits.
- A5. Add bridge §2.2 obligation: the field node must advertise `UIA_TextPatternId` or the selection event is dropped at finalize.
- A6. Reconcile the test count (106) across docs.
- A7. Soften "PROVEN/SETTLED/permanently capped" language to "event-layer validated; fidelity pending VM" + "capped today, drawElement could lift it."

**VM-gated additions to the verification plan (TASK-00a / NEXT-QUESTIONS):**
- B1. Smallest end-to-end slice first (insert + caret move → UIA → NVDA) as the real proof gate.
- B2. `ITextRangeProvider` conformance probe (raw UIA client walking ranges by unit) — independent of NVDA.
- B3. Real-typing + read-back + **braille** capture; **JAWS** pass; **coalescing-under-load** scenario.
- B4. Confirm results on a console session (not just RDP) and pin window-foreground per run.

**Strategic decisions for planning (no clean answer; surfaced as risks):**
- C1. Resolve Strategy A vs B given that the *proven* artifact is standalone (B); build a maintenance/rebase/security-update cost model for in-tree before committing to A.
- C2. Answer "why canvas at all for an a11y-first product?" explicitly.
- C3. Decide product vs standards as primary goal (the two are in timeline tension).
- C4. Measure native-vs-mirror-DOM head-to-head through a screen reader before claiming native superiority.

Net: the foundation is real and honestly evidenced; the conclusions are ahead of the evidence by one (decisive) layer; and the review surfaced ~7 concrete factual fixes plus a sharper, harder verification bar to clear on the VM.
