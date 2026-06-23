# 10 — Editing Scenario Matrix: Loop, Word Online, Docs

Status: GRADED 2026-06-12 against Chrome 149 tag 149.0.7827.115 source; 12 rows additionally VERIFIED BY TEST at the generated-event layer the same day (results/T2-linux-generated-events.md, results/matrix-linux-generated-events.md). Grades: SUPPORTED / BROWSER-WORK / OPEN, each with file:line citations.

## Purpose

Inventory every editing interaction a modern collaborative editor exposes, then grade each: SUPPORTED (expressible through the AX tree today), BROWSER-WORK (schema or UIA translation gap in Chromium), or OPEN (needs research). This is both the engineering checklist and the standards-argument evidence.

## Headline question answered: multi-cell table selection and SelectionPattern2

Scenario: focus in a table, text selected inside a cell, then a whole cell selected, then a range or discontiguous group of cells.

Source findings:
- Item-level selection is fully generic. ISelectionItemProvider is implemented on the platform node (ui/accessibility/platform/ax_platform_node_win.cc lines 3127 to 3236): get_IsSelected reads node selection state; AddToSelection, RemoveFromSelection, Select route back as actions to the producer. Discontiguous selection is the selected attribute on arbitrary cells.
- Container-level selection is UIA Selection pattern v1. ISelectionProvider (lines 3238 to 3285): GetSelection enumerates selected descendants; CanSelectMultiple from the multiselectable state; IsSelectionRequired. Exposed on containers with selectable children (line 8652).
- Table and TableItem patterns ride along on table-like roles and cells (lines 8658 to 8689): headers and coordinates.
- ISelectionProvider2 is NOT implemented anywhere in the UIA layer. SelectionPattern2 adds FirstSelectedItem, LastSelectedItem, CurrentSelectedItem, ItemCount — what Narrator uses for rich N-of-M selection summaries and traversal.

Verdict: the scenario is functionally expressible today through SelectionItem plus Selection v1 plus Table patterns with two-way actions. SelectionPattern2 is a genuine BROWSER-WORK item, and small: GetSelectedItems already enumerates the selection, so the four properties derive from existing data. Ideal overlay patch and a strong standards exhibit: the platform API outruns what the browser surfaces.

OPEN (NARROWED 2026-06-12): when text selection inside a cell transitions to cell-level selection, what should ATs receive and in what order? The generation half is now answered by test (results/matrix-linux-generated-events.md): in one atomic update the two mechanisms compose cleanly — documentSelectionChanged + selectedChanged + selectedChildrenChanged in a single event set, no collision, and textSelectionChanged correctly stops once the focus leaves the text field. Remaining for T2-7 on the VM: UIA translation ordering and NVDA announcement behavior only.

## Scenario inventory — GRADED (2026-06-12)

Method: direct source reading of the pinned checkout, same as section 09. Every grade cites file:line evidence verifiable with grep (spot-check commands at the end). Two corrections to this document's own seeded assumptions surfaced during grading; they are flagged inline and in the corrections note.

> **Grade caveat (expert review 2026-06-16):** a SUPPORTED grade here means "expressible through the AX tree and verified at the *generated-event / provider-API* layer." It does **not** guarantee a good — or any — screen-reader experience, which depends on UIA translation + NVDA behavior not yet run (VM-gated). Known cases where SUPPORTED-at-API ≠ SUPPORTED-for-a-user: **autocorrect & IME** (downgraded below — announcement rides the composition-only TextEdit path); **multi-cell selection** (per-cell feedback works, but no coherent "N of M selected" summary from Selection v1); **comments** (anchor exposed, but not navigable/readable inline without the missing ITextProvider2). Treat every SUPPORTED as "SUPPORTED at the layer tested" until the VM/NVDA pass. Full analysis: results/expert-reviews/AGGREGATE.md.

### Text core

