# Decision Analysis — C1 (Strategy A vs B), C2 (Why canvas?), S1 (SkParagraph a11y geometry)

Date: 2026-06-24. Source backlog items: `results/expert-reviews/REVIEW-BACKLOG.md` (C1, C2, S1) and `AGGREGATE.md` findings #6, #8 (C1); #20 (C2); #16 (S1).

**This document is decision-analysis, not verified fact.** Every claim is tagged by evidence level per repo convention:
- **PROVEN** = a passing test or run exists in this repo.
- **SOURCE** = read from Chromium source / official docs / this repo's docs.
- **VM** = pending the Windows verification that has not yet run.

Each RECOMMENDATION below is exactly that — a recommendation. The user makes the final call.

> **Dependency note:** S1 gates C1. The C1 cost model assumes "~80% shared core / well-trodden Skia text" (docs/11). If S1 resolves negative (SkParagraph cannot expose the a11y geometry), that assumption breaks and the shared-core cost rises sharply — which in turn changes the A-vs-B math. Read S1 first if you want the cost model's load-bearing assumption checked; read C1/C2 first if you want the strategic shape.

---

## C1 — Strategy A (in-tree Chromium fork) vs Strategy B (standalone host on `ui/accessibility`)

### The question restated
docs/11–13 recommend **Strategy A** (build the editor inside the Chromium tree, reusing Skia + gfx + Ozone + `ui/base/ime` + `AXPlatformNode`). But the only **PROVEN-runnable** artifact today is the **standalone Strategy B** spine (`blite/blite_host.cc`), which links *only* `//ui/accessibility:accessibility_internal` + `//base` and runs the full producer pipeline end-to-end [PROVEN — `blite/README.md`; `results/blite-host-run.txt`]. So: does the recommendation survive a real maintenance/cost model, given that the proven path is the one the docs do *not* recommend?

### Evidence — for Strategy A (in-tree)
- **IME is already written, five times, and already wired to a11y.** `ui/base/ime` contains the per-platform `TextInputClient` implementations (TSF / NSTextInputClient / IBus) *and* the accessibility hook `SetActiveCompositionForAccessibility → AXPlatformNodeWin::OnActiveComposition` [SOURCE — `docs/12` §accessibility tie-in; `ax_platform_node_win.cc:823`]. This is the single strongest A argument: IME is graded "the single biggest cost" and "eats cross-platform editor projects" [SOURCE — `docs/12`; `docs/11`], and A inherits it solved.
- **The platform-node layer is per-platform and already exists** for all five targets (`AXPlatformNodeWin`/UIA, `…Mac`, `…AuraLinux`, Android, iOS) [SOURCE — `docs/11` §framing]. A reuses it; B must link and host it itself.
- **The manager finalize machinery now lives in `ui/accessibility/platform/`** (not `content/`), so even B can reuse `BrowserAccessibilityManagerWin`'s event finalize — but A reuses it without leaving the tree [SOURCE — `docs/09` Finding 1].

### Evidence — for Strategy B (standalone)
- **B demonstrably works at the spine level.** The lean cone (`accessibility_internal` + `base`, no v8, no aura/views) builds and runs the surface→bridge→AXTree→AXEventGenerator pipeline and produces the predicted editing event set [PROVEN — `blite/README.md`]. A is *not* runnable in this repo at all today.
- **B's deliberately-excluded layer is bounded and known.** B-lite omits only `AXPlatformNode` materialization, which "pulls a large new build (v8 via gin; aura/views/glib for AT-SPI)" [SOURCE — `blite/README.md`]. The boundary is explicit, not vague.
- **B is the lighter binary with full control** — A's cost is "build inside Chromium's heavy build and pull a large dependency cone" [SOURCE — `docs/11` Strategy A].

### The maintenance / cost model (the part the docs under-examine — AGGREGATE #6)
A shipping product needs **security updates**. This is where A and B diverge most, and where the one-line "heavy build" note in docs/11 is insufficient.

