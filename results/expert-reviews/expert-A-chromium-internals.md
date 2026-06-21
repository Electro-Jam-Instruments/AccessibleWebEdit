# Expert A — Chromium `ui/accessibility` internals review

Lens: implementation correctness against Chromium 149.0.7827.115 source (line-level), verified against `/home/user/chromium/src`. Three passes: generated-event tests + B-lite spine; bridge contract (docs/03); the four prototype patches.

## SUCCESSES

**[HIGH] The core editable-text event chain is correctly verified.** `FireValueInTextFieldChangedEventIfNecessary` (ax_event_generator.cc:1124-1148) fires BOTH `EDITABLE_TEXT_CHANGED` and `VALUE_IN_TEXT_FIELD_CHANGED` on the text-field ancestor, gated by `CanContributeToValueOfTextfield` + `text_field_ancestor != target_node`. The T2-1 tests target the field (node 2) from a name change on its static-text child (node 3) — node targets are exactly right. Not tautological.

**[HIGH] The caret-bounds correction is a genuine, source-backed fix to the project's own earlier docs.** `kCaretBounds` fires `CARET_BOUNDS_CHANGED` on the node itself (ax_event_generator.cc:920-922), NOT via sel_ fields. The T2-3 caret-bounds test (unittest:246-261) and docs/03 §2.3 correctly retract docs/09's wrong claim. Honest self-correction with exact line cite.

**[HIGH] The two-selection-events contract (docs/03 §2.2) is exactly right.** `OnTreeDataChanged` (ax_event_generator.cc:945-960) fires `DOCUMENT_SELECTION_CHANGED` at root + `TEXT_SELECTION_CHANGED` on `selection_focus->GetTextFieldAncestor()`. The T2-7 handoff negative (matrix:124-158) correctly proves TEXT_SELECTION_CHANGED stops when focus lands on a non-editable cell. Affinity-only firing is also real: lines 941/944 include both affinity fields in the diff condition.

**[HIGH] Atomic dedup claim (T2-5, docs/03 §3.2) is sound.** `tree_events_` is `map<id, set<EventParams>>` and `AddEvent` (ax_event_generator.cc:286-289) inserts into a set; within one Unserialize both deletion (OnSubtreeWillBeDeleted) and creation (OnNodeCreated) paths share identical `event_data()`, so the two EDITABLE_TEXT_CHANGED collapse to one. `EXPECT_EQ(count, 1)` is legitimate.

**[HIGH] FormatBold_RequiresRichlyEditable is a precise, correctly-constructed test.** kTextStyle fires TEXT_ATTRIBUTE_CHANGED only if `node->HasState(kRichlyEditable)` on the *changed node itself* (ax_event_generator.cc:687-693). The test adds kRichlyEditable to node id 7 (the changed static text, via `nodes[6]`), expects the event at node 7, and the negative omits it. Matches source exactly. docs/03 §1.2 is well-supported.

**[MEDIUM] SelectionProvider2 patch is API-accurate.** `ISelectionProvider2` confirmed absent (zero hits). `IsFocused()` exists on AXPlatformNodeDelegate (ax_platform_node_delegate.h:291); `GetMaxSelectableItems()` / `GetSelectedItems(int, vector*)` signatures match (ax_platform_node_base.h:607,434). Insertion point after `get_IsSelectionRequired` (.h:738, .cc:3278) is right. ~84 lines is plausible.

**[MEDIUM] Annotation author/datetime patch is the cleanest of the four.** Stubs confirmed verbatim (ax_platform_node_win.cc:2670-2688). `GetString16Attribute(StringAttribute)->u16string` exists on the base (ax_platform_node_base.h:171); `SysAllocString(base::as_wcstr(...))` is the established idiom (lines 2363, 2665). Non-breaking empty-string fallback reasoning is correct. ~8 lines accurate.

**[MEDIUM] B-lite spine is honest about scope.** blite_host.cc:17-20 and the run capture (blite-host-run.txt:3) both explicitly state the platform-node/UIA layer is NOT linked. It proves only surface→bridge→AXTree→AXEventGenerator, and says so. No overclaiming.

**[LOW] ENVIRONMENT.md is unusually candid** about every deviation (tarball vs gclient, gn version skew, shim asserts, the 87/87 baseline in the same binary). Provenance is auditable.