- **Character/word/line/paragraph insert + delete — SUPPORTED.** UIA editing events are computed from raw tree deltas: any change contributing to a text field's value fires EDITABLE_TEXT_CHANGED / VALUE_IN_TEXT_FIELD_CHANGED on the field ancestor, ui/accessibility/ax_event_generator.cc:1124 (FireValueInTextFieldChangedEventIfNecessary), finalized as UIA_Text_TextChangedEventId in ui/accessibility/platform/browser_accessibility_manager_win.cc:1313. All granularities exist in the schema: deletion InputEventTypes kDeleteWordBackward…kDeleteContentForward at ui/accessibility/ax_enums.mojom:1148-1155, TextBoundary kCharacter/kLineStart/kParagraphStart/kWordStart at :1192-1221.

- **Undo / redo — SUPPORTED.** kHistoryUndo/kHistoryRedo at ax_enums.mojom:1159-1160 under Command::kHistory (:1114); the reverted content fires the delta-driven text-changed path regardless. Caveat (per 09 Finding 1): intents are not consumed by the Windows UIA translation, so "undo" qua undo is not distinguished — a UIA vocabulary limit, not a Chromium gap.

- **Autocorrect / autoformat replacement — BROWSER-WORK / verify (downgraded from SUPPORTED, expert review 2026-06-16).** The atomic delta + coherent finalize pass (kInsertReplacementText at ax_enums.mojom:1144; AXTreeUpdate atomic at ui/accessibility/ax_tree_update.h:23; T2-5) is real, but it is **not sufficient for an NVDA user to hear "the replaced teh."** NVDA does not speak the *content* of `UIA_Text_TextChangedEventId`; a genuine "X replaced Y" announcement rides `UIA_TextEdit_TextChangedEventId` with `TextEditChangeType_AutoCorrect`, which in this codebase fires **only** from the composition hook (`FireUiaTextEditTextChangedEvent`/`OnActiveComposition`, ax_platform_node_win.cc:835), not the generic delta path. App-driven autocorrect not routed through composition → NVDA likely says nothing or echoes only the trigger char. SUPPORTED *at the generation layer*; the AT-consumption half is **VM/NVDA-pending** (review backlog F4, NEXT-QUESTIONS).

- **IME composition — BROWSER-WORK / verify (downgraded from SUPPORTED, expert review 2026-06-16).** `OnActiveComposition` + `GetActiveComposition`/`GetConversionTarget` exist (ax_platform_node_win.cc:823/:851; ax_platform_node_textprovider_win.cc:284/:293), but the `UIA_TextEdit_TextChangedEventId` path **early-returns on commit** (`is_composition_committed`) and is **gated on `HasEventListenerForEvent(UIA_TextEdit_TextChangedEventId)`** — i.e., it does nothing unless NVDA actually subscribed for the custom provider window. Whether NVDA hears ongoing composition and committed CJK characters is exactly what T2-4 must verify; until then this is mechanism-present / announcement-unproven, not SUPPORTED.

- **Rich formatting state (bold/italic/underline/strikethrough/sub-super/justify/indent) — SUPPORTED.** kFormat* intents at ax_enums.mojom:1162-1173; UIA text attributes served by GetTextAttributeValue (ax_platform_node_win.cc:6069): FontWeight :6106, IsItalic :6117, IsSubscript/IsSuperscript :6132/:6136, StrikethroughStyle :6144, HorizontalTextAlignment :6154, UnderlineStyle :6161.

- **Formatting change NOTIFICATION on UIA — BROWSER-WORK.** TEXT_ATTRIBUTE_CHANGED is generated (ax_event_generator.cc:529) but the Windows manager fires only the IA2 event, no UIA counterpart (browser_accessibility_manager_win.cc:681-683). Patch point: that case (UIA text-changed with TextEditChangeType or a property-changed); the kFormat* intents are likewise unconsumed on the UIA path.

- **Find and replace — SUPPORTED.** App-driven replacement is an atomic delta + selection move (same machinery as autocorrect); AT-driven search via ITextRangeProvider::FindText (ax_platform_node_textrangeprovider_win.cc:480) and FindAttribute (:347).

