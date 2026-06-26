# UIA Text/TextRange provider — what a screen reader needs, and what we must feed Chromium

Date: 2026-06-25. Deep dive on the Microsoft UIA Text/TextRange docs + NVDA requirements, mapped to the
B-lite host. Goal: make our editable field a "real surface" NVDA will read with insertion-point (IP) tracking.

## The UIA Text contract (from Microsoft Learn)
A control that lets a user enter text / place a caret MUST support the **Text control pattern**:
- **`ITextProvider`** required members: `DocumentRange`, `SupportedTextSelection`, **`GetSelection`**,
  `GetVisibleRanges`, `RangeFromChild`, `RangeFromPoint`, + events `UIA_Text_TextChangedEventId` and
  **`UIA_Text_TextSelectionChangedEventId`**.
- **`ITextRangeProvider`** required members (~17): `Clone, Compare, CompareEndpoints,
  ExpandToEnclosingUnit, Find*, GetAttributeValue, GetBoundingRectangles, GetChildren,
  GetEnclosingElement, GetText, Move, MoveEndpointByUnit/Range, Select, ScrollIntoView, Add/RemoveFromSelection`.
- **`ITextProvider2`**: `GetCaretRange`, `RangeFromAnnotation`.
- **Insertion point = a degenerate (empty) text range** at the caret. `GetSelection` must return that
  degenerate range when the caret exists and nothing is selected. **`TextSelectionChanged` must fire on
  every caret move** — clients (NVDA) depend on it to track the IP.
- Should be **paired with `IValueProvider`** (programmatic value get/set); both `TextChanged` and the
  Value `AutomationPropertyChanged` events should fire.
- Control type should be **Edit** or **Document**.
Sources: learn.microsoft.com /windows/win32/winauto/uiauto-implementingtextandtextrange and
/uiauto-about-text-and-textrange-patterns; ITextProvider/ITextRangeProvider API pages.

## The good news: Chromium's `AXPlatformNodeWin` already implements ALL of it
`AXPlatformNodeWin` exposes `UIA_TextPatternId` (full `ITextProvider`/`ITextRangeProvider`/`ITextProvider2`)
for any node where **`IsPlatformDocument() || IsTextField() || IsText()`** (ax_platform_node_win.cc:8698).
Our field is `kTextField` ⇒ it qualifies. **We do NOT implement the Text pattern.** Our job is to feed
`AXPlatformNodeWin` a *complete* AX node so its Text/Value/selection logic produces the right answers.

## What a screen reader (NVDA) needs to engage and track the IP — and our gaps
1. **Focusable + actually focused.** The field must be `kFocusable`, be the tree's focused node, and report
   `HasKeyboardFocus=true`. We set `kFocusable` + `tree_data.focus_id=kField` + fire `kFocus`. VERIFY the
   field reports `HasKeyboardFocus` to UIA (next test).
2. **A value.** Pair Text with Value: the field should carry its text as a **value** (`StringAttribute::kValue`),
   not only as a child static-text name. **GAP: our field has no `kValue`** — add it (and update it on edit).
3. **Text content** for `DocumentRange`/`GetText`: the field's subtree text (our `kStaticText` child) supplies it. OK.
4. **Selection / caret.** The tree's text selection (`sel_anchor/focus` at the caret offset) is what
   `GetSelection` turns into the degenerate IP range. We set it on the `kText` child. VERIFY it yields a
   degenerate caret range on the field's Text pattern.
5. **Events.** Fire `TextSelectionChanged` on caret move (we do) AND ideally the Value `AutomationPropertyChanged`.
6. **Control type Edit.** `kTextField` → UIA Edit. OK.

## Screen-reader operational notes (from the project owner)
- **Items must take keyboard focus and HAVE focus** — a real, reported focus, not just a focus event.
- **Start the app BEFORE the screen reader** so that when the app/field takes focus and emits focus info,
  the screen reader (starting/just-started) reads the focused control. (We had been starting NVDA first;
  try app-first for the live NVDA run.)
- The surface must "look/act/feel like a real window that took focus" or NVDA filters it.

## Working hypothesis
NVDA (and arguably the UIA-client walk) isn't engaging because the field isn't yet a *complete* editable
surface — most concretely it lacks a **value** and we haven't verified it reports **HasKeyboardFocus** or a
**degenerate caret range**. Close those, then re-test with `Inspect.exe`/NVDA on an unlocked desktop.

