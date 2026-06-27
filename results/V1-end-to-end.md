# V1 — end-to-end insert + caret → UIA → NVDA

## ✅ V1 PROVEN (2026-06-27): NVDA speaks our custom non-Blink field
With NVDA 2026.x (No-speech synth, log-level 12, `results/nvda-v1-PROVEN.log`), against the B-lite host:
NVDA announced our field as **`Speaking [..., 'edit', ..., 'hello']`** — role "edit", content "hello", no name
(so it is OUR field, not Chrome's named "Ask me anything"). The whole thesis lands end-to-end: custom
canvas-style editor model → `Bridge` → `AXTree` → `AXPlatformNodeWin` → UIA → **NVDA actually says "edit, hello."**
No `gainFocus` error, no crash.

Two fixes got it there (after the field was already a complete editable surface to the UIA client):
1. **Flat UIA leaf:** stop exposing the StaticText/InlineTextBox as navigable UIA elements
   (`HidesChildrenFromUIA`: text-field/static-text report no UIA children; positions still walk the AXNode
   tree). The exposed inline box made NVDA's web text navigation treat it as embedded "replaced content".
2. **`base::i18n::InitializeICU()` in `main()`:** when NVDA navigates by word/character it calls
   `ITextRangeProvider::ExpandToEnclosingUnit` → `AXPosition::GetGraphemeIterator` → ICU `ubrk_open`, which
   FATAL-crashed the host without ICU data. That crash was the actual cause of NVDA's `gainFocus` error.

Remaining NVDA refinements (V2/V4): announcing the live edit (`hello`→`hello!` TextChanged) and caret-move
read-back as they happen; word/line navigation read-back; braille. The core "screen reader announces our
editable field + its text" is PROVEN.

# (historical) V1 — end-to-end insert + caret → UIA → NVDA (first run)

Date: 2026-06-25. Host: local Win11 (SAC off), **RDP session 2**. Target:
`out\host\blite_host_win.exe` (full cone, tag 149.0.7827.115).

## BREAKTHROUGH 2026-06-26: native UIA now surfaces our editor tree. Root cause was HWND association.
The `WM_GETOBJECT`/provider path was always correct; the missing link was that our **content-node delegates
must return our real HWND** from `GetTargetForNativeAccessibilityEvent()`. `AXFragmentRootWin` returns the
real HWND; our content (hand-rolled delegate, and `TestAXNodeWrapper` which hard-codes a mock `(HWND)-1`)
returned null/mock → UIA couldn't tie the subtree to the window. Fix: hand-rolled `BliteNodeDelegate` now
overrides `GetTargetForNativeAccessibilityEvent()` to return `host_->hwnd()`.

### COMPLETE editable surface to native UIA (2026-06-26). Three more delegate fixes after the HWND one:
- **Focus:** `BliteNodeDelegate::GetFocus()` → the tree's `focus_id` node, so `AXPlatformNodeWin` reports
  `HasKeyboardFocus` on the field and UIA's `GetFocusedElement` returns the Edit (verified CLEAN — no user
  interaction; it is our override, not a stray click).
