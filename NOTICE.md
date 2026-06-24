# NOTICE — third-party material and attribution

This repository is licensed under BSD 3-Clause (see `LICENSE`), chosen deliberately to be compatible with, and consistent with, the Chromium project from which parts of this work derive.

## Chromium-derived files

The following are written to follow Chromium conventions and are intended to compile against, or be applied to, the Chromium source tree (pinned tag `149.0.7827.115`). They carry the standard Chromium copyright header (`// Copyright 2026 The Chromium Authors`) and are governed by the BSD-style license in `LICENSE`:

- `tests/linux-t2/*.cc`, `tests/linux-t2/*.gn`, `tests/linux-t2/*.patch` — generated-event unit tests + the build registration patch, designed to drop into `//ui/accessibility`.
- `blite/*.cc`, `blite/*.gn` — the B-lite host spine, written against `//ui/accessibility`.
- `patches/*.md` — prototype patch descriptions that **quote** small excerpts of Chromium source (with file:line citations) and propose diffs against it.

Chromium itself is **not** vendored in this repository. The patches and tests reference Chromium by tag and file:line; to build or apply them you supply your own Chromium checkout (see `docs/TASK-00b-local-windows-setup.md` / `docs/TASK-00-vm-bootstrap.md`). Chromium is © The Chromium Authors, BSD 3-Clause: https://chromium.googlesource.com/chromium/src/+/main/LICENSE

## Original analysis

The `docs/` and `results/` directories (the research, the editing-scenario matrix, the bridge contract, the cross-platform / AOM analysis, and the expert-review reports) are original analysis produced for this project, under the same BSD 3-Clause license.

## Authorship note

This repository's documentation, tests, and prototypes were produced with substantial AI assistance and are research/exploration artifacts — see the status disclaimer in `README.md`. Claims are tagged by evidence level (PROVEN / SOURCE / VM) throughout; the central product claim (screen-reader-observable editing fidelity) is **not yet end-to-end verified** — see `results/expert-reviews/REVIEW-BACKLOG.md`.
