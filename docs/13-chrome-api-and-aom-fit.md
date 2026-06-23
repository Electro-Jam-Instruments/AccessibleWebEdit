# 13 — Chrome Web Accessibility APIs, AOM, and How They Fit the Native Approach

Date: 2026-06-14. A deep dive synthesizing the project's native `ui/accessibility` work (docs/09–12, blite/) with the web-platform accessibility APIs — the **Accessibility Object Model (AOM)**, **EditContext**, **ariaNotify**, **HTML-in-Canvas**, and Chrome's web→platform AX pipeline — to answer: what is the best way to build this **as cross-platform as possible**, and how do these APIs fit our current work?

Sourcing note: web-API status below comes from a multi-agent primary-source investigation (2026-06-14). Browser version numbers and standards-position labels are cited but several primary hosts (chromestatus, MDN-rendered, caniuse) rate-limited automated fetches, so exact versions are corroborated via MDN browser-compat-data JSON, W3C/WICG raw markdown, and release notes; treat specific version numbers as "best-verified" and re-confirm before quoting externally. Confidence flags are inline.

## The central finding: two doors to a screen reader, one shared room

A custom canvas editor can reach a screen reader by exactly two architectures, and **both converge on the same `ui/accessibility` AXTree → AXPlatformNode layer this project has been testing**:

```
                        the SAME shared layer (verified Blink-agnostic)
                                         |
   NATIVE door (our B-lite)              v                WEB door (in a browser tab)
   own producer  --------> AXNodeData / AXTreeUpdate <-------- Blink AXObject (BlinkAXTreeSource)
                                  |   (also: Views UI, PDF)         ^
                                  v                                 |  needs a way to inject
                          BrowserAccessibilityManager               |  a11y for canvas content
                                  |                                  |  that has NO DOM
                                  v                                  |
                          AXPlatformNode (+ AXPlatformNodeDelegate)  |
                                  |                            (this injection primitive
              UIA / AT-SPI / NSAccessibility / NodeInfo         is AOM virtual nodes —
                                  |                              which never shipped)
                            screen reader
```

The Chromium pipeline is explicitly producer-agnostic. `AXTreeSerializer` "is designed so that it doesn't know anything about Blink … we're using AXTreeSerializer for other accessibility trees in Chromium outside of Blink" (how_a11y_works_2.md), and `AXPlatformNodeDelegate` is the generic contract any producer implements (how_a11y_works_3.md). Web content (Blink), the Views UI toolkit, and PDF all feed this same layer. **Our native producer is therefore the same kind of citizen as Blink and PDF — confirming docs/09 Finding 3 at the architecture level.** This is the load-bearing fact for everything below.

## AOM: what it was, and what actually became of it

AOM was a WICG proposal (Google/Apple/Mozilla) to give JavaScript control over the accessibility tree. Its motivating problem is *exactly this project's domain*: "when the UI is custom-drawn, there aren't any DOM elements to add ARIA attributes to in order to make it accessible" (WICG/aom explainer). It was split into phases; here is the verified 2026 status:

| AOM piece | What it was | Status 2026 | Where it lives now |
|---|---|---|---|
| ARIA attribute reflection (`element.role`, `element.ariaXXX`) | ARIA as IDL properties | **SHIPPED** — string refl. Chrome 81 / FF 119 / Safari 12.1 (Baseline ~Oct 2023); element refs Chrome 135 / FF 136 / Safari 16.4 (2025) | folded into **W3C ARIA 1.2** + WHATWG HTML |
| `ElementInternals` default semantics for custom elements | custom-element default ARIA | **SHIPPED (Chrome; broadening)** | WHATWG HTML |
| **Virtual Accessibility Nodes** (`canvas.attachAccessibleRoot()` → `appendChild(new AccessibleNode())`) — *the canvas solution* | a11y nodes with no DOM backing, for canvas | **DEAD / "Speculative, Blocked"** — never specified beyond a draft, never shipped even behind a flag | abandoned (privacy) |
| Computed accessibility tree (`getComputedAccessibleNode`, getComputedRole/Label) | read computed AX tree from JS | **TEST-ONLY** — WebDriver `getComputedRole`/`getComputedLabel` only; no page JS API | WebDriver/WPT; Core-AAM |
| AT input events / accessible actions (increment, dismiss, scroll from AT) | AT actions delivered to JS | **ABANDONED** across engines (Safari-only "synthesized keyboard events" survives) | dropped |