## OPEN CONCERNS

**[HIGH] docs/03 §1.1 overstates the editable-root requirement — "plus `State::kEditable`" is false against source.** `IsAtomicTextField()` (ax_node_data.cc:839-850) requires only `ui::IsTextField(role)` (role kTextField/kSearchBox/kComboBox), NOT kEditable. `GetTextFieldAncestor` (ax_node.cc:2475) keys solely on `IsTextField()`. The T2-2 negative test (unittest:166) flips role→kGenericContainer AND drops kEditable *simultaneously*, so it cannot isolate the gate — it is under-constrained and the [PROVEN] tag for "plus State::kEditable" is unearned. A canvas producer that sets role=kTextField without kEditable would still get all editing events, contradicting the contract.

**[HIGH] text-attribute-changed patch uses the wrong enqueue API.** It proposes `text_changed_nodes_.insert(wrapper)` directly. The real code never inserts a raw wrapper — `EnqueueTextChangedEvent` (browser_accessibility_manager_win.cc:1229-1234) inserts `GetUiaTextPatternProvider(node)`, resolving to a node that actually supports UIA_TextPatternId. Finalize fires `UIA_Text_TextChangedEventId` unconditionally on whatever is in the set (line 1312-1313, no pattern guard, unlike the selection set at 1302-1304). So inserting a bare leaf wrapper risks firing the event on a node with no Text pattern. The correct one-liner is `EnqueueTextChangedEvent(*wrapper);` (mirroring EDITABLE_TEXT_CHANGED at line 476). Would compile, but is wrong.

**[MEDIUM] All four patches cite the manager at the wrong path.** They reference `content/browser/accessibility/browser_accessibility_manager_win.cc`, but at tag 149 the file is `ui/accessibility/platform/browser_accessibility_manager_win.cc` (the content→ui move). Line numbers (681-683, 1300-1314) match, but the path is stale and would mislead anyone applying the diff.

**[MEDIUM] ITextProvider2 patch has two real API errors and is a sketch, not a diff.** (1) Its pattern-factory diff returns `&PatternProvider<ITextProvider>`, but the real TextProvider factory is `AXPlatformNodeTextProviderWin::CreateIUnknown` (ax_platform_node_win.cc:8700) — `PatternProvider<>` is not used for text. (2) Its gating predicate `IsText()||IsTextField()||kRootWebArea` diverges from the canonical `IsPlatformDocument()||IsTextField()||IsText()` (line 8699). (3) Both method bodies contain `/* ... */` placeholders for range construction and IRawElementProviderSimple→node resolution, so the ~64-line estimate is optimistic. UIA_TextPattern2Id-in-break-list (8733) and the get_Target reverse-relation reuse (2690-2697) are correctly cited.

**[LOW] The "104 tests" figure is inconsistent across docs.** Binary contains 87 upstream + 7 T2 + 12 matrix = 106. docs/03:82 says "104 + matrix tests"; ENVIRONMENT.md:47 says "104 passing generated-event tests." Neither reconciles to 106 or to 19 (T2+matrix). The verifiable "87/87 upstream baseline" is the only clean number; "104" appears to be a stale repeated count.

**[LOW] SelectionProvider2 patch ships an empty `namespace { }` block** (selectionprovider2-prototype.md:49-52) described as a "shared helper" that contains nothing — dead scaffolding the four methods don't use. Cosmetic.

**[LOW] T2-2 comment misattributes the suppression mechanism.** The test comment and docs/03 §1.1 credit "editable=false," but the actual gate is the role change to kGenericContainer (kEditable is inert for `IsTextField()`). Same root cause as the HIGH item; flagged separately as a documentation-clarity issue.

## BOTTOM LINE

**Biggest success:** The generated-event layer is verified honestly and correctly against source — the editable-text pair, the two selection events, the kCaretBounds correction, the richly-editable gating, and the atomic dedup all hold up to line-level scrutiny, and the project explicitly fences off what it cannot prove off-Windows. Real, load-bearing verification, not theater.

**Biggest concern:** docs/03 §1.1's "[PROVEN] ... plus `State::kEditable`" is false — `IsTextField()` is role-only — and the T2-2 test that supposedly proves it confounds role and state, so the single most foundational clause of the bridge contract (what the editable document root must carry) is both overstated and under-tested.
