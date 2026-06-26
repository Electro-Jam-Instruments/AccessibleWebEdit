# B-lite host — Windows / UIA slice (NVDA end-to-end)

Status: **[SOURCE-LEVEL / VM-UNVERIFIED] DRAFT.** Written offline against Chromium tag `149.0.7827.115` by source-reading; **not compiled, not run.** This is review-backlog item **V1** (`results/expert-reviews/REVIEW-BACKLOG.md`): the smallest *insert + caret move → UIA → NVDA* slice — the moment the fidelity thesis is proven or broken. Do not upgrade any claim here to PROVEN until it actually runs and NVDA speaks. Every API call whose exact tag-149 signature is uncertain carries an inline `TODO(VM)` in `blite_host_win.cc`; resolve them all against the real headers before building.

## What it is

`blite_host_win` is the Windows counterpart to the Linux spine (`blite_host.cc`). It runs the **same** platform-agnostic spine — `MockCanvasEditor → Bridge → AXTree → AXEventGenerator` — and then **adds the platform/UIA layer the Linux host deliberately omitted**, so a real UIA client (NVDA) can attach to a real window and observe editing events:

```
MockCanvasEditor (custom surface: text + caret)        ┐
  -> Bridge (emits AXTreeData + AXTreeUpdate deltas)    │ identical to
    -> AXTree (+ AXEventGenerator)                       │ blite_host.cc
                                                         ┘
      -> AXPlatformNodeWin / AXPlatformNodeDelegate      ┐ NEW on Windows
        -> AXFragmentRootWin  (HWND <-> UIA bridge)      │ (the platform
          -> UIA (IRawElementProviderSimple/ITextProvider)│  layer)
            -> NVDA  ->  nvda-run.log "Speaking [...]"   ┘
```

It builds the initial tree from the editor, stands up a real Win32 HWND, wires UIA so `WM_GETOBJECT` resolves a provider, then injects **one** text insert + **one** caret move as a single atomic `AXTreeUpdate` (docs/03 §3.2) and fires the platform events so a UIA client surfaces `UIA_Text_TextChangedEventId` + `UIA_Text_TextSelectionChangedEventId`.

## Design — what the platform layer adds over the spine

| Concern | Spine (`blite_host.cc`, Linux) | This host (Windows) |
|---|---|---|
| Surface → tree → events | yes | yes (verbatim copy) |
| Platform node | **not linked** (lean cone) | `AXPlatformNode::Create(delegate)` → `AXPlatformNodeWin` per node |
| Delegate | none | `BliteNodeDelegate : AXPlatformNodeDelegate` (one per `AXNode`; serves `GetData`, parent/child nav, `GetNativeViewAccessible`) |
| Window | none | real top-level HWND: `RegisterClassExW` / `CreateWindowExW` + `GetMessage` loop |
| HWND ↔ UIA | none | `AXFragmentRootWin` + `AXFragmentRootDelegateWin`; `WM_GETOBJECT` → `UiaReturnRawElementProviderForHwnd` |
| Edit events | generated only | generated **and** fired to UIA (`NotifyAccessibilityEvent` → UIA event ids) |

Key wiring (class / method names in `blite_host_win.cc`):

- **Per-node delegate:** `BliteNodeDelegate` overrides `AXPlatformNodeDelegate::GetData`, `GetNativeViewAccessible`, `GetParent`, `ChildAtIndex`, `GetChildCount`; it constructs its platform node via `AXPlatformNode::Create(this)` and tears it down via `AXPlatformNode::Destroy()`.
- **HWND ↔ UIA bridge:** `BliteAXHost : AXFragmentRootDelegateWin` creates `AXFragmentRootWin::Create(hwnd, this)` and answers `WM_GETOBJECT` in `OnGetObject(...)` via `fragment_root_->GetNativeViewAccessible()` + `UiaReturnRawElementProviderForHwnd(...)`. It implements `GetChildOfAXFragmentRoot` (→ document root node), `GetParentOfAXFragmentRoot`, `IsAXFragmentRootAControlElement`.
- **Window:** `CreateHostWindow()` (`RegisterClassExW`/`CreateWindowExW`), `BliteWndProc` routes `WM_GETOBJECT` to the host; `Run()` pumps `GetMessage`/`TranslateMessage`/`DispatchMessage`.
- **Edit events:** `FirePlatformEditEvents()` calls `AXPlatformNode::NotifyAccessibilityEvent` for `kValueInTextFieldChanged`, `kTextSelectionChanged`, `kDocumentSelectionChanged` (with a raw `UiaRaiseAutomationEvent` fallback noted inline).

