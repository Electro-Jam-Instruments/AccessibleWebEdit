# AccessibleWebEdit — Master Task List (prototype-to-NVDA)

**Goal:** make a *true* prototype go — a custom (non-Blink) editor surface driving
Chromium's `ui/accessibility` → `AXPlatformNodeWin`/UIA → **NVDA actually announcing**
an edit, on this Windows 11 box. This is the file the work loop reads each iteration.

**Single source of truth.** Update status here as work completes. Status legend:
`TODO` · `WIP` · `BLOCKED(reason)` · `DONE(evidence)`.
Evidence levels stay tagged **PROVEN / SOURCE / VM** per repo convention — never
upgrade to PROVEN without an actual passing run.

**Box facts:** Ryzen 9 7945HX (16C/32T), 30 GB RAM, 783 GB free on C:. NVDA installed.
VS BuildTools @ `C:\BuildTools` (cl 14.44 + Win11 SDK 26100). depot_tools @ `C:\src\depot_tools`.
Chromium checkout target: `C:\src\chromium\src`, tag `149.0.7827.115`.

---

# ★ ACTIVE ROADMAP — make it a REAL editor (the loop works these top-down) ★

V1 is PROVEN (NVDA says "edit, hello" — `results/nvda-v1-PROVEN.log`). Now build it
into a real rich editor + tables, each feature verified the SAME way:
**(a) build producer infra (editor model + Bridge + AX nodes) → (b) verify with a
native UIA probe `scripts/uiaprobe*.cpp` → (c) verify with NVDA No-speech capture →
(d) add the coupled visual rendering.** Tick each sub-step with evidence. Keep the
single layout pass (pixels == a11y geometry) and the deferred-a11y-off-input pattern.

