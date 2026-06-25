# AccessibleWebEdit — end-to-end demo

A runnable, cross-platform demonstration of the project thesis: a **custom
(non-Blink) canvas editor** whose *one layout pass* drives **both** the pixels a
sighted user sees **and** the accessibility events a screen reader speaks — with
no ARIA and no DOM mirror.

![demo](demo.gif)

*The Linux headless backend replaying the shared edit script: type, select
"World", replace it, backspace, move the caret. Every frame's pixels and its
accessibility geometry come from the same layout.*

## The one idea

There is a single source of truth — `core/layout_engine.h::LayOut()` — and two
consumers that read its output:

```
                         core/text_document.h   (the editable model)
                                  |
                                  v
                    core/layout_engine.h  ── LayOut() ──►  Layout   (one pass)
                                  |                            |
                 ┌────────────────┘                            └────────────────┐
                 v                                                              v
        core/raster.h  → pixels                          AXTreeUpdate (kCaretBounds,
   (PPM on Linux, GDI on Win32, Skia later)               value, selection)
                                                                   |
                                                                   v
                                              real AXTree + AXEventGenerator
                                              → (Windows) AXPlatformNodeWin/UIA → NVDA
```

Because the painted caret and the caret the screen reader announces are derived
from the *same* `Layout`, they cannot drift — the constraint `docs/11` calls
non-negotiable, here made executable.

## What runs where (proven vs staged)

| Backend | What it does | Status |
|---|---|---|
| **`platform/linux_headless/demo_render.cc`** | Replays the edit script, paints each step to a PPM frame, prints the matching a11y geometry. Standalone `g++`, no deps. | ✅ runs on Linux now → `demo.gif`, `contact_sheet.png` |
| **`platform/linux_headless/demo_e2e.cc`** | Same core feeding a **real Chromium `AXTree` + `AXEventGenerator`**; dumps the actual generated events + `kCaretBounds` per step. | ✅ built & run in the Chromium tree on Linux → `results/demo-e2e-run.txt` |
| **`platform/win32/win_main.cc`** | A real Win32 window with live keyboard input, painting the editor via GDI from the same raster. Standalone MSVC, no Chromium. | 🪟 builds & runs on Win11 (`build_win.ps1`) |
| **`platform/win32` Path A (UIA→NVDA)** | `demo_e2e` + Chromium's `AXPlatformNodeWin` in the Win32 window → UIA events → NVDA speech. | 📋 staged for Win11 (`uia_bridge.md`); the fidelity-proving run |

So "show it on Linux **or** Windows" is real today: Linux gives you the frames +
the real generated events; Windows gives you the interactive window. The final
fusion (UIA + NVDA on Win11) is documented and code-staged, not yet run — and
nothing here claims NVDA fidelity before that run.

## Run it

### Linux — visual frames (no checkout needed)
```sh
cd platform/linux_headless
g++ -std=c++17 -O2 -I../../core demo_render.cc -o /tmp/demo_render
/tmp/demo_render /tmp/frames               # writes frame_000.ppm ..
python3 ../../tools/make_media.py /tmp/frames .   # -> demo.gif + contact_sheet.png
```

### Linux — real accessibility events (Chromium tree)
Flatten the core headers + `demo_e2e.cc` + its `BUILD.gn` into
`//ui/accessibility/demo_e2e/`, add `//ui/accessibility/demo_e2e:demo_e2e` to
the GN root group `//ui/accessibility/t2:t2` (see `results/ENVIRONMENT.md`),
then:
```sh
gn gen out/rel
ninja -C out/rel ui/accessibility/demo_e2e:demo_e2e
./out/rel/demo_e2e            # capture: results/demo-e2e-run.txt
```

### Windows 11 — interactive window
```powershell
cd platform\win32
pwsh -File build_win.ps1       # locates MSVC, compiles win_main.cc
.\win_demo.exe                 # type, Backspace, ←/→/Home/End
```
For the UIA → NVDA path (Path A), see `platform/win32/uia_bridge.md`.

## Layout

```
demo/
  core/                     shared, dependency-free (compiles standalone AND in-tree)
    text_document.h         editable model: insert/backspace/caret/selection
    layout_engine.h         the single layout pass  (THE source of truth)
    font5x7.h               lean bitmap font (the throwaway part)
    raster.h                CPU raster -> RGB / PPM
    demo_script.h           the canonical edit sequence, shared by all backends
  platform/
    linux_headless/         demo_render.cc (pixels) + demo_e2e.cc (real AXTree) + BUILD.gn
    win32/                  win_main.cc (window+input+GDI) + build_win.ps1 + uia_bridge.md
  tools/make_media.py       PPM frames -> animated GIF + contact sheet
  demo.gif, contact_sheet.png
```

The staged build plan (Stages 0–4, from this lean demo to NVDA on Win11) is
`docs/14-drawn-demo.md`.