1. **Security-rebase cadence (A).** Chromium ships stable security updates roughly every 1–2 weeks. An in-tree product pinned to `149.0.7827.115` either (a) **rebases continuously** — moving the editor *and* the four `patches/` *and* any local `ui/accessibility` edits forward against a moving tree, indefinitely; or (b) **freezes the fork** and ships known-vulnerable Chromium. There is no third option. This is a standing, unbounded engineering tax that exists for the life of the product. [SOURCE — general Chromium release cadence + AGGREGATE #6; the *magnitude in hours/quarter* is unmeasured — treat as VM/operational spike.]
2. **The four patches as permanent rebase liabilities.** `patches/` is ~160 lines across 6 files [SOURCE — `patches/README.md`], small in absolute terms but each is a permanent merge-conflict surface against `ax_platform_node_win.cc` / `browser_accessibility_manager_win.cc` until **upstreamed** — and upstreaming UIA-provider changes into Chromium is a multi-quarter review process with no guarantee of acceptance [SOURCE — AGGREGATE #6]. Until then they re-apply on every rebase. Note: the patches are A-vs-B-neutral in one sense (both A and B that want those UIA capabilities carry them), but **A multiplies the rebase surface** because the *whole editor* also rebases alongside them.
3. **The dependency cone (open decision #4).** A inherits Skia + gfx + Ozone + `gpu`/`viz` + `ui/base/ime`. docs/11 open decision #4 explicitly asks "how much of Chromium's `gpu`/`viz`/Ozone surface stack to pull vs a thin per-platform GL/Metal context + Skia backend" — i.e., **the size of the cone is itself unresolved** [SOURCE — `docs/11` open decision #4]. B's cone is `accessibility_internal` + `base` proven, plus a known increment for the platform-node layer. A's cone is "most of a browser" [SOURCE — AGGREGATE #6]. The honest statement is: B's cone is measured; A's is not.

### Tradeoffs (the resolution of the tension)
The contradiction dissolves once you separate **two different layers** the docs conflate:
- **The a11y producer + platform-node layer** (AXTree → AXEventGenerator → AXPlatformNode → UIA). This is small, proven at the generation layer [PROVEN — 109 tests], and B already links most of it. There is **no strong A argument here** — B reaches the same `AXPlatformNode` citizen-of-Chromium status [SOURCE — `docs/13` §central finding].
- **The IME + input stack** (`ui/base/ime`, TSF, composition→a11y hook). This is where A's argument is real and B's cost is highest [SOURCE — `docs/12` §why-IME-drives-the-decision].

So "A vs B" is not one decision — it is two. The docs lean A primarily on the **IME** argument and then let that pull the *entire* stack in-tree, including the GPU/viz cone the editor may not need to inherit wholesale.

### RECOMMENDATION (C1)
**Recommendation: do not commit to monolithic Strategy A. Adopt a hybrid / staged path, and keep B as the spine.**

1. **Keep the a11y producer + platform-node layer on the Strategy-B standalone path.** It is the proven artifact, the cone is bounded, and A buys nothing here.
2. **Scope A's justification narrowly to `ui/base/ime`.** Treat "how much of `ui/base/ime` can be consumed without inheriting `gpu`/`viz`/Ozone" as an explicit spike (pairs with docs/11 open decision #4). The IME argument justifies pulling the *input* stack, not the *whole* browser.
3. **Before any commitment to in-tree, produce two measured numbers** the docs currently assert: (a) a real security-rebase cost estimate (one actual rebase of `patches/` + editor across, say, two stable bumps, timed), and (b) the actual dependency-cone size for "ime-only" vs "full surface stack." These are operational spikes, not desk research.
4. **Default posture: standalone B for the shippable core, with a documented in-tree `ui/base/ime` dependency as the one deliberate exception** — i.e., closest to a "B-plus-IME" hybrid, not A. This matches the proven evidence and avoids signing up for an unbounded, unmeasured rebase tax against most of a browser.

**Gating:** S1 must resolve before this cost model is trustworthy (see S1). If SkParagraph cannot supply the a11y geometry, the shared core grows a custom layout engine and the calculus shifts further *toward* reusing more of Chromium (mild push toward A), but also raises the total cost of the whole endeavor regardless of A/B.

---

## C2 — "Why canvas at all, for an accessibility-first product?"

### The premise, stated explicitly (AGGREGATE #20)
The entire IME burden (docs/12), the native-shell burden (docs/11), and the in-tree-build burden (C1) **exist because of one upstream choice: rendering text on a custom canvas surface instead of in the DOM.** A DOM/`contenteditable` editor gets the full UIA Text pattern *for free* via Blink, with no mirror-DOM ceiling, because the thing the screen reader reads *is* the thing being edited. The premise of this project is that the canvas choice is worth that entire cost. That premise has not been written down and defended. Here it is, steelmanned both ways.

### Steelman — use DOM/`contenteditable`, not canvas
- **You get the real Text pattern free.** Blink already feeds `AXObject → AXTreeSource → AXPlatformNode`, the same shared layer this project targets [SOURCE — `docs/13` §central finding]. A contenteditable editor inherits caret/selection/word-line granularity, live caret events, and IME composition-announce with **zero** of the C1/IME/native-shell work.
- **No mirror-DOM ceiling — because there is no mirror.** The mirror-DOM ceiling (docs/13) only exists when pixels and semantics are computed separately. A DOM editor has one representation; layout *is* the accessible layout. The single hardest constraint in docs/11 ("one layout engine feeds both painter and AX tree") is satisfied *by construction*.
- **IME is solved.** A contenteditable/textarea is already a registered text input client; TSF/NSTextInputClient/IBus all work. The entire docs/12 mountain disappears.
- **It is the cheapest path to a shipping a11y-first editor**, which is ostensibly the product goal.

### Steelman — use canvas (the project's actual bet)
- **Contenteditable's a11y is only "free" until you need control.** Real high-end editors (Google Docs, Figma, VS Code/Monaco) abandoned contenteditable for canvas/custom surfaces precisely because contenteditable's layout, selection, and rendering are **not controllable enough** for collaborative cursors, virtualization of huge documents, pixel-stable cross-platform layout, and bidi/ligature fidelity [SOURCE — `docs/13` §canvas ceiling; Monaco/VS Code corroboration]. The moment you need that control, you are on canvas, and then the a11y question is forced.
- **The project's thesis is specifically about the *forced* canvas case.** This is not "should a generic editor use canvas?" — it is "*given* that high-end editors must go custom-surface, can they still get full native a11y fidelity instead of the capped mirror-DOM approximation everyone ships today?" [SOURCE — `docs/13` §synthesis]. The answer the project is building is "yes, via native `AXPlatformNode` below the web sandbox" — which the web cannot do because AOM virtual nodes died on privacy grounds [SOURCE — `docs/13` §AOM].
- **The native door clears a ceiling the web door structurally cannot** (mirror layout ≠ canvas layout for wrap/bidi/ligatures) — *argued, not yet measured* [SOURCE/VM — `docs/13` §2 + caveat; head-to-head measurement is backlog C4]. The whole point is to deliver the granularity contenteditable-via-mirror cannot.

### The crux
The two steelmen do not actually contradict — they answer **different questions**:
- If the product is *"a good accessible rich-text editor, as cheaply as possible,"* the DOM answer wins decisively. The canvas cost is unjustified.
- If the product is *"prove that custom-surface editors (the kind that must exist for Docs/Figma-class control) can get full native a11y fidelity, and build the producer that does it,"* canvas is not a choice — it is the **subject under test**. DOM would make the product trivial and the research pointless.

### RECOMMENDATION (C2)
**Recommendation: keep canvas, but only if the project's primary goal is the custom-surface fidelity thesis — and write that premise into `docs/00-INDEX` and `docs/13` explicitly.**

Concretely:
1. **State the premise in one sentence at the top of the index:** "Canvas is assumed because the target is the class of editors (Docs/Figma/Monaco-grade control) that *cannot* use contenteditable; the research question is whether that class can still reach full native a11y fidelity. If the goal is instead the cheapest accessible editor, use contenteditable and stop here." [Action — doc edit, the user's call on wording.]
2. **This decision is entangled with C3 (product vs standards as primary goal)** and cannot be fully settled without it. If "ship an editor" is primary and Docs/Figma-grade control is *not* actually required, the honest answer is **reconsider canvas**. If "prove the fidelity thesis / standards exhibit" is primary, canvas is load-bearing and stays.
3. **Lowest-regret next step before locking this in:** the C4 head-to-head (native canvas vs mirror-DOM contenteditable through a real screen reader, on the VM). That single measurement is what converts "canvas is worth it" from asserted to demonstrated — and it is also the cheapest way to *falsify* the entire canvas premise if the mirror-DOM ceiling turns out to be lower than claimed.

---

## S1 — SkParagraph a11y-geometry granularity spike

### The question restated (AGGREGATE #16; docs/11 open decision #2)
docs/11 names "one layout engine feeds both painter and AX tree" as the non-negotiable constraint, and proposes Skia's **`SkParagraph`/`SkShaper`** as the shared layout engine. The screen reader (UIA Text pattern) and **braille** need per-character, per-word, and per-line **bounds** plus caret rects [SOURCE — `docs/11` §non-negotiable constraint; `docs/13` §canvas ceiling]. **Does `SkParagraph` actually expose geometry at that granularity?** If not, the "one layout engine" constraint forces a **custom HarfBuzz layout engine**, which invalidates the "well-trodden Skia text / ~80% shared core" assumption — and that assumption is the backbone of the C1 cost model.

### What is knowable now (from sources, without the spike)
- **SkParagraph is Flutter's production text-layout stack** and is already in the Chromium/Skia tree, which is why docs/11 reaches for it [SOURCE — `docs/11` §shared core]. It does line-breaking, shaping (via SkShaper/HarfBuzz), and painting.
- **SkParagraph exposes *some* geometry**: it has APIs for rect-for-range (glyph/UTF range → bounding boxes, used for selection highlighting) and position-for-coordinate (hit testing), which is how Flutter draws selection and places carets [SOURCE — general SkParagraph API knowledge; **not verified against the pinned tag in this repo** — this is the gap the spike closes].
- **What is *not* knowable from desk reading**, and is exactly the risk: whether SkParagraph's rect-for-range output is (a) available at **word and line granularity** as discrete units the AT can walk (not just arbitrary char ranges the caller must re-segment), (b) **stable and exact** enough to match painted pixels for braille cursor-routing round-trips, and (c) exposes **caret/cluster boundaries** correctly under bidi, ligatures, and combining marks — the precise cases docs/13 names as the mirror-DOM ceiling. [VM/spike — unverified.]

### The exact spike to run later
Define S1 as a concrete, falsifiable investigation (runnable in the Linux lean checkout — does **not** require the Windows VM, so it can run before V1):

1. **Build a SkParagraph fixture** in the pinned checkout (`149.0.7827.115`) that lays out a mixed-content string: ASCII, a wrapped multi-line paragraph, a ligature (e.g. "ffi"), a bidi run (Latin + Hebrew/Arabic), and a combining-mark grapheme.
2. **Extract, for that layout:** (a) per-character bounding rects, (b) per-word bounding rects, (c) per-line bounding rects, (d) caret rect at each cluster boundary, (e) position-from-point hit results at known coordinates.
3. **Assert the UIA/braille requirements:** every grapheme cluster has a distinct, monotonic caret position; word and line units are derivable as contiguous discrete ranges (not requiring the caller to reimplement word segmentation); bidi runs produce visually-correct (not logical-order) rects; ligature/combining cases do not collapse two graphemes into one un-navigable cell.
4. **Decision output:** PASS = SkParagraph supplies the a11y geometry → the "~80% shared / well-trodden Skia" assumption holds → C1 cost model stands. FAIL (any of the granularities missing or wrong under bidi/ligature) = a custom HarfBuzz-based layout engine joins the shared core → re-cost the shared core upward and re-run C1.

**Estimated scope:** a focused fixture + assertion harness, comparable in size to one T2 test file; the value is the decision bit, not the code.

### RECOMMENDATION (S1)
**Recommendation: run S1 before finalizing the C1 cost model, and run it on the existing Linux checkout (it does not need the VM).**

1. **Sequence it ahead of the C1 operational spikes.** S1 is the cheapest input that can move the C1 answer, and it is not VM-gated — there is no reason to wait for Windows. It can run in parallel with the V1 NVDA slice.
2. **Treat a FAIL as a cost-model event, not a blocker.** A negative result does not kill the project; it adds a custom-layout line item to the shared core and *increases the pull toward reusing more of Chromium's text stack* (a mild C1 push toward in-tree). Either way the C1 numbers must be recomputed after S1.
3. **Record the result as PROVEN once the fixture runs** — this is one of the few C-series items that can earn a PROVEN tag without the VM, so it is unusually high-leverage for closing open questions cheaply.

---

## Summary of recommendations (cross-references)
- **C1:** Don't commit to monolithic in-tree A. Keep the proven standalone B spine for the a11y producer; scope A's justification narrowly to `ui/base/ime`; measure the security-rebase cost and the real dependency-cone size before committing. Default to a "B-plus-IME" hybrid. Gated on S1.
- **C2:** Keep canvas *only if* the primary goal is the custom-surface fidelity thesis (entangled with C3); write the premise explicitly into the docs; run the C4 head-to-head as the cheapest way to confirm or falsify the canvas bet. If the goal is merely "cheap accessible editor," reconsider canvas in favor of contenteditable.
- **S1:** Run the SkParagraph geometry spike now, on Linux, before trusting the C1 cost model; PASS keeps the 80/20 assumption, FAIL adds a custom layout engine and re-costs the shared core. Earns a PROVEN tag without the VM.