Why it leaves the lean cone: linking `//ui/accessibility/platform` pulls `AXPlatformNodeWin` / `AXFragmentRootWin` and their deps — expected on Windows, and the whole point (the Linux host stayed lean specifically by *not* doing this). See `blite_host_win-BUILD.gn`.

## Build & run (in a Chromium 149.0.7827.115 checkout, on Windows 11)

Prereqs: VS Build Tools (C++ + Win11 SDK), depot_tools, the synced checkout, NVDA installed — all from `docs/TASK-00b-local-windows-setup.md` (run `scripts/setup-local-windows.ps1` elevated).

1. **Drop the files into the tree** (rename to the canonical filenames Chromium expects):

   ```
   <SrcRoot>\chromium\src\ui\accessibility\blite\blite_host_win.cc
   <SrcRoot>\chromium\src\ui\accessibility\blite\BUILD.gn      (from blite_host_win-BUILD.gn,
                                                                 or merge its target into the
                                                                 existing blite/BUILD.gn)
   ```

   Add `//ui/accessibility/blite:blite_host_win` to a root group (e.g. extend `t2-root-BUILD.gn`) or name it directly on the ninja command line.

2. **Generate + build** (component build, dchecks on — matches the T2 baseline):

   ```
   cd <SrcRoot>\chromium\src
   gn gen out\rel --args="is_debug=false is_component_build=true dcheck_always_on=true"
   ninja -C out\rel blite_host_win
   ```

3. **Run the host** (leave it running — it pumps a message loop and waits for a UIA client):

   ```
   out\rel\blite_host_win.exe
   ```

   It prints the tree state + generated events, fires the UIA events, then blocks in the message loop. Close the host window to exit.

## NVDA capture (the verification channel — docs/TASK-00a)

Run NVDA as a UIA client against the host with the **No speech** synth and an input/output-level log, then grep the log for what it would have spoken.

1. **One-time:** create the pinned config (`Speech → synthesizer = No speech`, no update checks) at `C:\awe\nvda-config` (see TASK-00a "Project config profile").

2. **Launch NVDA pointed at the host run** (separate elevated-desktop session; NVDA needs an interactive desktop — it is *not* truly headless):

   ```powershell
   & "$env:ProgramFiles\nvda\nvda.exe" `
       -m `                                   # --minimal: no sounds/interface/start message
       -c C:\awe\nvda-config `                # pinned No-speech profile
       --log-level=12 `                       # input/output: logs speech + gestures
       --log-file=C:\awe\logs\nvda-run.log
   ```

3. **Drive the slice:** with `blite_host_win.exe` running, focus the host window (Alt+Tab or `SetForegroundWindow`); the host has already injected the insert + caret move and fired the UIA events. For an interactive caret move, inject input via PowerShell `SendKeys` / `keybd_event` (TASK-00a "Scenario driving").

4. **Extract the utterance sequence** (live or post-run):

   ```powershell
   # live tail while driving:
   Get-Content C:\awe\logs\nvda-run.log -Wait -Tail 50

   # post-run: pull just the spoken output
   Select-String -Path C:\awe\logs\nvda-run.log -Pattern "Speaking" 
   ```

   The `Speaking [...]` lines are the NVDA-observable record. Commit the captured log under `results/` exactly like the Linux event logs, alongside an `ax_dump_events` capture for the Chromium-side event chain (tree delta → generated event → UIA event → announcement).