- **Caret / insertion point:** `GetSelection` was empty then erroring because the base
  `AXPlatformNodeDelegate::GetFromNodeID()` is literally `return nullptr;` (the Text provider resolves the
  selection's anchor/focus objects through it) and the position machinery had no tree context. Fixes:
  `BliteNodeDelegate::GetFromNodeID()` → `host_->PlatformNodeFor(id)`, `GetUnignoredSelection()` →
  `tree()->GetUnignoredSelection()`, **plus a leaked non-platform `AXTreeManager`** (gives `CreatePositionAt`
  its tree context). `is_platform_tree_manager=false` is essential: the platform variant hijacked node
  management and collapsed child navigation to one node.

**Final native `IUIAutomation` (CUIAutomation8 — NVDA's API) probe, after the edit, no interaction:**
```
Window 'AccessibleWebEdit B-lite host'
  └─ Group(50030) RootWebArea                 focus=0 focusable=0 textPat=1 selRanges=1
     └─ Edit(50004)  value='hello!'           focus=1 focusable=1 textPat=1 selRanges=1   <- our field
        └─ Text(50020) name='hello!'          focus=0 focusable=0 textPat=1 selRanges=1
FOCUSED element = Edit(50004)
```
So a custom non-Blink tree presents to the faithful native UIA client as a **complete editable text field**:
Edit control type · a **value** · **keyboard-focusable AND focused** · **Text pattern** · a **degenerate caret
range** (empty selText = a true insertion point, not a selection) · the **live edit propagated** (`hello`→`hello!`).
Every owner-listed requirement (focus, "real surface", selection/IP) is met at the UIA-client level. PROVEN.

### Bidirectional message flow — COMPLETE at the native-UIA-client level (2026-06-26)
Verified with native clients (`scripts/uiaprobe_events.cpp`, `scripts/uiaprobe_action.cpp`):
- **provider→client events:** a *subscribed* client receives `FocusChanged→Edit`, `Text_TextChanged`,
  `Text_TextSelectionChanged` (caret), `Value changed→'hello!'` (`focus=2 textChanged=1 selChanged=1
  valueChanged=1`). Host fires `kTextChanged`+`kTextSelectionChanged`+`kValueChanged`.
- **client→provider reads:** the client walks the tree and reads value/selection/text.
- **client→provider writes:** `IValueProvider::SetValue('client-typed')` → `hr=0x0`, value reads back
  `'client-typed'` (APPLIED) via `BliteNodeDelegate::AccessibilityPerformAction(kSetValue)` →
  `host_->ApplyClientSetValue()`.

### Cursor routing + text ranges + rich-text attributes — NOW WORKING (2026-06-26, second pass)
The earlier `Select`/`UIA_E_ELEMENTNOTAVAILABLE` block was a **range-owner** problem, not the handler.
`AXPlatformNodeTextRangeProviderWin::GetOwner()` returns null unless the position's manager
`is_platform_tree_manager()` AND resolves nodes via `GetPlatformNodeFromTree` — a null owner makes EVERY range
method (`GetText`/`Select`/`GetAttributeValue`) fail. Fixes:
- **`BliteTreeManager : public AXPlatformTreeManager`** routes `GetPlatformNodeFromTree` to the host's own
  platform nodes (so it doesn't create a conflicting second set that collapsed navigation).
- **`BliteNodeDelegate::GetFromTreeIDAndNodeID`** (Select resolves its delegate through it; base returned null → DCHECK).
- An **inline-text-box leaf** (`kInline`, with character offsets) under the StaticText so AXPosition can anchor a
  whole-document range (`field->CreateTextPositionAt(0)->AsLeafTextPosition()` was otherwise null-anchored).

Verified by native clients:
- **Range reads:** `DocumentRange.GetText()` → `'hello!'` (was ELEMENTNOTAVAILABLE).
- **Cursor routing:** `DocumentRange.Select()` → `hr=0x0`; host logs `client SetSelection applied anchor(4,0) focus(4,6)`;
  selection round-trips to the full text (`scripts/uiaprobe_select.cpp`). Via `AccessibilityPerformAction(kSetSelection)`.
- **Rich-text attributes:** `GetAttributeValue` → `FontWeight=700`, `IsItalic=TRUE`, `UnderlineStyle=1(Single)`
  (`scripts/uiaprobe_attrs.cpp`). Atomic text field ⇒ attrs read from the FIELD (its `GetLowestPlatformAncestor`).
- **Attribute-change detection:** the edit flips bold off → `FontWeight 700→400` post-edit, signalled by `TextChanged`.

### NVDA actually ran on our surface (2026-06-26) — FIRST real screen-reader contact
Autonomous No-speech capture (`scripts/_nvda-capture.ps1` logic; NVDA 2026.x, log-level 12,
`results/nvda-v1-live.log`). NVDA attached to our custom non-Blink UIA provider and:
- **Did NOT hang** the app (scripted mode; the viewer-mode lock-up was the `AttachThreadInput`
  foreground-steal, now removed).
- **Recognized our field as a genuine editable text control:** NVDA instantiated it as
  `NVDAObjects.Dynamic_ChromiumUIAEditableTextWithAutoSelectDetectionUIA` — i.e. NVDA's UIA handler
  classifies our surface exactly like a Chromium web editable field. Announced our window + `document` root.
- **BUT errored reading the text on focus:** `error executing event: gainFocus on <...EditableText...>`,
  failing in NVDA's `NVDAObjects\UIA\web.pyc` `_moveToEdgeOfReplacedContent` /
  `_get_UIAElementAtStartWithReplacedContent`. So NVDA did not cleanly speak "edit, hello".
- Test artifact: NVDA also read the host's **console stdout** (our debug `cout`) as terminal text —
  the host should redirect stdout / drop the debug prints for a clean capture.

**Diagnosis / next:** "replaced content" = embedded UIA elements inside editable text. Our
Field→StaticText→**exposed** InlineTextBox structure likely surfaces the inner text as an embedded element
NVDA then fails to navigate. Candidate fix: keep `kInline` in the AX tree for AXPosition but DON'T expose it
as a navigable UIA element (mark ignored / no platform node), so NVDA sees flat field text. This is the V1
finish-line bug — the producer→UIA→NVDA chain connects and NVDA recognizes the field; text read-back is the gap.

### Remaining for the full V1
1. **NVDA end-to-end** on an unlocked desktop (the faithful AT, with UIAccess) — start app-first. The only
   remaining gate; everything above is the native UIA *client* (NVDA's API), not NVDA's speech.
2. **Mixed-format runs** (different styles within one field): needs a non-atomic structure exposing per-run
   platform leaves; today's uniform-run attributes read from the field. (Single-run is fully working.)

### (historical) the long road to this — host-side verification, AXTreeManager crash, delegate iterations

### Node data completed per the UIA Text deep-dive (2026-06-25, docs/research/uia-text-provider-findings.md)
`AXPlatformNodeWin` already implements the full Text pattern for a `kTextField`; we just feed it the node.
Closed the clearest gap: the field now carries its text as a **value** (`SetValue`, init + edit) so it pairs
Text with Value and reads as a complete editable surface. Field is `kFocusable` + `tree_data.focus_id=kField`
+ fires `kFocus`; selection set at the caret. These are correct per MSDN but can't be exercised yet because
of the binding issue below.

### NATIVE C++ IUIAutomation probe (the faithful client, same API as NVDA) — DEFINITIVE
Built `scripts/uiaprobe.cpp` (native C++, `CUIAutomation8`/`IUIAutomation`, latest SDK 26100; compiled with
VS Build Tools). Result against the host:
```
ROOT (native IUIAutomation): name='AccessibleWebEdit B-lite host' ctrlType=50032(Window) framework='Win32'
--- raw subtree ---            (EMPTY — no children)
FOCUSED element: name='AccessibleWebEdit B-lite host' ctrlType=50032(Window)
```
So **even native UIA returns the Win32 window provider for our HWND, with no children, and the focused
element is the window — our provider's content is not surfaced.** This is NOT a managed-.NET-client
artifact; it's real and would affect NVDA. **Caveat (per project owner):** `uiaprobe.exe` runs WITHOUT
UIAccess (unsigned, medium IL); NVDA has UIAccess. UIAccess governs cross-integrity reach more than
same-user child enumeration, so this is strong evidence but not 100% identical to NVDA — the fully
faithful test is still NVDA on a desktop.

### Compared our WM_GETOBJECT to Chromium's own views (the known-working reference)
`HWNDMessageHandler::OnGetObject` (ui/views/win/hwnd_message_handler.cc:2140-2144) is **identical** to our
host's: `ax_fragment_root_->GetNativeViewAccessible()->QueryInterface(IRawElementProviderSimple)` then
`UiaReturnRawElementProvider(hwnd, w, l, provider)`. Views' only extra `UiaReturnRawElementProvider(hwnd,0,0,
nullptr)` (line 1950) is **teardown** on WM_DESTROY, not a setup step. So our provider-return path matches
the reference exactly — the difference that makes UIA ignore our provider is **elsewhere** (candidates:
the host is a *console-subsystem* process creating a window; broader app/UIA registration; UIAccess; a
timing/caching subtlety). This is the precise open question for the next focused session.

### IMPORTANT caveat on the earlier managed probe (superseded by the native one above)
All the "1 element / Win32 / un-navigable" results are from **`System.Windows.Automation`**, the *legacy*
.NET UIA client. It may not handle this standalone fragment-root-on-HWND pattern. **NVDA / Inspect.exe use
the modern native `IUIAutomation` (CUIAutomation8)**, which can surface providers the legacy client doesn't.
So the managed probe is **suggestive, not definitive** — the decisive test needs a native `IUIAutomation`
client. Options: (a) Inspect.exe / NVDA on an unlocked/observable desktop (definitive, app-first per the
project owner's guidance); (b) a small compiled C# `CUIAutomation8` console probe (no desktop needed, but
fiddly COM interop). Until one of those runs, "our content isn't surfaced" is unproven for native UIA.

### DEFINITIVE host-side verification (2026-06-25)
Runtime diagnostic from the host itself:
`[host] fragment child non-null=1  fragmentChildDelegate non-null=1  fragmentNVA non-null=1`
i.e. the fragment root has a valid child (our RootWebArea), the child delegate exists, and the
fragment root's accessible is valid. Combined with the earlier traces, **the entire host/provider
side is complete and correct**: `OnGetObject` returns the native provider (hr=0),
`AXFragmentRootWin::get_HostRawElementProvider` returns `UiaHostProviderFromHwnd`, the delegate is the
complete `TestAXNodeWrapper`, and the fragment-root→child link is wired. The earlier `observers_.empty()`
FATAL at exit was my `AXTreeManager` experiment owning+destroying the tree while TestAXNodeWrapper's
global observers outlived it — fixed by leaking the tree (one-shot host) and dropping the manager.

**Despite all that, a native UIA client (`System.Windows.Automation`, i.e. UIA) still surfaces only the
bare `Window` (1 element, `FrameworkId=Win32`).** Since the provider side is verified correct, the
remaining gap is in **how the UIA framework surfaces a standalone (non-Blink, manager-less)
fragment-root provider's content to clients** — not a wiring bug I can fix by more code iteration. This
is exactly the never-done-before piece the project tagged VM-unverified.

### Path forward (specialized, not blind iteration)
- Compare against a **minimal known-working standalone UIA fragment-root sample** (e.g. Windows-classic-
  samples `UIAutomation` provider, or how `views::DesktopWindowTreeHostWin` finalizes its fragment root)
  to find the one framework-level step our host differs on.
- Use **UIA provider-side tracing** / `Inspect.exe` on an observable desktop to watch the actual
  navigation calls the client makes into our provider.
- Then the **NVDA** spoken-output run on an unlocked desktop.
All three need an observable/unlocked desktop and/or a focused UIA-internals session — beyond this
autonomous run. Host source retains `[host]`/`[OnGetObject]` debug couts (remove when done).

### (historical) earlier verdict framing

**One-line:** The host runs on Windows and drives the full producer pipeline including firing the UIA
events; `WM_GETOBJECT(UiaRootObjectId)` returns our native provider correctly — but a UIA client still
sees the HWND as the **default Win32 provider** with our tree un-navigable. Getting a *standalone*
(manager-less, non-Blink) host's UIA provider accepted as the window's host provider is the genuinely
novel, unproven piece (exactly what the project tagged VM-gated). NVDA's spoken-output half is
additionally gated on an unlocked interactive desktop (the RDP session auto-locked).

### What was tried (this session) and the definitive finding
- Fix #1 root→fragment-root `GetParent`: necessary, not sufficient.
- Fix #2 wrap the tree in a platform `AXTreeManager` (+ valid `AXTreeID`): compiled & connected, but the
  UIA client **still** sees only the Window. So node-resolution-via-manager was not the (sole) cause.
- **Definitive:** Chromium's own `ax_fragment_root_win_unittest.cc` / `ax_platform_node_win_unittest.cc`
  build a navigable fragment-root tree using the **full `AXPlatformNodeDelegate` machinery**
  (TestAXNodeWrapper-style: complete `GetData`, hit-testing, `GetTreeManager`, sibling/index, name/value
  computation, etc.). Our host uses a **hand-rolled `BliteNodeDelegate` that overrides only a few methods**
  (`GetParent`/`GetChildCount`/`ChildAtIndex`/`GetNativeViewAccessible`). That incompleteness is why a UIA
  client can't walk our subtree. **The real path: make the delegate complete** (adopt the standard
  node-backed delegate contract) — a substantial, well-scoped effort, not a one-liner.

### UPDATE — TestAXNodeWrapper tried; result invariant. The gap is HWND↔provider binding, not the delegate.
Adopted `TestAXNodeWrapper` (the complete delegate) — compiled clean, **but the UIA client STILL sees only
the Window** (1 element, `FrameworkId=Win32`). The result is now **invariant across four fixes**
(root→fragment-root parent, `AXTreeManager`, focusable/focus_id, complete delegate). Combined with:
- `OnGetObject` returns our native provider correctly (`UiaReturnRawElementProvider`, hr=0 — traced), and
- `AXFragmentRootWin::get_HostRawElementProvider` correctly returns `UiaHostProviderFromHwnd(hwnd)`, and
- our nodes report `FrameworkId=Chrome` (the client sees `Win32`),
…this means **`AutomationElement.FromHandle` is binding to the default Win32 proxy, not our provider** —
a UIA HWND↔provider **binding** issue, not a delegate/navigation one. This is the deepest layer of the
standalone-UIA frontier and is **beyond productive blind rebuild-iteration**.

**Next steps (need a native inspector / unlocked desktop, not more guessing):**
1. **Run `Inspect.exe`** (Windows SDK, `…\bin\<ver>\x64\inspect.exe`) against the host — a native UIA
   client. If Inspect shows our tree, the managed `.FromHandle` probe was a red herring and **NVDA
   (native UIA) would work** once the desktop is unlocked. If Inspect also shows nothing, debug the UIA
   provider connection (UIA provider-side event logging; compare against a minimal known-working
   standalone UIA-provider sample, e.g. the Windows-classic-samples UIAutomation provider).
2. **NVDA on an unlocked console/desktop** (the real target) — currently blocked by the auto-locked RDP
   session.
Both are concrete and decisive; neither is a blind code change. Host source retains temporary
`[OnGetObject]` debug couts (remove when done).

### (superseded) earlier "adopt TestAXNodeWrapper" note
Chromium ships the complete delegate in `:test_support` (already linked by this host):
`ui::TestAXNodeWrapper` — `static TestAXNodeWrapper* GetOrCreate(AXTree* tree, AXNode* node)` →
gives `->ax_platform_node()` and `->GetNativeViewAccessible()`. It's exactly what
`ax_fragment_root_win_unittest.cc` / `ax_platform_node_win_unittest.cc` use to get a navigable tree.
Refactor `BliteAXHost` to **replace the hand-rolled `BliteNodeDelegate`** with `TestAXNodeWrapper`:
materialize wrappers for all nodes in the ctor; `PlatformNodeFor(id)` →
`TestAXNodeWrapper::GetOrCreate(tree_, tree_->GetFromId(id))->ax_platform_node()`;
`GetChildOfAXFragmentRoot()` → `...GetOrCreate(tree_, tree_->root())->GetNativeViewAccessible()`.
Watch the integration: the root↔fragment-root parent linkage (the unittest sets the fragment-root
delegate's child/parent), and the static wrapper-map lifetime. Then incremental relink + the
no-rebuild UIA probe to confirm the subtree (RootWebArea → TextField → Text "hello"→"hello!") appears.
This is a focused ~1-2h effort, not a one-liner — deferred to a deliberate next session rather than
rushed. (Host source currently retains temporary `[OnGetObject]` debug couts to remove afterward.)

### Earlier next-step hypotheses (now superseded by the above)
1. **Host-provider chaining:** confirm `AXFragmentRootWin` returns `UiaHostProviderFromHwnd(hwnd)` from
   `get_HostRawElementProvider`, and that nothing the host does prevents UIA from treating our provider
   as the HWND's host. The `FrameworkId=Win32` + un-navigable tree (client gets "Unrecognized error" on
   first child) points here.
2. **AXTreeManager:** wrap the `AXTree` in a `ui::AXTreeManager` (constructor takes
   `unique_ptr<AXTree>`) and give the tree a real `AXTreeID`, so the platform layer can resolve nodes
   for UIA navigation (`AXPlatformNodeDelegate::GetTreeManager()` is currently null for our delegates).
3. **AX mode:** enable the accessibility mode if the platform nodes gate serving on it.
Each is a small, well-scoped change + the ~2-min incremental relink + the no-rebuild UIA-client probe
(`AutomationElement.FromHandle`, works even while the desktop is locked) to verify.

### Verdict detail

### What works (PROVEN this run)
- **The host runs and drives the full pipeline on Windows** (standalone, exit 0):
  `MockCanvasEditor → Bridge → AXTree → AXEventGenerator → AXPlatformNodeWin → UIA (HWND)`.
  It builds the tree ("hello", caret 5), brings up a real Win32 window, marks the field
  focused (`tree_data.focus_id=kField`, `kFocusable`), fires `kFocus`, applies the insert
  ("hello!", caret 6), generates the expected events (documentSelectionChanged,
  editableTextChanged, textSelectionChanged, valueInTextFieldChanged, nameChanged) and
  **fires `UIA_Text_TextChangedEventId` + `UIA_Text_TextSelectionChangedEventId`**. This is the
  producer spine reaching the Windows UIA layer for the first time — the half `blite_host.cc`
  could not reach on Linux.
- **NVDA runs and captures autonomously** (No-speech synth, log-level 12, quit via `-q`).

### Why NVDA didn't announce (root cause, from the NVDA log)
NVDA logged: `Foreground took too long to change. Foreground still 0 (). Should be … (BliteHostWindow)`.
So NVDA SAW our window request foreground, but the OS foreground **stayed 0** — i.e. the desktop is
**LOCKED**. Confirmed: `LogonUI` present, `GetForegroundWindow()=0`, RDP session idle **2h54m**
(auto-locked after the user signed off). On a locked/secure desktop **no window can be foreground**,
so NVDA cannot navigate into our window's UIA subtree or register listeners — hence silence. This is
exactly the **V8 RDP-vs-console caveat**. It is an environment gate, **not** a host bug (the host fired
everything correctly; NVDA even picked up the host's console stdout via the terminal UIA).

### What's needed to finish the NVDA half
An **unlocked, interactive desktop** during the capture (the TASK-00 "interactive session" requirement).
On this RDP box the session locks when the user disconnects/idles. Options:
1. User keeps the session unlocked/connected during a capture (simplest), or
2. Disable auto-lock (needs admin: `DisableLockWorkstation` was Access-Denied for me), or
3. Redirect to the console session (`tscon`) so it stays interactive (disconnects the user's RDP — they
   said they can work from another machine), or run on the physical console.

### UIA-client probe results (NVDA-independent; runs while desktop is locked)
A raw UIA client (`AutomationElement.FromHandle`) attaches to the host window but sees **only the
Window — our editor tree is NOT navigable**. Debugged with `OnGetObject` instrumentation:
- `WM_GETOBJECT(UiaRootObjectId=-25)` **is** received and our handler **returns the native provider**
  successfully (`uia_enabled=1`, `root_accessible=yes`, `QI IRawElementProviderSimple hr=0x0`,
  `UiaReturnRawElementProvider`). So the provider hand-off is correct.
- But the client reports `FrameworkId=Win32` for the window and `GetFirstChild` throws
  **"Unrecognized error"** (0 children). So **navigation from the fragment root into its child
  (our RootWebArea) fails.**
- Applied fix #1: root node's `GetParent()` now returns the HWND fragment root (was `nullptr`) — the
  header requires the internal root to navigate back to the fragment root. Necessary but **not sufficient**.
- **Leading hypothesis (next step):** this standalone host has **no `AXTreeManager`** wrapping the
  `AXTree`. `AXFragmentRootWin`/`AXPlatformNodeWin` resolve cross-node UIA navigation via
  `GetFromTreeIDAndNodeID` / the manager; without a registered manager (and possibly without an
  enabled `AXMode`), fragment→child navigation returns an error. The likely fix is to wrap the tree in
  an `AXTreeManager` (and/or enable the accessibility mode) so the platform layer can resolve nodes.
  This is the genuine "standalone UIA provider" frontier the project always tagged VM-unverified.

### Unblocked alternative being pursued now
Verify the **producer→UIA contract with a raw UIA client** (`AutomationElement.FromHandle`), which attaches
by window handle and does **not** need foreground/NVDA/an unlocked desktop. If UIA exposes our editor tree
(field + "hello"→"hello!") and the TextChanged event, the core thesis (custom non-Blink tree → UIA) is proven
independently; only the NVDA-spoken-output half then waits for an unlocked desktop. See
`results/V1-uia-client-probe.md`.
