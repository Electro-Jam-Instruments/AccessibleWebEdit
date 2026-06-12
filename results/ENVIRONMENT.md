# Linux Generated-Event Test Environment

Date: 2026-06-12. This file records how the Chromium 149.0.7827.115 source was obtained and built in the Claude Code cloud container, including deviations from the planned `gclient` flow and why they were necessary. Results produced here are at the **AXEventGenerator (cross-platform) level**; the Windows UIA finalize layer (BrowserAccessibilityManagerWin → UIA_Text_TextChangedEventId etc.) still requires the TASK-00 VM.

## What the Linux run can and cannot prove

- CAN: which AXEventGenerator events fire for synthetic AXTreeUpdate deltas (T2-1, T2-2, T2-3, T2-5 at the generated-event level). Per docs/09 Finding 1, the Windows UIA event surface is driven by these generated events, so this is the load-bearing layer.
- CANNOT: the final UIA event IDs, Text pattern behavior, ax_dump_events output, or anything Narrator-observable. Those remain on the Windows VM work queue.

## Source acquisition (deviation from plan)

The container's network policy blocks `chromium.googlesource.com` (HTTP 403 host_not_allowed), which rules out `fetch`/`gclient`. The disk budget (~31 GB free) also rules out a full sync. Instead:

- Source: official release tarball `https://commondatastorage.googleapis.com/chromium-browser-official/chromium-149.0.7827.115.tar.xz` (5.76 GB). This artifact is generated from the exact release tag with all DEPS-pinned third_party vendored, no git history — it satisfies the "minimal no-history checkout pinned to 149.0.7827.115" requirement; line numbers in docs/09 and docs/10 are valid against it.
- Partial extraction: only the dependency cone of `//ui/accessibility:accessibility_unittests` was extracted (base, build, buildtools, mojo, testing, tools, ui, url, and ~25 third_party libs). blink, v8, chrome, content were never extracted.
- GN scoping: the `.gn` dotfile's `root` was pointed at `//ui/accessibility` so GN only loads the extracted subgraph. GN binary is Ubuntu noble's `generate-ninja` (version 1000, 03d10f1) because the CIPD host for the pinned gn is also blocked.

## Toolchain

Recorded after the build (see below): clang, sysroot, gn args.

## Build

(filled in after the build completes)