- **Spellcheck markers + suggestions — SUPPORTED (forward direction).** Marker schema kMarkerTypes/kMarkerStarts/kMarkerEnds at ax_enums.mojom:988-990, MarkerType kSpelling/kGrammar/kSuggestion at :1068-1074, Command::kMarker at :1116. UIA: GetAnnotationTypesAttribute maps to AnnotationType_SpellingError/GrammarError (ax_platform_node_win.cc:6204, :6246-6249); SPELLING_MARKER_CHANGED raises the UIA Changes event (browser_accessibility_manager_win.cc:690-692).

- **Annotation-to-range navigation (RangeFromAnnotation) — BROWSER-WORK.** CORRECTION to this doc's seeded text: ITextProvider2 is NOT implemented. The text provider COM map exposes only ITextProvider and ITextEditProvider (ax_platform_node_textprovider_win.h:23-24); UIA_TextPattern2Id is in the "Not currently implemented" list (ax_platform_node_win.cc:8733); RangeFromAnnotation has no hits anywhere in ui/accessibility. Patch point: add ITextProvider2 to AXPlatformNodeTextProviderWin; the forward data (kDetailsIds reverse relations) already exists to derive it.

### Tables

- **Cell text editing — SUPPORTED.** Text/TextEdit patterns exposed on documents, text fields, and text (ax_platform_node_win.cc:8697-8701); in-cell editing fires the same delta-driven events via the cell's text-field ancestor (ax_event_generator.cc:1124).

- **Cell / row / column / rectangular / discontiguous selection — SUPPORTED (verified).** ISelectionItemProvider at ax_platform_node_win.cc:3127 (AddToSelection :3196, RemoveFromSelection :3202, Select :3208, get_IsSelected :3214); ISelectionProvider at :3238-3285 (GetSelection :3241, get_CanSelectMultiple :3270, get_IsSelectionRequired :3278); pattern exposure :8646/:8652; Grid/Table :8611/:8658/:8674.

- **SelectionPattern2 — BROWSER-WORK (re-verified).** ISelectionProvider2: zero hits in the UIA layer. GetSelection (:3241) already enumerates selected descendants, so FirstSelectedItem/LastSelectedItem/CurrentSelectedItem/ItemCount are derivable — the headline verdict stands.

- **Add / delete rows and columns — SUPPORTED.** Insertions: SUBTREE_CREATED → StructureChangeType_ChildAdded (browser_accessibility_manager_win.cc:677-680); removals: StructureChangeType_ChildRemoved (:498, :1031). Note ROW_COUNT_CHANGED is explicitly in the "Currently unused events on this platform" block (:729) — clients re-query IGridProvider::get_RowCount; UIA-idiomatic, but a count property-changed would be a small enhancement.

- **Reorder rows / columns — SUPPORTED.** CHILDREN_CHANGED fires EVENT_OBJECT_REORDER and StructureChangeType_ChildrenReordered (browser_accessibility_manager_win.cc:408-418).

- **Column hide — OPEN.** Structural removal is expressible (ChildRemoved; UIA_IsHiddenAttributeId maps IsInvisibleOrIgnored at ax_platform_node_win.cc:6113), but no schema distinguishes "hidden" from "deleted", and no column-count event fires (:729). Open: should a hidden column remain in the tree as ignored (recoverable, announceable) or vanish, and what should the AT hear?

- **Sort — SUPPORTED.** IntAttribute::kSortDirection → UIA_ItemStatusPropertyId (ax_platform_node_win.cc:5549-5575) and AriaProperties "sort=" (:7523-7543); SORT_CHANGED generated on change (ax_event_generator.cc:670-674), finalized as AriaProperties property-changed (browser_accessibility_manager_win.cc:673-675, :1295-1297).

- **Filter — OPEN.** Filtered-out rows are plain structure removals; no AX attribute or UIA property expresses "view is filtered / N of M shown" (no kFilter* in ax_enums.mojom; UIA has no filter concept short of ItemStatus free-text). Open: is UIA_ItemStatusPropertyId the right carrier, or is this app-level live-region territory?

- **Header announcement during navigation — SUPPORTED.** TableItem pattern on cells/headers (ax_platform_node_win.cc:8674); GetColumnHeaderItems :3290, GetRowHeaderItems :3305; container-level GetColumnHeaders/GetRowHeaders :3324/:3335.