## ROOT CAUSE of "no children under the window" (found 2026-06-26)
The owner's lead — how Chromium links an independent UIA subtree (PDF/iframe) under a host — pointed at the
**HWND association**, not the Text pattern. UIA connects content to a window's fragment via each node's
**owning HWND**, which `AXPlatformNodeWin` reads from `GetDelegate()->GetTargetForNativeAccessibilityEvent()`
(used for hit-testing, window handle, and `AXFragmentRootWin::GetFragmentRootParentOf`, ax_platform_node_win.cc
:1630,2146,5173). Our **`AXFragmentRootWin` returns the real HWND** (constructed with it), but our **content-node
delegates do NOT**:
- `TestAXNodeWrapper::GetTargetForNativeAccessibilityEvent()` returns `native_event_target_`, which is
  `kNullAcceleratedWidget` / `kMockAcceleratedWidget = (HWND)-1` — **never our real HWND, and there is NO setter**.
- the earlier hand-rolled `BliteNodeDelegate` never overrode the method at all (base default = null).
So fragment root HWND ≠ content HWND ⇒ UIA can't tie our subtree to the window ⇒ the Window has no children
(exactly the native-probe result). **This is the connection bug behind every "1 element" probe.**

**Fix:** use a delegate whose `GetTargetForNativeAccessibilityEvent()` returns our real HWND. `TestAXNodeWrapper`
can't (no setter), so switch back to a hand-rolled `BliteNodeDelegate` and add that one override returning
`host_->hwnd()`. (Validates the owner's "it needs more than a window / PDF-iframe linkage" insight.)

## Anticipated NEXT hurdle (per project owner): bidirectional message flow
Once the subtree is hooked up, the next class of problems is making **messages flow both ways cleanly**:
- **provider → client (events):** UIA events must actually reach the client — `UIA_Text_TextChanged`,
  `UIA_Text_TextSelectionChanged` (caret/IP), focus, and Value `AutomationPropertyChanged`. The platform node
  only raises an event when a client has a **registered listener** (`HasEventListenerForEvent`/
  `AddEventListener`), so the host must keep pumping its STA and fire AFTER the client attaches (already
  designed: NVDA-first + the 8s settle + kFocus-then-edit).
- **client → provider (requests/actions):** the client walks/queries our tree and may issue actions
  (`ITextRangeProvider::Select` to move the IP, `IValueProvider::SetValue`, focus). These come in as COM
  calls marshaled onto our STA thread and are dispatched during the message pump — so the pump must stay
  responsive and the provider methods must succeed.
Plan: verify the event direction first (NVDA hears the insert+caret), then the action direction
(`ITextRangeProvider::Select` round-trip / cursor-routing), per V3/V4 in the backlog.

### RESOLVED 2026-06-26 — event direction works to a subscribed native client
`scripts/uiaprobe_events.cpp` (native `IUIAutomation`, implements the three event-handler interfaces,
subscribes via `AddFocusChangedEventHandler` / `AddAutomationEventHandler` / `AddPropertyChangedEventHandler`
BEFORE the host fires) caught **all four**: `FocusChanged → our Edit`, `Text_TextChanged`,
`Text_TextSelectionChanged` (the caret/IP), and `Value changed → 'hello!'`. Summary line:
`focus=2 textChanged=1 selChanged=1 valueChanged=1`. The host fires `kTextChanged` + `kTextSelectionChanged`
+ **`kValueChanged`** (this one maps to `UIA_ValueValuePropertyId` via `MojoEventToUIAProperty`;
`kValueInTextFieldChanged` does NOT map — important). So provider→client events + client→provider *reads*
(the probe reads value/selection/text) both work.

### RESOLVED 2026-06-26 — write (action) direction works; bidirectional flow COMPLETE
`IValueProvider::SetValue` returned `E_FAIL` until `BliteNodeDelegate::AccessibilityPerformAction` was
implemented (base returns false; `AXPlatformNodeWin::SetValue` builds `AXActionData{kSetValue, value}` and
returns E_FAIL when the delegate rejects it — ax_platform_node_win.cc:3396). Now `AccessibilityPerformAction`
handles `kSetValue` via `host_->ApplyClientSetValue()` (editor `SetText` → `BuildEditDelta` → `Unserialize` →
`FirePlatformEditEvents`) and `kFocus` (accept). Verified by `scripts/uiaprobe_action.cpp`:
`SetValue('client-typed') hr=0x0`, value reads back `'client-typed'` → **round-trip APPLIED**. All three flows
(events out, reads in, writes in) now work with a native UIA client.

**`kSetSelection` (cursor-routing) — handler ready, client path blocked at the range layer.** The producer
handler is implemented + wired (`AccessibilityPerformAction` → `host_->ApplyClientSetSelection` updates the
tree-data selection and fires `kTextSelectionChanged`). But a client `ITextRangeProvider::Select()` on the
DocumentRange returns **`0x80040201` = `UIA_E_ELEMENTNOTAVAILABLE`** (NOT InvalidOperation — corrected)
*before* `AccessibilityPerformAction` is called (the host never logs `[host] client SetSelection`).
**Precise gate:** `UIA_VALIDATE_TEXTRANGEPROVIDER_CALL()` (ax_platform_node_textrangeprovider_win.cc:22)
returns ELEMENTNOTAVAILABLE when `!start()->GetAnchor() || !end()->GetAnchor()` (or owner destroyed). The
range comes from `get_DocumentRange` → `GetRangeFromChild(owner=field, field)` — so the **DocumentRange
positions built over our FIELD have null anchors**, while `GetSelection`'s range (anchored on the `kText`
child at the caret) is fine and `GetText` on it works. So the gap is the field's text-content position
anchoring for a whole-document range in our standalone tree (hypertext / inline-text / child-offset setup
that `AXPosition` needs), i.e. real `AXPosition` machinery — scoped as backlog **V3**, not rabbit-holed.
Write direction stays PROVEN via `SetValue`. NVDA-on-desktop run still pending.