> Caveat (TASK-00a open item #1): the `Speaking` line format is an NVDA implementation detail and is regex-fragile across versions. Pin the parse regex to the installed NVDA version on first run, or graduate to NVDA's SystemTestSpy (Strategy B) if a durable suite is wanted.

## TODO(VM) — verified against real tag-149 headers (see "Windows compile results")

Every item below has now been resolved against the real `149.0.7827.115` headers at
`C:\src\chromium\src` and the code/BUILD updated. Original guesses (and their
corrections) are recorded in the next section.

- **Delegate base class** — derive from `AXPlatformNodeDelegate` vs a concrete base that supplies default impls (affects how many pure-virtuals must be implemented).
- **`AXPlatformNode::Create(delegate)`** factory signature and **`Destroy()`** teardown (ref-counted, not `delete`).
- **`AXPlatformNodeDelegate` accessor names/return types:** `GetData()`, `GetNativeViewAccessible()`, `GetParent()`, `ChildAtIndex(size_t)`, `GetChildCount()`, unique-id accessor.
- **`AXNode` navigation names:** `GetChildCount()` / `GetChildAtIndex()` / `GetParent()` at 149.
- **`AXFragmentRootWin::Create(HWND, AXFragmentRootDelegateWin*)`** signature (HWND vs `gfx::AcceleratedWidget`).
- **`AXFragmentRootDelegateWin`** interface: exact method names (`GetChildOfAXFragmentRoot` / `GetParentOfAXFragmentRoot` / `IsAXFragmentRootAControlElement`).
- **`WM_GETOBJECT` answer path:** `fragment_root_->GetNativeViewAccessible()` + `UiaReturnRawElementProviderForHwnd`, vs a fragment-root helper that does the `UiaReturn*` itself.
- **`NotifyAccessibilityEvent` → UIA event-id mapping** for `kValueInTextFieldChanged` → `UIA_Text_TextChangedEventId` and `kTextSelectionChanged`/`kDocumentSelectionChanged` → `UIA_Text_TextSelectionChangedEventId` — or fall back to raw `UiaRaiseAutomationEvent`.
- **COM apartment** — `ScopedCOMInitializer` STA vs MTA for the UIA provider path.
- **base task environment** — whether `SingleThreadTaskExecutor` is required by the platform layer at 149.
- **BUILD label** — `//ui/accessibility/platform` umbrella vs `:platform` explicit target; whether `libs` (uiautomationcore/user32/ole32) are already transitive.

## Windows compile results (tag 149.0.7827.115)

The draft was placed at `//ui/accessibility/blite` in the real checkout
(`C:\src\chromium\src`) and built as `//ui/accessibility/blite:blite_host_win`
(reachable via a `group("blite_win")` added to `//ui/accessibility/BUILD.gn`),
with the existing `out/rel` args (`is_component_build=true dcheck_always_on=true
ax_t2_minimal=true`). The host was iterated against the real headers until the
`TODO(VM)` API guesses were resolved. It was **not run** (Smart App Control
blocks freshly-built exes on the build box, and NVDA end-to-end is the actual
open verification) — this section is COMPILE/LINK only.

**Status:** the host translation unit (`blite_host_win.cc`) **compiles cleanly**
against the real tag-149 headers — verified by compiling it with the exact ninja
command line (full clang plugin set: raw-ptr / blink-gc / find-bad-constructs /
unsafe-buffers, plus `/WX` warnings-as-errors), `clang-cl` exit 0, ~1 MB object.
That is the substantive result: every resolved `TODO(VM)` API is accepted by the
real headers. The final **link** of the executable pulls the full
`//ui/accessibility` → `//ui/base` → **V8** dependency closure (see "Build cost
note"); on this box that cold closure is a multi-hour compile, so the link step
was still completing at hand-off. No code-level blockers remain — the link is
gated only on that dependency build finishing. The `group("blite_win")` builds
with `ninja -C out/rel ui/accessibility:blite_win`.

### TODO(VM) APIs — confirmed / corrected (old → new)

| Area | Draft guess | Real tag-149 API (header) | Action |
|---|---|---|---|
| Delegate base | abstract base, or a separate `AXPlatformNodeDelegateBase` with default impls | `AXPlatformNodeDelegate` is **concrete**: owns the backing `AXNode*`, has a protected `AXPlatformNodeDelegate(AXNode*)` ctor + `SetNode`/`node()`, and supplies defaults for almost everything (`ax_platform_node_delegate.h`/`.cc`). | **Simplified.** Subclass derives via `: AXPlatformNodeDelegate(node)`; only the navigation quartet needs overriding. |
| Platform node factory | `AXPlatformNode::Create(delegate*)`, then `platform_node_->Destroy()` in dtor | `static AXPlatformNode::Pointer Create(AXPlatformNodeDelegate& delegate);` — takes a **reference**, returns `unique_ptr<AXPlatformNode, Deleter>` whose deleter calls `Destroy()` (`ax_platform_node.h:48`). | **Corrected.** Stored as `AXPlatformNode::Pointer`; manual `Destroy()` removed (RAII). |
| `GetData()` | overridden, pure-virtual | Non-virtual-effectively; base returns `node_->data()`. | **Dropped override** (base handles it). |
| `GetNativeViewAccessible` | const | **non-const** virtual (`...delegate.h:221`) | Override signature fixed to non-const. |
| `GetParent`/`GetChildCount`/`ChildAtIndex` | const, names correct | const, names correct, but base defaults are **no-ops** (null/0) — must override to wire the AXNode tree to platform nodes (`...delegate.cc:162/183/187`). | Kept overrides; confirmed required. |
| Unique id | maybe override `GetUniqueId()` / use `AXNodeID` | Base `GetUniqueId()` already returns a stable per-instance `AXPlatformNodeId` (lazily-created `AXUniqueId`) — exactly what UIA runtime-ids need. | **No override** needed. |
| AXNode nav | `GetParent()/GetChildCount()/GetChildAtIndex()` | identical at 149 (`ax_node.h:131/138/142`). | Confirmed. |
| `AXFragmentRootWin` ctor | `AXFragmentRootWin::Create(HWND, delegate*)` factory | **No factory.** Public ctor `AXFragmentRootWin(gfx::AcceleratedWidget widget, AXFragmentRootDelegateWin* delegate)` (`ax_fragment_root_win.h:36`); `AcceleratedWidget == HWND`. | **Corrected** to `std::make_unique<AXFragmentRootWin>(hwnd, this)`. |
| `AXFragmentRootDelegateWin` header | declared in `ax_fragment_root_win.h` | lives in its **own** header `ax_fragment_root_delegate_win.h`; three methods exactly as guessed. | Include corrected; method names confirmed. |
| `WM_GETOBJECT` answer | `fragment_root_->GetNativeViewAccessible()` → `IRawElementProviderSimple*` then `UiaReturnRawElementProviderForHwnd` | `NativeViewAccessible == IAccessible*` on Win, **not** `IRawElementProviderSimple*`. Authoritative pattern (`hwnd_message_handler.cc:2121`): switch on `static_cast<LONG>(lparam)`; for `UiaRootObjectId`, `QueryInterface` the fragment root's `IAccessible` to `IRawElementProviderSimple` and call **`UiaReturnRawElementProvider`** (not `...ForHwnd`); for `OBJID_CLIENT`, `LresultFromObject(IID_IAccessible, wparam, accessible)`. | **Corrected** (both API and the IAccessible→provider QI). |
| Edit-event mojo ids | `kValueInTextFieldChanged` → TextChanged; `kTextSelectionChanged`/`kDocumentSelectionChanged` → TextSelectionChanged | `MojoEventToUIAEvent` (`ax_platform_node_win.cc:8203`) maps **only** `kTextChanged → UIA_Text_TextChangedEventId` and `kTextSelectionChanged → UIA_Text_TextSelectionChangedEventId`. `kValueInTextFieldChanged` and `kDocumentSelectionChanged` map to **nullopt** (raise no UIA event). | **Corrected** to fire `kTextChanged` + `kTextSelectionChanged` on the field. |
| AXPlatform instance | not addressed | `AXPlatformNode`/`AXPlatformNodeWin` require a process-wide `AXPlatform` (`AXPlatform::GetInstance()`); it takes a `Delegate&`. Test harness wraps one in `AXPlatformForTest` (`ax_platform_for_test.h`, in `//ui/accessibility:test_support`). | **Added** `ui::AXPlatformForTest` in `main()`. |
| BUILD: platform label | `//ui/accessibility/platform` (umbrella) or `:platform` | The `platform` component restricts `visibility` to an allowlist that excludes `//ui/accessibility/blite` → a **direct dep is a GN error**. The `//ui/accessibility` group (`public_deps = [accessibility_internal, platform]`) is on the allowlist. | **Corrected** to depend on `//ui/accessibility` (+ `//ui/accessibility:test_support`). |
| `libs` | `uiautomationcore/user32/ole32` | `LresultFromObject` needs **`oleacc.lib`**; rest confirmed. | Added `oleacc.lib`. |
| COM apartment | STA vs MTA | `ScopedCOMInitializer` default = STA, correct for a Win32 message-loop UIA provider (the fragment-root path also `CreateATLModuleIfNeeded()`). | Kept STA. |
| Header include order | `<windows.h>` then `<uiautomation.h>` | The Chromium build defines `WIN32_LEAN_AND_MEAN`, so `<windows.h>` does **not** pull `<objbase.h>`. `<uiautomation.h>` (`UIAutomationCore.h`) uses the MSVC `interface` keyword, only defined (as `struct`) by `<objbase.h>`/`<combaseapi.h>` — so without them clang-cl errors `unknown type name 'interface'`. | **Corrected.** Include `<objbase.h>` and `<oleacc.h>` **before** `<uiautomation.h>` (matches `ax_platform_node_win.h`'s objbase→oleacc→uiautomation order). This was the one genuine compile error found in the host TU; it now compiles clean with the full ninja flag set (all clang plugins + `/WX`). |

### Reachability / build wiring

- Files placed at `C:\src\chromium\src\ui\accessibility\blite\` (`blite_host_win.cc`, `blite_host.cc`, `BUILD.gn`).
- A target is only in the ninja graph if reachable from a root, so a `group("blite_win")` depending on `//ui/accessibility/blite:blite_host_win` was added to `//ui/accessibility/BUILD.gn` (guarded `if (is_win)`). Build with: `ninja -C out/rel ui/accessibility:blite_win`.

### Build cost note

Because `blite_host_win` links the real UIA platform layer, it pulls the full
`//ui/accessibility` group → `//ui/base` cone → **V8**, etc. — a large
dependency closure the lean `ax_t2_minimal` cone (used by the 109 T2 tests)
never compiles. So the first build of this target is a long cold compile of that
closure (V8 included); subsequent incremental builds are fast. This is expected
and is build-time only — it does not reflect on the host code.

### Remaining / not-yet-done

- **Runtime UIA enablement:** `AXPlatform::IsUiaProviderEnabled()` is additionally gated on `base::Feature features::kUiaProvider` and has no public setter. For the live NVDA run, enable it the standard way (`--enable-features=UiaProvider` / `ScopedFeatureList`). Compile-time only: unaffected.
- **Not run / NVDA:** still `[VM-UNVERIFIED]`. The exe has not been executed (SAC) and NVDA has not observed it. All UIA/NVDA fidelity claims stay un-upgraded until the end-to-end slice actually speaks (review-backlog V1).
