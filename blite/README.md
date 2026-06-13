# B-lite host — Linux lean spine

Date: 2026-06-13. A standalone **runnable executable** (not a unit test) that stands in for the AccessibleWebEdit canvas editor and drives Chromium's `ui/accessibility` layer directly — the non-Blink tree-producer architecture from docs/09, as an actual program.

## What it is

`blite_host` runs the platform-agnostic spine end to end:

```
MockCanvasEditor (custom surface: text + caret)
  -> Bridge (emits AXTreeData + AXTreeUpdate deltas)
    -> AXTree (+ AXEventGenerator)
```

It builds the initial tree from the editor model, applies one keystroke as a single-node text delta plus a tree-data caret move (exactly how the editor would post a keystroke), and dumps the tree + the generated event set. Captured output: `results/blite-host-run.txt`.

The keystroke produces the editing event set the T2 tests predicted — `editableTextChanged` + `valueInTextFieldChanged` on the field ancestor, `documentSelectionChanged` + `textSelectionChanged`, `nameChanged` on the text leaf — now demonstrated from a real application driving the live pipeline, not a fixture.

## What it deliberately does NOT include (held for the Windows VM)

The **platform-node layer** — `AXPlatformNode` (which is `AXPlatformNodeWin`/UIA on Windows, `AXPlatformNodeAuraLinux`/AT-SPI with `use_atk`) — is intentionally not linked. Materializing it leaves the lean build cone and pulls a large new build (v8 via gin; aura/views/glib for AT-SPI). Per the 2026-06-13 decision, that layer and all screen-reader work are done on the Windows VM, where UIA is the real target. This host is the spine the VM build extends: the VM adds a Win32 window + the already-existing `IS_WIN` UIA provider; the surface→bridge→tree→events code here is unchanged across platforms.

## Build & run (in a Chromium 149.0.7827.115 checkout)

Drop `blite_host.cc` + `BUILD.gn` into `ui/accessibility/blite/`, use `t2-root-BUILD.gn` as the GN root group (or add `//ui/accessibility/blite:blite_host` to any target), build with the lean args in `tests/linux-t2/args.gn`, then:

```
ninja -C out/rel blite_host && ./out/rel/blite_host
```

Depends only on `//ui/accessibility:accessibility_internal` + `//base` — the same lean cone as the T2 tests, no platform layer, no v8.