## RESOLVED 2026-06-26 — the field is a complete editable surface to native UIA
All gaps in items 1/2/4 above are closed; verified by the native `IUIAutomation` probe (no interaction):
- **Focus (item 1):** `BliteNodeDelegate::GetFocus()` → tree `focus_id` node ⇒ Edit reports `HasKeyboardFocus`
  AND is UIA's `GetFocusedElement`.
- **Value (item 2):** field carries `kValue` ⇒ probe shows `value='hello!'` (and the live edit propagated).
- **Caret (item 4):** the missing piece was node resolution + position context, NOT the Text pattern itself.
  Base `AXPlatformNodeDelegate::GetFromNodeID()` is `return nullptr;`, so the Text provider could not resolve
  the selection's anchor/focus objects. Fixes: override `GetFromNodeID()` (→ `host_->PlatformNodeFor`),
  `GetUnignoredSelection()` (→ `tree()->GetUnignoredSelection()`), + a leaked **non-platform** `AXTreeManager`
  for `CreatePositionAt`'s tree context. ⇒ `GetSelection` returns a degenerate caret range (`selRanges=1`,
  empty text = the insertion point). NOTE: `is_platform_tree_manager=true` collapsed child navigation; use false.

Net: the producer→UIA editable-surface thesis is PROVEN at the native-UIA-client level. Next is the
bidirectional message-flow section above, then NVDA on an unlocked desktop.

## RESOLVED 2026-06-26 (second pass) — text ranges, cursor routing, rich-text attributes all work
The earlier "Select blocked at the range layer" was misdiagnosed as `AXPosition` depth; the real cause was the
**range owner**. `AXPlatformNodeTextRangeProviderWin::GetOwner()` returns null unless the position's
`GetManager()->is_platform_tree_manager()` is true AND it can `GetPlatformNodeFromTree(anchor)` — and a null
owner makes EVERY range method fail `UIA_E_ELEMENTNOTAVAILABLE` (0x80040201; note: ELEMENTNOTAVAILABLE, not
INVALIDOPERATION). A plain `AXTreeManager(…, /*is_platform=*/true)` is NOT an `AXPlatformTreeManager` (bad
cast), and `false` disables the owner path — hence the dilemma.

Fix set (all in `blite/blite_host_win.cc`):
1. **`BliteTreeManager : public AXPlatformTreeManager`** — overrides `GetPlatformNodeFromTree`/`RootDelegate`
   to return the host's existing platform nodes (so it does not spawn a conflicting second set, which had
   collapsed navigation when we first tried platform mode). Created before the host, `SetHost` after.
2. **`BliteNodeDelegate::GetFromTreeIDAndNodeID`** — `Select` resolves the selection's delegate through it
   (base returned null → `DCHECK(delegate)` crash).
3. **Inline-text-box leaf** (`kInline`, character offsets) under the StaticText — gives `AXPosition` a real text
   leaf so `field->CreateTextPositionAt(0)->AsLeafTextPosition()` anchors (whole-document range was null-anchored).

Verified (native clients): `DocumentRange.GetText()`='hello!'; `Select()` hr=0x0 → `kSetSelection` applies and
the selection round-trips; `GetAttributeValue` → FontWeight=700 / IsItalic=TRUE / UnderlineStyle=1; bold→normal
edit → FontWeight 700→400. Rich-text attrs must sit on the **field** (atomic text field ⇒ its inner text's
`GetLowestPlatformAncestor` is the field) — putting them only on the StaticText/inline box did nothing.
Limitation: mixed-format runs need a non-atomic per-run structure; single uniform run is fully working.

## Concrete next actions
1. Add `StringAttribute::kValue` to the field (init + edit delta).
2. Verify (via a UIA-client query of the field, or Inspect) `HasKeyboardFocus`, `IsKeyboardFocusable`,
   ControlType=Edit, TextPattern present, and `GetSelection` returning a degenerate range at the caret.
3. Live NVDA run **app-first**, on an unlocked/console desktop.
