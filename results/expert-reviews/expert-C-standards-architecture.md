# Expert C — web-platform / standards architecture review

Lens: AOM history, EditContext, ariaNotify, ARIA, cross-platform native a11y APIs. Three passes: native-vs-web thesis (docs/13); cross-platform rendering + IME (docs/11, docs/12); standards-exhibit framing + overall risk. Severity reflects impact on the project's central claims.

Verification note: load-bearing `ui/accessibility` claims hold against the pinned checkout (GetActiveComposition/GetConversionTarget present; OnActiveComposition at ax_platform_node_win.cc:823; `browser_accessibility_manager_win.cc` is in `ui/accessibility/platform/`; ax_action_target.h exists; T2 tests drive the real generator; patches honestly self-labeled "written, not compiled"). **Caveat:** the checkout is lean/pruned — `components/pdf`, Blink core editing/canvas, and EditContext are absent — so the PDF-producer precedent (docs/09 Finding 3) and EditContext's input-only behavior could not be re-verified here.

## PASS 1 — Native-vs-web thesis (docs/13)

### Successes
- **[HIGH] S1.1 Producer-agnostic pipeline claim is correct and load-bearing.** Verified the key consequence directly: `browser_accessibility_manager_win.cc` lives in `ui/accessibility/platform/`, not `content/`. The "same kind of citizen as Blink and PDF" foundation is real.
- **[HIGH] S1.2 EditContext-is-input-only distinction is accurate and well-deployed.** Splitting the editor stack into input (EditContext) / a11y (mirror-DOM) / announcements (ariaNotify) is the sharpest analytical move; Monaco/VS Code's separate `ScreenReaderSupport` is exactly right corroboration.
- **[MEDIUM] S1.3 Privacy-asymmetry argument is novel and correct.** Virtual nodes died on AT-fingerprinting of a *web origin*; a native app isn't a web origin — the objection doesn't transfer.

### Open concerns
- **[MEDIUM] O1.1 "AOM virtual nodes permanently dead" is overstated.** The doc's own HTML-in-Canvas (`drawElement`, shipping behind a flag ~147) is solving the exact mirror-sync problem the ceiling rests on. Honest framing: "capped today, one active effort could lift it."
- **[MEDIUM] O1.2 Mirror-DOM ceiling magnitude asserted, not demonstrated.** Google Docs/Figma ship canvas editors via the mirror pattern at scale; the "native is *superior*" thesis needs a head-to-head native-vs-mirror measurement through a real screen reader. Asserted, not measured.
- **[LOW] O1.3 "This project IS what virtual nodes should have been" conflates two things.** Virtual nodes' value was *web-developer ergonomics/reach* inside the sandbox; B-lite is C++ below it. They share the AXTreeUpdate vocabulary; the native producer is the *substrate*, not the missing web API. §1 mostly gets this right; headline framing sands it off.
- **[LOW] O1.4 ariaNotify macOS weakness soft-pedaled.** "Not reliably spoken on macOS" is a per-platform hole on platform #2, more than a parenthetical.

## PASS 2 — Cross-platform rendering + IME (docs/11, docs/12)

### Successes
- **[HIGH] S2.1 "IME is the hard part, not rendering" is correct and well-argued.** Bidirectional text-input-client framing accurate; per-platform contract table (TSF/NSTextInputClient/IBus-fcitx/InputConnection/UITextInput) correct.
- **[HIGH] S2.2 "One layout engine feeds both pixels and a11y" is the right non-negotiable.** Single layout pass or braille/caret drifts; correctly flagged expensive-to-retrofit.
- **[MEDIUM] S2.3 IME→Strategy-A linkage is the strongest A argument** (`ui/base/ime` has both the TextInputClient impls and `SetActiveCompositionForAccessibility`).