**Why virtual nodes — the one phase that would make a *web* canvas editor natively accessible — died:** privacy/AT-fingerprinting. The explainer states verbatim: "due to a number of complications including privacy concerns, the working group is not pursuing virtual nodes as intended." The core blocker (WICG/aom #144, W3C TAG design-principles #293): "any events fired on virtual nodes would be an immediate indication that the user must be using assistive technology, which is private information." AOM as a unified project is effectively dormant — last meaningful WICG/aom activity was June 2024, and that was off-boarding the Reference-Target work to WICG/webcomponents.

**The proposed canvas API (for the record), since it is what our native producer mirrors:**
```js
canvas.attachAccessibleRoot();
let table = canvas.accessibleRoot.appendChild(new AccessibleNode());
table.role = "table"; table.colCount = 10; table.rowCount = 100;
// ... build a virtual a11y subtree with no DOM
```
This is structurally identical to what our B-lite producer does in C++ via `AXTreeUpdate`. (Note: an earlier draft method name `getOrCreateVirtualAccessibilityRoot()` could not be verified in any web spec — the web proposal was `attachAccessibleRoot()`; that name is a Chromium-internal/Views term.)

## EditContext: solves input, explicitly NOT accessibility

EditContext (shipped **Chrome/Edge 121, Jan 2024; Chromium-only** — not Safari/Firefox, though both are positive/prototyping) lets a canvas editor participate in the OS text-input/IME protocol *without* a hidden contenteditable. It directly addresses the docs/12 IME problem on the web: composition events fire on the EditContext (`compositionstart`/`compositionend`, `textupdate`, `textformatupdate`), and `characterboundsupdate` → `updateCharacterBounds()` feeds IME candidate-window placement (plus `updateControlBounds`/`updateSelectionBounds`).

**Critical distinction, verified from MDN verbatim:** EditContext is *input/IME plumbing only — it does not touch the accessibility tree.* (The one a11y-adjacent thing it does: attaching an EditContext makes the element focusable / part of the focus order — focus participation only, no name/role/value/caret exposure.) "If you use the EditContext API with a `<canvas>` element, make sure to also make the text accessible to assistive technology. Screen readers can't read the text in a `<canvas>` element. … you could maintain a separate view of the text in an offscreen DOM element." Real-world confirmation: VS Code/Monaco keeps a *separate* `ScreenReaderSupport` class alongside its EditContext controller, and there is an open VS Code issue to turn EditContext *off* for screen-reader users.

So on the web the editor stack is **three separate responsibilities**: EditContext (input) + an offscreen DOM mirror (accessibility) + ariaNotify/live-regions (announcements). EditContext closes the IME half of docs/12; it does nothing for the AX half.

## Canvas accessibility on the web today — and its ceiling

Because virtual nodes never shipped, every real web canvas editor uses the **mirror / offscreen-DOM pattern**: render pixels on canvas, maintain a parallel ARIA-annotated DOM that the screen reader reads. Google Docs' 2021 canvas migration kept a hidden "side DOM" for exactly this; Figma "translates canvas elements into HTML in accessibility mode." Supporting primitives: focusable fallback content inside `<canvas>`, `drawFocusIfNeeded()` (focus ring on canvas mapped to a focused fallback element; broadly supported since ~2016), and ARIA. The old `addHitRegion()` API that would have routed events/semantics was **removed from the WHATWG standard** and never replaced.

**The ceiling (why this matters for an *editor* specifically):** screen readers navigate text through platform text APIs — UIA Text pattern, IAccessible2 `IAccessibleText`, ATK/AT-SPI `Text`, AX `AXTextMarker` — which expect **character offsets, word/line/paragraph granularity, caret offset, and live caret-move events**. A static `role="img"`/`aria-label` mirror conveys a name but none of that. A *hidden contenteditable* mirror can expose a caret/selection but must stay byte-for-byte synced with the canvas's own layout (wrapping, bidi, ligatures), and the granularity the AT reports then reflects the mirror's layout, not the canvas's. This is precisely the rich Text-pattern semantics this project verified natively in docs/09–10 and the T2 tests — and it is the thing the web mirror approach structurally cannot deliver faithfully. **The native door gets the real Text pattern; the web door gets an approximation.**

## New 2024–2026 developments worth tracking

- **ariaNotify** — the live successor that graduated from this space, now BCD-confirmed standard-track (not experimental). `element.ariaNotify("message", {priority})` tells a screen reader what to announce, decoupled from DOM mutation — exactly the canvas problem of announcing something with no DOM change. **Chrome 141** (per browser-compat-data, *partial*: fully supported on Windows and Linux, **method exposed on macOS but notifications not reliably spoken, no ChromeOS support**), **Firefox 150**, **Safari not implemented**; spec home `w3c.github.io/aria/#ARIANotifyMixin` (ARIA 1.3 ED). The per-platform unevenness matters for our cross-platform story: ariaNotify is reliable on exactly the two desktop platforms (Win/Linux) where we'd lean on it, weak on macOS. Maps directly onto our edit-origin / live-announcement open questions (NEXT-QUESTIONS #3, #4).
- **HTML-in-Canvas (`drawElement`/`drawElementImage`)** — a genuinely new WICG approach (behind `chrome://flags/#canvas-draw-element`, Chromium ~147 Canary/Brave): draw real DOM elements *into* the canvas, so "elements drawn into the canvas will match their corresponding canvas fallback" — i.e., the visual and the accessible representation come from the same DOM, sidestepping the mirror-sync problem. This is the most promising *web* path for canvas a11y and is worth watching, though early and flag-gated. [Confidence: flag/version from secondary sources.]

## Synthesis: how it all fits our work

**1. Our native approach is not a workaround for a missing web API — it is the architecturally correct place to do this.** The web's missing primitive (AOM virtual nodes) is precisely what our B-lite producer *is*, implemented in C++ below the web sandbox. And the reason virtual nodes died — AT-fingerprinting of a web origin — **does not apply to a native application**, which is not a web origin being fingerprinted. The privacy objection that blocked the web API is a non-issue natively.

**2. The native door is positioned to deliver fidelity the web door cannot — argued, not yet measured.** Feeding `AXPlatformNode` directly exposes the full UIA Text pattern, the delta-driven editing events, caret/selection geometry, and action channel that docs/09–10 + the T2 tests validated **at the generation layer**. The web mirror-DOM approach is, in principle, capped below that ceiling (mirror layout ≠ canvas layout for wrap/bidi/ligatures). **Caveat (expert review 2026-06-16):** the *magnitude* of that gap is asserted, not measured — Google Docs/Figma ship mirror-DOM canvas editors at scale, and "native is *superior*" needs a head-to-head native-vs-mirror comparison through a real screen reader (review backlog C4) plus the VM run that proves native fidelity in the first place. State it as "native can reach a ceiling the web mirror cannot, pending end-to-end measurement," not as established superiority.

**3. This project is the standards exhibit for reviving the web equivalent.** docs/09's standards-track note — "a web-facing API could be as small as: post tree deltas plus tree data, receive actions" — is now backed by a working native proof. Virtual nodes stalled on privacy *and* on API-design complexity; a delta-based child-tree API (the shape PDF and our producer use) is a cleaner proposal, and we have the evidence it works.

**4. One producer core, multiple outputs.** The B-lite producer's `AXTreeUpdate` vocabulary is the same whether it feeds native `AXPlatformNode` today or, hypothetically, a future web virtual-node/child-tree API. The investment is not platform-locked.

## Cross-platform recommendation (answering the question directly)

| Strategy | Cross-platform reach | A11y fidelity | Cost | When |
|---|---|---|---|---|
| **Native, in Chromium tree** (docs/11 Strategy A) | all 5 via AXPlatformNode | **full** (UIA Text pattern etc.) | per-platform shells + heavy build | the product; validated path |
| **Web app** (EditContext + mirror DOM + ariaNotify) | all 5 *free* via browser | **capped** at mirror-DOM ceiling | lighter, but a11y ceiling is real | reach/zero-install, accepting limits |
| **Standards play** (revive delta child-tree / virtual nodes) | would make web door full-fidelity | n/a (future) | standards effort | the long game; we hold the evidence |

**Recommendation:** lead with **native (Strategy A)** for the shipping product — it is the only path that delivers the rich editing accessibility this project is *about*, and `AXPlatformNode` already covers all five platforms. If a browser deployment is also required, build it as **EditContext + offscreen-DOM mirror + ariaNotify** and *accept the documented ceiling*, while tracking **HTML-in-Canvas** as the web path that could eventually lift it. Separately, treat the native producer as the **standards exhibit** to push a delta-based web child-tree API — the project is uniquely positioned to make that case with working evidence rather than a proposal.

## How the web APIs map onto our findings (concrete tie-ins)

- **AOM virtual nodes ↔ our B-lite producer / AXTreeUpdate** — same concept; ours is native and unblocked.
- **AOM AT input events (abandoned on web) ↔ our `AXActionTarget`** (docs/09 Finding 3, T2-6) — the two-way action channel the web gave up on, we have natively.
- **AOM computed tree (test-only WebDriver) ↔ our `ax_dump_tree`/`ax_dump_events`** — the native equivalent of the introspection the web kept test-only.
- **ariaNotify ↔ edit-origin / live announcements** (NEXT-QUESTIONS #3, #4) — the web's answer to "announce a non-DOM change"; informs how we model remote-edit and status announcements.
- **EditContext ↔ docs/12 IME** — closes the IME half on the web; the AX half still needs the mirror, exactly as our native host still needs the TSF→OnActiveComposition wiring.
- **`kRichlyEditable` / caret-bounds / intents findings (T2)** — native-tree facts a faithful web API would also have to express; evidence for what any web child-tree proposal must carry.

## Open decisions for planning

1. **Product surface(s):** native-only, web-only, or both from one producer core? Drives everything.
2. If web is in scope: accept the mirror-DOM a11y ceiling, or invest in HTML-in-Canvas / a standards push?
3. **Adopt ariaNotify** (with live-region fallback) for the announcement gaps the AX tree can't express as mutations — relevant even to the native story's cross-platform announcement quality.
4. **Standards contribution:** is pursuing a delta-based web child-tree API (using this project as the proof) a goal, or strictly an internal product?

## Key sources (with confidence)

Verified primary (high confidence): WICG/aom explainer (raw) and spec drafts; W3C/MSEdge EditContext explainers (raw) and MDN EditContext markdown; MDN browser-compat-data JSON (EditContext 121; reflection versions; drawFocusIfNeeded); Chromium accessibility docs (overview.md, how_a11y_works_2/3.md) for the shared-pipeline claim; W3C ARIA PR #2577 / WebKit standards-positions #370 for ariaNotify; WICG/aom #144 + W3C TAG design-principles #293 for the virtual-nodes privacy blocker; WICG/html-in-canvas README.
Best-verified (re-confirm versions before external citation): ariaNotify Chrome 141 / Firefox 150 / Edge OT; HTML-in-Canvas Chromium 147 flag; ElementInternals exact per-browser versions; WebKit/Firefox EditContext + ariaNotify shipping (positions confirmed, shipping not). Full URL lists are in the 2026-06-14 research transcripts.