### Comments and annotations

- **Comment anchored to a text range — SUPPORTED forward; reverse lookup is the ITextProvider2 BROWSER-WORK item.** Role::kComment (ax_enums.mojom:157) → AnnotationType_Comment (ax_platform_node_win.cc:2723-2724); IAnnotationProvider exposed when the node is the target of a kDetailsIds relation (:8600-8603; IsStructuredAnnotation at ax_platform_node_base.cc:789); annotated runs surface UIA_AnnotationObjectsAttributeId/AnnotationTypesAttributeId (:6077-6081).

- **Comment anchored to a table cell / board field — SUPPORTED.** kDetailsIds is valid on any node: element-level UIA_AnnotationObjectsPropertyId built from it (ax_platform_node_win.cc:5396-5401); IAnnotationProvider::get_Target resolves the reverse relation (:2691). The text-attribute form requires a text node (GetAnnotationObjectsAttribute early-outs on !IsText(), :6184-6190), but for a cell the element property is the correct surface.

- **Threaded replies; edit/delete own comment; keyboard into comment pane — SUPPORTED.** Threads are ordinary tree structure (comments are UIA_GroupControlTypeId, ax_platform_node_win.cc:923-926); thread collapse rides IExpandCollapseProvider (:2744, exposure :8605-8609); edits inside comments are standard text machinery.

- **Mentions — SUPPORTED, no first-class vocabulary.** No mention-specific role exists in ax_enums.mojom (verified). Expressible today as kLink (ax_enums.mojom:256 → UIA_HyperlinkControlTypeId, ax_platform_node_win.cc:1218-1220) plus StringAttribute::kRoleDescription (:658) for the flavor; UIA has no mention concept either, so this is not a translation gap.

### Collaboration presence

- **Remote cursors and remote selections — OPEN.** AXTreeData carries exactly one (self) selection (ui/accessibility/ax_tree_data.h:67-72); GetSelection / DOCUMENT_SELECTION_CHANGED are all self-selection. No schema exists for non-self carets. Closest existing machinery: highlight markers → AnnotationType_Highlighted with HIGHLIGHT_MARKER_CHANGED → UIA Changes event (browser_accessibility_manager_win.cc:696-698). Open: model remote selections as highlight markers + annotations (works today, loses "this is a person's caret" semantics) or new schema — noting UIA itself has no non-self-caret concept, making this standards-track.

- **Attribution of who edits what region — BROWSER-WORK.** UIA Annotation defines Author, but Chromium's get_Author is an explicit empty-string stub (ax_platform_node_win.cc:2670-2678; get_DateTime likewise :2680-2688). Patch point: new string attribute on annotation nodes mapped in get_Author/get_DateTime — pattern plumbing already exists.

- **Live remote changes during local edit — OPEN.** Atomicity is solid (ax_tree_update.h:23; one deduplicated finalize pass, browser_accessibility_manager_win.cc:1291-1314), but updates and events carry no origin/source field — a remote edit fires the same EDITABLE_TEXT_CHANGED as local typing. Open: how should remote-origin deltas be marked (or announcements moderated) so they are not conveyed as local-typing echoes mid-edit?

### Structured and embedded content

- **Checkable task items in text flow — SUPPORTED.** Role::kCheckBox → UIA_CheckBoxControlTypeId (ax_platform_node_win.cc:971-973); Toggle pattern via IsToggleSupported (:8703-8707, :8983); state changes fire UIA_ToggleToggleStatePropertyId (browser_accessibility_manager_win.cc:397-406). In-text embedding rides the U+FFFC/TextChild machinery below.

- **Progress trackers / voting — SUPPORTED.** Role::kProgressIndicator → UIA_ProgressBarControlTypeId (ax_platform_node_win.cc:1367-1369); RangeValue pattern gated on IsRangeValueSupported (:8631-8635; role list ui/accessibility/ax_role_properties.cc:597-603); UIA_RangeValueValuePropertyId on change (browser_accessibility_manager_win.cc:586-591).

