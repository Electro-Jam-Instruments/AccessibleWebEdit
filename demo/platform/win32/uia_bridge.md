# Win32 accessibility: from the demo core to UIA and NVDA

This is the accessibility half of the Windows end-to-end. The `win_main.cc`
frontend gives you a real window + keyboard + pixels with no dependencies; this
doc is how the **same edits become UIA events a screen reader (NVDA) speaks**.

There are two paths. They share the demo core (`text_document.h`,
`layout_engine.h`, the edit operations); they differ only in what consumes the
resulting `AXTreeUpdate`.

## Path A (recommended) — in-tree, real AXPlatformNodeWin/UIA

This is the highest-fidelity route and the one the whole project thesis rests
on: feed Chromium's real `AXTree`, then let Chromium's **own** Windows platform
layer (`AXPlatformNodeWin`) translate generated events into UIA. It is exactly
the Linux `demo_e2e` build (`demo/platform/linux_headless/demo_e2e.cc`) plus the
platform-node layer, built on Windows.

What changes from the Linux `demo_e2e`:

1. **Link the platform layer.** Depend on `//ui/accessibility/platform` (the
   `AXPlatformNode` target) in addition to `:accessibility_internal`. On Windows
   this brings `AXPlatformNodeWin`, the UIA provider.
2. **Host an HWND + a tree manager.** Create the Win32 window (reuse
   `win_main.cc`) and an `AXPlatformTreeManager` over the `AXTree`, so each
   `AXNode` gets an `AXPlatformNodeWin` with an `IRawElementProviderSimple`.
3. **Answer `WM_GETOBJECT`.** Return the root provider via
   `UiaReturnRawElementProvider`, so UIA clients (NVDA) can connect.
4. **Pump events.** When you `Unserialize` a delta and the generator produces
   `CARET_BOUNDS_CHANGED` / `*_TEXT_CHANGED` / `*_SELECTION_CHANGED`, the
   platform layer fires the matching UIA events
   (`UIA_Text_TextChangedEventId`, `UIA_Text_TextSelectionChangedEventId`) —
   gated on the field node advertising `UIA_TextPatternId` (the producer
   obligation from `docs/03` §2.2).

The generated-event set this produces is already proven on Linux — see
`results/demo-e2e-run.txt`: each keystroke yields `editableTextChanged` +
`valueInTextFieldChanged` on the field, `documentSelectionChanged` +
`textSelectionChanged`, `caretBoundsChanged`, and `nameChanged` on the leaf.
Path A is what turns those into UIA on Windows.

> Building this is the heavier lift (it pulls the platform target's cone). It is
> the doc-14 **Stage 2/3** milestone and belongs on the Win11 box, not the lean
> Linux container.

## Path B — standalone UIA provider (lighter, lower fidelity)

If you want UIA without the Chromium platform cone, implement a minimal UIA
provider directly on the editor: a single automation element for the field
exposing `ITextProvider`/`ITextProvider2` (Text pattern), backed by the demo's
text + the per-character bounds from `layout_engine.h`. Fire
`UiaRaiseAutomationEvent` / `UiaRaiseTextEditTextChangedEvent` on edits. This is
real UIA NVDA can read, but you re-implement the Text-pattern surface Chromium
already provides in Path A — useful as a teaching exhibit, not the product
path. The `patches/itextprovider2-rangefromannotation-prototype.md` and
`patches/selectionprovider2-prototype.md` sketches are relevant here.

## NVDA verification (either path)

Per `docs/TASK-00a-nvda-verification.md`:

1. Install NVDA (the local-setup script does this).
2. Set the synth to **"No speech"** and enable speech-viewer / a speech log
   (`--log-level=12`), or use SystemTestSpy for programmatic capture.
3. Launch the demo, focus the window, type. Confirm NVDA announces the inserted
   characters, the caret movement, and the selected text — i.e. *see it (pixels)
   and hear it (NVDA) from the one layout pass.*
4. That run is the point at which the fidelity claim moves from **VM** to
   **PROVEN**; capture the speech log under `results/`.

## Why this is honest

Today, runnable and verified: the pixels + input (`win_main.cc`, standalone) and
the real generated events (`demo_e2e`, Linux). Not yet run: the UIA translation
and NVDA speech — those are Path A on Win11. Nothing here claims NVDA fidelity
before that run happens.
