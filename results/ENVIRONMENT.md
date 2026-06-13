# Linux Generated-Event Test Environment

Date: 2026-06-12. This file records how Chromium 149.0.7827.115 source was obtained and built in the Claude Code cloud container, including every deviation from a stock checkout and why it was necessary. Results produced here are at the **AXEventGenerator (cross-platform) level**; the Windows UIA finalize layer still requires the TASK-00 VM.

## What the Linux run can and cannot prove

- CAN: which AXEventGenerator events fire for synthetic AXTreeUpdate deltas (T2-1, T2-2, T2-3, T2-5 at the generated-event level). Per docs/09 Finding 1, the Windows UIA event surface is driven by these generated events, so this is the load-bearing layer.
- CANNOT: final UIA event IDs, Text pattern behavior, ax_dump_events output, or anything Narrator-observable. Those remain on the Windows VM queue (TASK-00, T2-1..T2-7).

## Source acquisition

The container's network policy blocks `chromium.googlesource.com` (403 host_not_allowed) and the CIPD package host, ruling out `fetch`/`gclient` and the DEPS-pinned gn binary. Disk (~31 GB free) also rules out a full checkout. Instead:

- Source: official release tarball `chromium-149.0.7827.115.tar.xz` (5.76 GB) from commondatastorage.googleapis.com — generated from the exact release tag with all DEPS third_party vendored, no git history. Line numbers in docs/09 and docs/10 are valid against it.
- Partial extraction: the dependency cone of the test target plus all `.gn`/`.gni` files tree-wide (11,104 files; GN evaluates the whole-tree build graph even for one root target). Individual missing files (BRANDING/VERSION data files read by exec_script, helper scripts) were fetched from the chromium GitHub mirror at the same tag.
- Toolchain (all exactly DEPS-pinned, from allowed hosts): Chromium clang `llvmorg-23-init-10931-g20b6ec66-8`, Rust toolchain `rustc 1.96.0 (4c4205163a...)`, Debian bullseye amd64 sysroot. Compiler and sources are fully canonical.
- gn binary: Debian `generate-ninja 0.0~git20260220.2c775ed` (Feb 2026; the exact pin `1740f5c2` is unreachable through allowed hosts). gn only generates ninja files; any incompatibility fails loudly at gen time, not silently at test time.

## Local modifications ledger (complete)

Build-configuration only; zero changes to any `.cc`/`.h` under test. Each is in the scratch checkout, not upstream.

1. `.gn` dotfile: `root = "//ui/accessibility/t2"` (scopes GN to the test target's cone); `expand_directory_allowlist` line removed (dotfile feature postdating the gn binary).
2. `build/toolchain/gcc_toolchain.gni`: six `inputs = rustc_wrapper_inputs` lines commented (tool-inputs for rust postdates this gn; affects rebuild tracking only — irrelevant for a one-shot build).
3. `ui/accessibility/BUILD.gn`: new `ax_t2_minimal` GN arg gating ax_base's `//ui/base`+`//ui/strings`+l10n deps, mirroring the upstream `is_chromeos` branch of the same file; new `ax_t2_unittests` test target (T2 tests + existing `ax_event_generator_unittest.cc` baseline + mojo-free main); T2 test file registered in `accessibility_unittests` for future canonical runs.
4. `ui/accessibility/t2/BUILD.gn` (new): one-line group wrapping the test target, used as GN root.
5. `ui/accessibility/ax_event_generator_t2_unittest.cc`, `ax_t2_test_main.cc` (new): the T2 tests and test main.
6. Resolution-only assert shims (`assert(true || ...)`) in four BUILD.gn files: `ui/native_window_tracker`, `ui/wm`, `ui/wm/public`, `chrome/browser/background/extensions`. GN resolves every target defined in any loaded file, so sibling targets of our deps (e.g. gfx_unittests → //ui/base → views) resolve even though they are never built; these four asserts assume aura/extensions configs we don't use. Verified post-gen via `gn desc`: none of the shimmed directories appear in the ninja build cone of the test target.

## Build configuration (out/rel/args.gn)

`is_debug=false`, `is_component_build=true`, `symbol_level=0`, `dcheck_always_on=true` (DCHECKs catch malformed tree updates in tests), `use_remoteexec=false`, `use_siso=false`, `use_aura=false` (keeps //ui/aura out of the accessibility component itself), `use_glib=true` (from sysroot, as on upstream CI), `ozone_auto_platforms=false`, `ozone_platform="headless"`, `ozone_platform_x11=false` (no display in container; prunes x11→remoting edge), `ax_t2_minimal=true` (see ledger #3). Everything else default.

## Build (completed 2026-06-12)

- Target: `ax_t2_unittests` (see ledger #3) — 5,179 ninja steps end to end on 4 cores, ~3.5 h wall including iteration; binary 1.6 MB (component build).
- Two additional environment items surfaced during compilation, both documented:
  7. `ui/gfx/native_ui_types.h`: 3-line `IS_LINUX` branch giving opaque-pointer stand-ins for NativeView/NativeWindow/NativeEvent (upstream has no Linux-without-aura branch; these plumbing types are never dereferenced in this configuration and are not accessibility code).
  8. Data stand-ins: `third_party/test_fonts/*` zero-byte placeholders (runtime data for font-rendering tests we never run); `gpu/webgpu/DAWN_VERSION` written from the DEPS-pinned dawn revision (c1179de12ec3...); `enable_skia_graphite=false` added to args (prunes the dawn GPU backend from skia).
- Result: ALL 7 T2 tests pass; upstream `AXEventGeneratorTest` baseline 87/87 passes in the same binary. Full event logs: results/T2-linux-generated-events.md. Test sources + BUILD patch + args preserved under tests/linux-t2/.


## B-lite host (lean spine) — added 2026-06-13

A standalone executable `blite_host` (preserved in repo `blite/`) runs the producer spine — MockCanvasEditor -> Bridge -> AXTree -> AXEventGenerator — as a real program (run capture: results/blite-host-run.txt). It depends only on `//ui/accessibility:accessibility_internal` + `//base`: the same lean cone as the T2 tests, no platform layer, no v8.

Decision (2026-06-13): the platform-node layer (AXPlatformNode -> UIA on Windows / AT-SPI via use_atk on Linux) and all screen-reader work are HELD FOR THE WINDOWS VM, not built on Linux. Rationale: materializing AXPlatformNode leaves the lean cone and pulls a large build (v8 via gin under enable_extensions; aura/views/glib for AT-SPI), and UIA — the project's actual target — is Windows-only. Linux scope is therefore: 104 passing generated-event tests + this lean spine.

Build dead-ends explored and reverted (recorded so they are not re-attempted): depending on `//ui/accessibility:test_support` pulls the full platform target, which drags `services/accessibility` automation -> gin -> v8 under enable_extensions=true; setting enable_extensions=false instead fails gn gen on chrome/browser + chrome/test parse asserts. Both paths reverted; args.gn and chrome/test/BUILD.gn are back to the lean/pristine state. The `services/accessibility` subtree was extracted during this exploration and left in place (harmless, unused by the lean target).