## Phase 3 — Rich text
- [x] 3.1 **Mixed-format runs** — DONE PROVEN (infra+probe+visual+NVDA+robustness).
  - [x] editor per-char `CharStyle` + `runs()` accessor (maximal same-style spans); backward-compatible.
  - [x] field `kNonAtomicTextFieldRoot` → attribute resolution moves field→StaticText (verified, no regress).
  - [ ] **per-run Bridge (NEXT — precise design):**
        1. Editor: mixed initial content via a ctor, SINGLE-LINE for now, e.g. text `"Bold plain"`, chars 0-3
           `{bold}`, 4-9 `{}`. (Changing default content is fine; V1 proof already banked.)
        2. Refactor `FillRunAttributes` to take a `const CharStyle&` (not the editor) -> set kFontWeight/
           kTextStyle(italic)/kTextUnderlineStyle from the style.
        3. Bridge `BuildInitialTree`+`BuildEditDelta`: for run i in `editor.runs()` emit StaticText id `3+2*i`
           + InlineTextBox id `4+2*i`, name = `text.substr(run.start,run.length)`, attrs = FillRunAttributes(style).
           Field `child_ids = {3+2*i ...}`. Single run -> kText=3/kInline=4 (back-compat). Keep field+each
           StaticText non-navigable (HidesChildrenFromUIA already does text-field+static-text).
        4. Selection: caret global offset c -> run r = last run with start<=c; local=c-start; sel anchor/focus
           object = inline box `4+2*r`, offset=local.
        5. Host: replace the fixed `{kRoot,kField,kText,kInline}` delegate loop with `MaterializeDelegates()`
           that WALKS the tree (root+descendants) creating one BliteNodeDelegate per node; add
           `RematerializeDelegates()` and call it after EVERY `tree_->Unserialize(...)` (ApplyLocalEdit,
           ApplyClientSetValue, the scripted edit) to add new run-node delegates + drop stale ones.
        6. Bounds: add host `RunGlobalStart(AXNodeID)` (sum preceding runs' lengths via field children order);
           `NodeScreenBounds`/`InnerTextRangeScreenBounds` map a run node's LOCAL offset -> global = start+local
           -> screen rect (single-line: x=origin_x+global*advance).
  - [x] probe: extend `uiaprobe_attrs` -> clone DocumentRange, MoveEndpointByUnit(Character) to cover run1 vs
        run2, GetAttributeValue per sub-range = per-run; whole range = `UiaGetReservedMixedAttributeValue`
        (VT_UNKNOWN). Also re-run uiaprobe (flat leaf intact) + uiaprobe_select (ExpandToEnclosingUnit no crash).
  - [x] NVDA: reads the mixed field, no crash (results/nvda-mixed.log: Speaking ['edit',...,' plain']). NVDA
        announces the focused run (non-atomic reads from the caret). Attr-difference voicing needs NVDA's
        'report font attributes' setting; per-run attrs are probe-proven.
  - [ ] visual: WM_PAINT iterate runs, CreateFontW per run from run.style, draw each run at its layout x.
- [x] 3.2 **Bulleted & numbered lists** — DONE PROVEN (infra+probe+NVDA+visual; results/blite-list.png) (block structure — precise design):
      1. Editor: add per-LINE block type. `enum BlockType { kParagraph, kBullet, kNumber }` + a
         `std::vector<BlockType> line_blocks_` (one per line, indexed by line number; default kParagraph).
         Helper `block_runs()` -> contiguous lines of the same list type grouped into a list (start line,
         count, ordered?). A new line inherits the current line's block type.
      2. Bridge: this is the FIRST block-level tree (so far field->runs is flat). Build per LINE:
         a paragraph line -> StaticText run(s) directly under field; a maximal group of kBullet/kNumber lines
         -> a `kList`(ordered for kNumber) node whose children are `kListItem` nodes; each kListItem has a
         `kListMarker`(name "•" for bullet, "N." for number) + the line's text run(s). Set
         `IntAttribute::kPosInSet` (1-based) + `kSetSize` on each kListItem. Field child_ids = the ordered mix
         of paragraph runs + kList nodes. Keep dynamic ids (reconcile handles add/remove). HidesChildrenFromUIA:
         the list/listitem/marker ARE navigable (don't hide) -- only text-field/static-text stay leaves; the
         list is real structure NVDA navigates. (May need to NOT make the field a single flat leaf when it has
         block children -- reconsider HidesChildrenFromUIA for the field-with-list case.)
      3. Selection/caret + bounds: map caret global offset -> the owning run's inline box (as today), and the
         per-line y from LayOut. List markers take an x-indent in LayOut (origin_x + marker_width).
      4. probe (add `uiaprobe_list.cpp`): walk UIA tree, assert List(50008)->ListItem(50007) with
         `CurrentPositionInSet`/`CurrentSizeOfSet` + the marker text; ordered list shows numbers.
      5. NVDA: capture -> "list", "bullet"/number, "N of M", item text. (Re-verify no crash with the reconcile.)
      6. visual: WM_PAINT draws the marker ("•" / "N.") at the item's indent, text after it; coupled to the
         same LayOut (marker advance reserved in the line's x).
      NOTE: lists are NAVIGABLE structure, a departure from the flat-leaf field. Watch the HidesChildrenFromUIA
      interaction + NVDA browse/focus mode. Start with a fixed demo doc (1 paragraph + a 3-item bullet list),
      prove it via probe+NVDA, THEN wire Tab/Enter editing.
- [x] 3.3 **Heading levels** — DONE PROVEN (infra+probe+NVDA+visual).  -- infra: `kHeading` + `IntAttribute::kHierarchicalLevel` (1-6). probe: UIA
      heading level (AriaProperties/`LevelId`). NVDA: "heading level N". visual: larger/bolder headings.
- [ ] 3.4 **Fonts & font weights.** infra: `StringAttribute::kFontFamily` + `FloatAttribute::kFontWeight`
      per run. probe: `UIA_FontNameAttributeId` + `UIA_FontWeightAttributeId` per run. NVDA: font/weight
      reported. visual: paint different families/weights.

  - [x] 3.1-ROBUST **delegate lifetime for dynamic run count** -- DONE: MaterializeDelegates is a full
        reconcile (drops removed-node delegates after Unserialize, before event firing). Verified NO crash with
        run removal 2->1 + NVDA reads (results/nvda-mixed.log). [original note:] (do BEFORE relying on typing): a run
        REMOVED on edit frees its AXNode, but MaterializeDelegates is add-only -> stale delegate dangles ->
        NVDA reentrant get_accChild -> DCHECK !GetTreeUpdateInProgressState crash. FIX: BliteAXHost observes
        the tree (AXTreeObserver), drop delegates_[node->id()] in OnNodeWillBeDeleted. (Scripted edit no longer
        collapses runs as a stopgap.)

## Phase 4 — Tables
- [ ] 4.1 **Text in a table cell.** infra: `kTable` → `kRow` → `kCell`(text), row/col span + indices.
      probe: UIA Grid/Table/GridItem patterns + cell text + RowCount/ColumnCount. NVDA: "table", row/col,
      cell content on navigation. visual: paint grid + cell text.
- [ ] 4.2 **Cell selection via ISelectionProvider2.** infra: selection on cells; `ISelectionProvider2`
      (FirstSelectedItem/LastSelectedItem/CurrentSelectedItem) — `patches/` has a sketch. probe: GetSelection
      + SelectionProvider2 members. NVDA: selected cells announced. visual: highlight selected cells.

## Phase 5 — Editor visuals (looks like a basic editor)
- [ ] 5.1 Unified rendering of all the above (caret, selection highlight, list markers, heading sizes,
      table grid) — coupled to the a11y geometry via the one layout pass.
- [ ] 5.2 Interactive polish in `--viewer` (type, navigate, select) reads correctly through NVDA.

## Completion gate
- [ ] When 3.1–3.4, 4.1–4.2, 5.1–5.2 are PROVEN (probe + NVDA + visual), create
      `results/PROTOTYPE-COMPLETE` with the evidence index. That ends the loop.

---

## Phase 0 — Toolchain setup
- [x] 0.1 VS Build Tools (C++ + Win11 SDK) — **DONE** (verified cl.exe 14.44 + SDK 26100, `vs2022_install=C:\BuildTools`)
- [x] 0.2 depot_tools cloned to `C:\src\depot_tools`, `DEPOT_TOOLS_WIN_TOOLCHAIN=0`, git perf config — **DONE**
- [x] 0.3 Chromium fetch + `gclient sync -D` at pinned tag — **DONE** (checkout built repeatedly at tag 149)
- [x] 0.4 NVDA installed — **DONE**; still TODO: create pinned No-speech config profile → `tests/nvda-config/`

## Phase 1 — Reproduce the baseline (canonical build)  [unblocks on 0.3]
- [x] 1.1 Copy tests into `//ui/accessibility/`, apply patch — **DONE**
- [x] 1.2 `gn gen` — **DONE** (`is_component_build=true dcheck_always_on=true`, +`use_remoteexec=false use_siso=false`)
- [x] 1.3 `ninja ax_t2_unittests` → `--gtest_filter=AX*` → **DONE PROVEN 109/109** (`results/phase1-windows-run.txt`)
- [x] 1.4 Recorded → closes **V10**. (SAC was the blocker; user disabled it.)

## Phase 2 — V1: the falsifying end-to-end slice  [the heart of "make it go"]
- [x] 2.1 B-lite spine on Windows — **DONE** (event set matches Linux; host builds in `out\host`)
- [x] 2.2 **UIA host** `blite/blite_host_win.cc` — **DONE PROVEN.** Win32 HWND + `BliteNodeDelegate` over the
      AXTree + `AXPlatformNodeWin` + `AXFragmentRootWin`. A native `IUIAutomation` client sees
      `Window → RootWebArea → Edit(value, focus, Text pattern, caret) → Text`. Four delegate fixes:
      `GetTargetForNativeAccessibilityEvent`→HWND, `GetFocus`, `GetFromNodeID`+`GetUnignoredSelection`,
      non-platform `AXTreeManager`. (`results/V1-end-to-end.md`; probe `scripts/uiaprobe.cpp`.)
- [x] 2.3 Drive 1 insert + 1 caret move; capture events — **DONE PROVEN.** Host fires
      `kTextChanged`+`kTextSelectionChanged`+`kValueChanged`; a *subscribed* native client receives all four
      (`scripts/uiaprobe_events.cpp`: focus→Edit, TextChanged, SelectionChanged, ValueChanged).
- [x] 2.4 Run NVDA (No-speech synth, log), capture utterance, assert announcement — **DONE PROVEN
      2026-06-27.** NVDA spoke our field `Speaking ['edit', ..., 'hello']` (`results/nvda-v1-PROVEN.log`).
      Needed: flat UIA leaf (`HidesChildrenFromUIA`) + `base::i18n::InitializeICU()` in main (NVDA's
      word/char nav → ICU break iterator → FATAL without ICU). Autonomous capture: launch NVDA No-speech
      → launch host (stdout→file) → grep log → `nvda -q`.
- [~] 2.5 **Verdict** — **PARTIAL: PROVEN at the native-UIA-client level** (the faithful API NVDA uses);
      `results/V1-end-to-end.md` written. Bidirectional flow also proven: events out, reads in, **writes in**
      (`IValueProvider::SetValue` round-trips via `AccessibilityPerformAction(kSetValue)`). NVDA *speech* leg = 2.4.

## Phase 3 — Widen verification (V2–V4)
- [ ] 3.1 V2: UIA finalize for T2-1/2/3/5 under `ax_dump_events`; T2-4 IME round-trip; T2-6 actions; T2-7 ordering
- [x] 3.2 V3: raw `ITextRangeProvider` conformance — **DONE PROVEN.** Range reads (`GetText`→'hello!') and
      cursor routing (`Select`→hr=0x0, reaches `kSetSelection`, selection round-trips) work. Root fix:
      `BliteTreeManager : AXPlatformTreeManager` (range `GetOwner()` needs a real platform tree manager) +
      `GetFromTreeIDAndNodeID` + an inline-text-box leaf for whole-text positions. `scripts/uiaprobe_select.cpp`.
- [ ] 3.3 V4: real typing through NVDA keyboard hook + review cursor + **braille** capture
- [x] 3.4 R1 (owner request): rich-text run attributes — **DONE PROVEN.** `GetAttributeValue` → FontWeight=700,
      IsItalic=TRUE, UnderlineStyle=1; bold->normal change shows FontWeight 700→400 on edit. Attrs on the
      (atomic) field where `GetLowestPlatformAncestor` resolves. `scripts/uiaprobe_attrs.cpp`. (Mixed-format
      runs would need a non-atomic per-run structure — noted in V1-end-to-end.md.)

## Phase 4 — Prototype patches (`patches/`)  [standards exhibits]
- [ ] 4.1 SelectionProvider2 — build + measure
- [ ] 4.2 ITextProvider2 / RangeFromAnnotation — build + measure
- [ ] 4.3 TEXT_ATTRIBUTE_CHANGED UIA event — build + measure
- [ ] 4.4 annotation author/datetime — build + measure

## Phase 5 — Decisions surfaced (data, not silent calls)  [can advance offline]
- [ ] 5.1 C1: Strategy A (in-tree) vs B (standalone) cost model
- [ ] 5.2 C2: "why canvas at all for an a11y-first product" — make premise explicit/defend
- [ ] 5.3 S1: SkParagraph a11y-geometry granularity spike (gates C1)

---

## Work-loop protocol (how each iteration runs)
1. Read this file. Read `scripts/chromium-fetch.done` if present (OK/FAIL).
2. If fetch still running (`.done` absent): advance an **offline-capable** task —
   draft `blite_host_win.cc` from docs/03 + AXPlatformNode API, prep Phase 1 copy commands,
   build the NVDA config profile, or work Phase 5 decisions. Do NOT idle.
3. If fetch done OK: proceed to Phase 1 → 2 in order.
4. After each unit of work: update status + evidence here; commit nothing unless asked.
5. **Completeness gate before stopping:** the prototype is "go" only when 2.5 has a real
   NVDA speech log proving an edit was announced. Until then, keep working or schedule the
   next iteration.

## Chromium fetch is a DETACHED scheduled task (survives session restart)
- **Task:** `AWE-ChromiumFetch` (Task Scheduler, runs as current user, interactive session). Script: `scripts/_fetch-chromium.ps1`.
- **NOT a harness-tracked job** — so after a session restart, nothing auto-notifies on completion. On "continue", CHECK it explicitly:
  - Status: `Get-ScheduledTask AWE-ChromiumFetch | Select State` and `Get-Content scripts/chromium-fetch.log -Tail 15`.
  - Done marker: `scripts/chromium-fetch.done` → `OK 0` (success) or `FAIL ...`.
  - If `.done` absent and task `Running` → still fetching; **arm a poll** (ScheduleWakeup ~20 min) and proceed to offline prep meanwhile.
  - If `.done` = OK → start Phase 1 (canonical build).
  - If task `Ready`/stopped but `.done` absent → it died; re-run: `Start-ScheduledTask AWE-ChromiumFetch`.

## >>> SAC blocker RESOLVED (2026-06-25) — 109/109 PASS on Windows <<<
User disabled Smart App Control (live, no reboot; `VerifiedAndReputablePolicyState=0`). Built exes run now. **`ax_t2_unittests.exe --gtest_filter=AX*` → `[109/109] SUCCESS` (exit 0).** Phase 1 / V10 PROVEN on Windows (evidence `results/phase1-windows-run.txt`). Remaining: B-lite host run + NVDA capture (NVDA may collide with the user's own screen reader — coordinate). Re-enable SAC when done.

## Running log (newest first)
- 2026-06-26 (later): **Cursor routing + text ranges + rich-text attributes ALL WORKING.** Root cause of the
  earlier range failures: `AXPlatformNodeTextRangeProviderWin::GetOwner()` returns null unless the position's
  manager is a real `AXPlatformTreeManager` resolving via `GetPlatformNodeFromTree` -> a null owner failed every
  range method (`GetText`/`Select`/`GetAttributeValue`). Fixes: `BliteTreeManager : AXPlatformTreeManager`
  (routes to our nodes, no nav collapse) + `GetFromTreeIDAndNodeID` + inline-text-box leaf for whole-text
  positions. Now: range reads (`GetText`='hello!'), cursor routing (`Select` hr=0x0 -> `kSetSelection`),
  rich-text attrs (FontWeight=700/IsItalic/UnderlineStyle=1) + change detection (700->400). New probes
  `uiaprobe_select.cpp`, `uiaprobe_attrs.cpp`. Full 6-flow regression green. Phase 3.2/3.4 DONE.
- 2026-06-26: **V1 PROVEN at the native-UIA-client level + bidirectional flow.** A non-Blink AXTree now presents
  to native `IUIAutomation` (NVDA's API) as a complete editable field — Edit type, value, keyboard-focused, Text
  pattern, degenerate caret — and a subscribed client gets all four events (focus/text/selection/value). Client
  reads work; client **writes** work (`SetValue` round-trips via `AccessibilityPerformAction(kSetValue)`). Four
  delegate fixes (HWND association was the breakthrough). Probes: `scripts/uiaprobe{,_events,_action,_select}.cpp`.
  Open: NVDA *speech* on an unlocked desktop (2.4, needs user); `ITextRangeProvider::Select` cursor routing (V3,
  blocked at the UIA range layer); rich-text attributes (R1). Details: `results/V1-end-to-end.md`.
- 2026-06-24 13:25: **Phase 2 host (2.2) COMPILES vs real tag-149 headers** — `blite/blite_host_win.cc` clang-cl exit 0; all TODO(VM) APIs resolved to the real API (table in blite/blite_host_win.README.md). Built via `group("blite_win")` in the checkout BUILD.gn. Final link (V8 closure) building in background. Cannot RUN (SAC) → fidelity still VM-unverified. This is real de-risking of the V1 code.
- 2026-06-24 10:44: **ROOT CAUSE = Smart App Control ON.** Blocks running all unsigned built exes (test exe `0xc0e90002`; static build `WinError 4551` on a Rust build-script). Needs user to disable SAC + reboot (irreversible). This gates the entire run/NVDA goal.
- 2026-06-24 10:40: **Phase 1 build PROVEN on Windows** — `ax_t2_unittests.exe` compiles+links canonically vs the real tag-149 checkout (109 tests built). Fixed en route: target name, `git apply -p0`, `vs2022_install`, local build (no RBE/siso), missing `dxil.dll` (in-tree workaround), and a latent patch bug (missing `no_exit_time_destructors` config — fixed in tree AND the committed patch). **Test RUN blocked:** component-build exe exits `0xc0e90002` pre-`main()` (static-init failure; cdb-confirmed not a crash). Tests already PROVEN on Linux → prototype not blocked. Static (non-component) build launched as the fix (`scripts/_phase1-static.ps1`).
- 2026-06-24 06:24: OFFLINE prep done while fetch downloads — (2.2) `blite/blite_host_win.cc`+BUILD+README drafted [SOURCE/VM, has TODO(VM) APIs to verify on first compile]; Phase-1 build driver `scripts/_phase1-build.ps1` + NVDA capture `scripts/_nvda-capture.ps1` + `tests/nvda-config/` authored & parse-validated; decisions (5.1/5.2/5.3) → `docs/research/decisions-C1-C2-S1.md`; living report → `results/DAILY-REPORT-2026-06-24.md`. Fetch confirmed healthy (live git-remote-https). Poll armed.
- 2026-06-24: Detached fetch as scheduled task `AWE-ChromiumFetch` (restart-proof) + permissions (acceptEdits + build allowlist + C:\src) configured.
- 2026-06-24: Stop-hook work loop armed (`.claude/settings.json` + `scripts/stop-loop-hook.sh`, all 6 rail cases tested). Needs a session restart to load. Chromium fetch downloading (3.6 GB into `src` clone).
- 2026-06-24: Phase 0 toolchain done (BuildTools verified cl.exe+SDK; depot_tools; NVDA); Chromium fetch launched (background). Master list created.

## Autonomous work loop — how it's wired
- **Mechanism:** project `Stop` hook → `scripts/stop-loop-hook.sh`. Blocks stopping while there's buildable work; feeds back "read MASTER-TASKLIST + CLAUDE.md, do next task."
- **Safety rails (any one allows a normal stop):** `scripts/STOP-LOOP` kill-switch · `results/PROTOTYPE-COMPLETE` done-sentinel · `scripts/chromium-fetch.done` absent (don't spin during the long download) · `scripts/.loop-count` > 40 runaway cap (auto-resets).
- **To stop the loop at any time:** create `scripts/STOP-LOOP` (e.g. `New-Item scripts\STOP-LOOP`).
- **Marks "really done":** create `results/PROTOTYPE-COMPLETE` only after Phase 2.5 has a real NVDA speech log.
