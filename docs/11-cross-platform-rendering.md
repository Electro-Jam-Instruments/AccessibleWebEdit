# 11 — Cross-Platform Text-Rendering UI: What It Would Take

Date: 2026-06-13. Scope: the sighted-facing rendering surface (the canvas editor's pixels) across Windows, Mac, Linux, Android, iOS. Priority order: **Windows > Mac > Linux > mobile**. Companion to docs/09 (a11y tree) and the B-lite spine (blite/).

## Framing: two cross-platform problems, one already mostly solved

1. **Accessibility surface (AX tree -> screen reader)** — already cross-platform if we stay on Chromium's `ui/accessibility`. `AXPlatformNode` has a per-platform implementation for all five targets: `AXPlatformNodeWin` (UIA), `…Mac` (NSAccessibility), `…AuraLinux` (AT-SPI), Android (`AccessibilityNodeInfo`), iOS (UIAccessibility). This is the B-lite work multiplied across platforms — wiring existing code, not inventing.

2. **Visual text-rendering surface (pixels)** — the new build, and the subject of this doc.

## Decomposition: ~80% shared core, thin per-platform shell

**Shared core, write-once C++ (the bulk):**
- Document model + edit operations
- Text layout + shaping: HarfBuzz (shaping) + line-breaking/wrap. Skia's `SkParagraph`/`SkShaper` (Flutter's stack) is the natural fit since Skia is already in the tree.
- Painting: Skia (Ganesh/GL or Graphite) — runs on all five platforms already.
- Selection/caret geometry, hit-testing.
- The AX bridge: the `AXTreeUpdate` producer (the existing B-lite spine).

**Per-platform shell (thin, but each has one genuinely hard piece):**

| Concern | Windows (1st) | Mac (2nd) | Linux (3rd) | Android / iOS (4th) |
|---|---|---|---|---|
| Window + GPU surface | Win32 + ANGLE/D3D | Cocoa + Metal | Ozone (X11/Wayland) + GL | SurfaceView / UIView + GL/Metal |
| Text input / IME (HARD) | TSF | NSTextInputClient | IBus/fcitx | InputConnection / UITextInput |
| Font enumeration | DirectWrite | CoreText | fontconfig | system SkFontMgr |
| A11y provider | UIA | NSAccessibility | AT-SPI | NodeInfo / UIAccessibility |

**The hard part is IME/text input, not rendering.** Skia text drawing is well-trodden; what eats cross-platform editor projects is composition/input — each platform has a completely different text-input framework, and it ties directly to the accessibility composition story (T2-4: composition flows from the input stack, not the tree producer). Budget most per-platform effort here.

## The non-negotiable design constraint

**One layout engine is the single source of truth feeding both the painter and the AX tree.** The screen reader needs character/word/line bounds, caret rects, and inline-text-box geometry (the `kCaretBounds` finding from the T2 tests), and those must be the exact coordinates the renderer paints. If rendering and accessibility compute layout separately they drift, and braille/caret tracking breaks. Architecture: one layout pass -> (a) Skia paint, (b) AX tree with geometry. Expensive to retrofit; get it right up front.

## Two build strategies

**(A) In the Chromium tree — RECOMMENDED (given the existing `ui/accessibility` commitment).** Reuse Skia + `gfx` + Ozone + `AXPlatformNode` + **`ui/base/ime`** (Chromium already has per-platform `TextInputClient`/TSF/NSTextInputClient integration). Cross-platform surface, text, IME, and accessibility come already integrated and battle-tested. Cost: build inside Chromium's heavy build and pull a large dependency cone. But the hardest pieces — IME and a11y — are already written for all five platforms.

**(B) Standalone app.** Link Skia + SkParagraph/HarfBuzz + just the `ui/accessibility` library; write own per-platform windowing/IME/font glue. Lighter binary, full control, but you re-implement the IME and surface plumbing Chromium already has — rebuilding the hardest 20% yourself, five times.

Recommendation: **(A)**. The project is already committed to Chromium's `ui/accessibility`, and `ui/base/ime` solves the IME problem you'd otherwise face on every platform. (B) only wins if not shipping a Chromium-sized dependency is a hard product requirement.

## Sequencing by priority

- **Windows first:** Win32 + ANGLE surface, TSF input, DirectWrite fonts, UIA a11y. Also where the whole UIA thesis lives, so it doubles as the proof platform. This milestone IS the B-lite app made visual + interactive on the VM.
- **Mac second:** Cocoa + Metal, NSTextInputClient, CoreText, NSAccessibility. Cleanest of the desktop three.
- **Linux third:** Ozone + GL, IBus/fcitx, fontconfig, AT-SPI. Most of the rendering core is already validated here from the 2026-06-12/13 Linux work.
- **Mobile last:** shells get heavier (touch, soft-keyboard IME, lifecycle, smaller a11y API surface); lowest priority — defer until the desktop three are solid.

Realistic shape: shared core + Windows shell = first milestone (the visual/interactive B-lite on the VM); Mac and Linux are incremental shells reusing the same core; mobile is a second phase.

## Open decisions for planning

1. Strategy (A) vs (B) — gated mostly by binary-size/dependency tolerance and how much of `ui/base/ime` we want to inherit vs own.
2. Layout engine: adopt `SkParagraph` as-is, or a custom layout on HarfBuzz for finer control over the a11y geometry coupling?
3. IME depth per platform — minimum viable (commit-only) vs full composition fidelity (ties to T2-4 verification on the VM).
4. How much of Chromium's `gpu`/`viz`/Ozone surface stack to pull vs a thin per-platform GL/Metal context + Skia backend.