- **Code blocks — SUPPORTED.** Role::kCode → UIA_TextControlTypeId, aria role "code" (ax_platform_node_win.cc:968-969); font distinction via UIA_FontNameAttributeId (:6094). Minor: ComputeUIAStyleId (:6322) has no kCode case (code spans get StyleId_Normal) — one-case enhancement, not a blocker.

- **Links — SUPPORTED.** Role::kLink → UIA_HyperlinkControlTypeId (ax_platform_node_win.cc:1218-1220); Invoke pattern (:8625-8629, :9015); URL via Value pattern (IsValuePatternSupported includes IsLink, ax_platform_node_delegate_utils_win.cc:36; value = kUrl, ax_platform_node_win.cc:8290-8296).

- **Dates — SUPPORTED.** Role::kDate/kDateTime (ax_enums.mojom:161-162) → UIA_EditControlTypeId (ax_platform_node_win.cc:1017-1020); date-picker popups get ExpandCollapse (:8605-8609).

- **Embedded interactive components in text flow — SUPPORTED.** Non-text children appear in hypertext as U+FFFC (ui/accessibility/ax_node.h:53-54, kEmbeddedObjectCharacterUTF16); TextChild pattern bridges element/text views: UIA_TextChildPatternId exposed whenever a text-container ancestor exists (ax_platform_node_win.cc:8691-8695), get_TextContainer/get_TextRange at ax_platform_node_textchildprovider_win.cc:60/:74.

- **Kanban drag-reorder — BROWSER-WORK.** UIA Drag/DropTarget patterns are both in the "Not currently implemented" list (ax_platform_node_win.cc:8724-8725) and the web schema deprecated its side (kGrabbedDeprecated ax_enums.mojom:932, kDropeffectDeprecated :804). Fully-supported fallback: posinset/setsize property-changed (browser_accessibility_manager_win.cc:582-585, :669-672) plus live-region announcements (:523-537). Patch point if pursued: IDragProvider/IDropTargetProvider in GetPatternProviderFactoryMethod.

### Corrections to seeded assumptions

1. "RangeFromAnnotation in ITextProvider2" (this doc's comments row): ITextProvider2 is not implemented in Chromium's UIA layer at this tag; only forward annotation exposure exists. Reverse comment→range navigation is a BROWSER-WORK item alongside SelectionPattern2.
2. IAnnotationProvider::get_Author / get_DateTime are empty-string stubs — bounds what comment attribution can convey today and feeds the collaboration-presence OPEN items.

### Citation spot-checks

```sh
grep -rn "ISelectionProvider2\|RangeFromAnnotation\|UIA_TextPattern2Id" ui/accessibility/
grep -n "UIA_FontWeightAttributeId\|UIA_StrikethroughStyleAttributeId\|UIA_UnderlineStyleAttributeId" ui/accessibility/platform/ax_platform_node_win.cc
grep -n "kFormatBold\|kHistoryUndo\|kInsertReplacementText" ui/accessibility/ax_enums.mojom
grep -n "FireUiaChangesEvent\|ROW_COUNT_CHANGED\|TEXT_ATTRIBUTE_CHANGED" ui/accessibility/platform/browser_accessibility_manager_win.cc
grep -n "IsStructuredAnnotation\|get_Author\|AnnotationType_Comment" ui/accessibility/platform/ax_platform_node_win.cc
```

## Next actions

1. DONE 2026-06-12: full inventory graded with citations (above).
2. Prototype the SelectionPattern2 overlay patch; measure size. The same overlay shape applies to the other BROWSER-WORK items found: UIA TEXT_ATTRIBUTE_CHANGED translation, ITextProvider2/RangeFromAnnotation, annotation Author/DateTime, Drag/DropTarget.
3. T2-7 covers the cell-versus-text selection handoff empirically (Windows VM).
4. Dump live UIA trees of Loop and Word Online tables during the multi-cell scenario for the current-ceiling baseline (Tier 3).
5. Open questions from this grading are consolidated in results/NEXT-QUESTIONS.md.