### Open concerns
- **[HIGH] O2.1 "~80% shared core + thin shell" is too optimistic and self-contradictory** — the docs also call IME "the single biggest" cost that "eats" such projects. Can't be "thin (20%)" and hold the dominant risk. Number is undisplayed-derivation; 00-INDEX promotes it without caveats.
- **[HIGH] O2.2 In-tree maintenance/rebase cost acknowledged in one line, then dismissed.** Pinned to stable → security updates require continuous rebase of the editor + four platform patches against a moving `ui/accessibility`, or a frozen vulnerable fork. The patches are permanent rebase liabilities unless upstreamed (multi-quarter). The "dependency cone" is most of a browser. Open-decision #4 (how much gpu/viz/Ozone to pull) determines whether A is even tractable and is left open. The "already committed to ui/accessibility" rationale is sunk-cost-shaped: the a11y lib is small/linkable (Strategy B links just it); IME+rendering reuse is what forces the full-tree commitment.
- **[MEDIUM] O2.3 Strategy B's cost overstated to make A inevitable.** 00-INDEX says the chosen path *is* B-lite (standalone, links just the a11y library) and it works (107 tests, runnable spine) — yet docs recommend A. Unreconciled.
- **[MEDIUM] O2.4 Skia/SkParagraph + a11y-geometry coupling glossed.** If SkParagraph doesn't expose per-char/word/line bounds at UIA-Text/braille granularity, the "one layout engine" constraint forces a custom layout engine, blowing up the 80% assumption. The risk most likely to invalidate the cost model.
- **[LOW] O2.5 Mobile a11y API surface is materially weaker.** `AccessibilityNodeInfo`/`UIAccessibility` expose a less rich text API than UIA Text/IA2; "full fidelity, all 5 platforms" is likely false for 2 of 5.

## PASS 3 — Standards-exhibit framing + overall risk

### Successes
- **[MEDIUM] S3.1 Delta-based child-tree API is a more credible proposal than virtual nodes** — smaller/cleaner than `appendChild(new AccessibleNode())`, with PDF + B-lite as working proof of the shape; sidesteps the API-complexity half of why virtual nodes stalled.
- **[MEDIUM] S3.2 Remote-cursor/edit-origin gaps are the strongest exhibits, found honestly** (docs/03 §7, NEXT-Q #3/#4) — gaps discovered by construction.
- **[MEDIUM] S3.3 Intellectual honesty of the evidence ledger** (PROVEN/SOURCE/VM tags, "written not compiled" patches, ENVIRONMENT.md deviations) makes weak points findable.

### Open concerns
- **[HIGH] O3.1 The entire editing-fidelity claim rests on generated-event tests, not end-to-end AT.** Every PROVEN = the generator produced expected event objects on Linux. Between that and "a blind user gets faithful editing" sit UIA translation, UIA delivery, and NVDA interpretation/speech — none run. 00-INDEX still headlines "Foundation PROVEN" and "native is the only path to *real* fidelity." Necessary but nowhere near sufficient.
- **[HIGH] O3.2 The whole verification critical path depends on a VM that has never run** (00-INDEX says so), plus an unresolved Win11 licensing attestation (NEXT-Q #12). Single point of failure for the thesis; design conclusions ("Options SETTLED") front-loaded ahead of the one experiment that could falsify them.
- **[MEDIUM] O3.3 "PROVEN on Linux" generalizes to Windows UIA by assumption.** Several open questions are Windows-specific translations the Linux runs cannot answer (which selection event → `UIA_Text_TextSelectionChangedEventId`; Text-pattern gating; no-double-announce on commit). Linux/AT-SPI and Windows/UIA diverge at exactly the layer the product is sold on.
- **[MEDIUM] O3.4 PDF precedent not reproducible from the provided checkout** (`components/pdf` absent) — the production-fact that de-risks the architecture can't be validated by the project's own stated method from the shipped artifact.
- **[MEDIUM] O3.5 Standards-play vs product timeline in tension, unaddressed.** Standards adoption is years + multi-vendor (Safari declined ariaNotify; FF/Safari haven't shipped EditContext). If native ships and works, the standards motivation weakens; if standards is the goal, the heavy Strategy-A build is an expensive PoC. Presented as complementary without examining the conflict.
- **[MEDIUM] O3.6 Unexamined premise: is a custom canvas editor the right answer at all?** A DOM/contenteditable editor gets the full Text pattern free via Blink and has no canvas ceiling; the entire IME + native-shell + in-tree mountain exists *because* of the canvas choice. "Why canvas, for an a11y-first product?" is the question that would most change the answer and isn't asked.

## Bottom line
- **Biggest success:** the architectural core is correct and verifiably so — producer-agnostic pipeline confirmed (the `browser_accessibility_manager_win.cc` relocation), plus the sharpest strategic analysis (EditContext-input-only, mirror-DOM ceiling, privacy asymmetry) and an honest evidence ledger.
- **Biggest concern:** the headline conclusion is asserted one full layer above where it's proven, and the falsifying experiment has never run. "Native is the only path to *real* fidelity" / "Options SETTLED" are claims about screen-reader-observable behavior, but every PROVEN result is generator-layer on Linux, with known Windows-specific open questions the Linux tests can't answer. Stop saying "settled/proven" about fidelity; say "designed and event-layer-validated; fidelity pending the VM." Most actionable: run the smallest end-to-end slice (one insert + one caret move) through AXPlatformNodeWin → UIA → NVDA on a real Windows box, and treat *that* as the moment the thesis is proven or broken.
