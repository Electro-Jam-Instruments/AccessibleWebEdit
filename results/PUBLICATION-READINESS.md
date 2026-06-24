# Publication Readiness — audit & checklist

Date: 2026-06-16. A deep-dive audit of whether this repo is in a good place to open up (make public), what was fixed, and the decisions that remain **yours** (they're not engineering tasks).

## Audit results (what I checked)

| Check | Result |
|---|---|
| **Secrets / credentials committed** | **None.** Every `password` / `PAT` / `token` reference is a placeholder (`<STRONG-PASSWORD>`, `<FINE_GRAINED_PAT>`) or a script parameter / env var (`$ADMIN_PASSWORD`, `$GitHubPat`). No real keys, tokens, or `.pat` files are tracked. |
| **Model-identity / session leaks** | **None.** No `claude-*` model IDs, no `claude.ai/code/session` URLs in files or commit messages. (Commit history has no session trailers — good for a public repo.) |
| **PII** | Only the repo's own org URL (`Electro-Jam/AccessibleWebEdit`) and one fabricated build-bot email, which was genericized to `…@users.noreply.github.com`. No personal data. |
| **Vendored third-party code** | **None.** Chromium is referenced by tag + file:line only; no `chromium/`, `out/`, or build artifacts are tracked (verified via `git ls-files`). |
| **Internal/confidential markers** | None (the only `TODO/XXX` grep hit was the literal string `ariaXXX`). |
| **Overclaiming / accuracy** | Addressed in the 2026-06-16 expert-review pass: "PROVEN/SETTLED/permanently capped" softened to "event-layer validated; fidelity pending VM"; autocorrect/IME re-graded; §1.1 corrected and re-proven. |

## Fixed this session (publication essentials)

- **`LICENSE`** — BSD 3-Clause added. This was *required*: the `.cc`/`.gn` files carry the Chromium header "governed by a BSD-style license that can be found in the LICENSE file," which had no LICENSE to point at. BSD-3-Clause also keeps the repo license-compatible with Chromium, from which the tests/spine/patches derive.
- **`NOTICE.md`** — attribution: which files are Chromium-derived (and why they carry "The Chromium Authors" headers), that Chromium isn't vendored, and the AI-assisted research-status note.
- **`README.md`** — rewritten from a one-line stub into a real front door: the question, an honest status banner (research, event-layer-validated, fidelity pending), navigation to `docs/00-INDEX.md`, reproduce steps, repo layout, license/attribution.
- **`.gitignore`** — blocks secrets (`*.pat`, `.git-credentials`, `*.env`), build output (`out/`, `chromium/`), and OS/editor cruft — so the bootstrap scripts' local `github.pat` can never be committed.
- **Bot email genericized** in `docs/TASK-00`.

## Decisions that are yours (not engineering — confirm before going public)

1. **License choice + copyright holder.** I added **BSD-3-Clause** with holder "The AccessibleWebEdit Authors" — the right default given Chromium derivation and the existing file headers. Confirm the license and the legal holder name (e.g., your name, or "Electro-Jam") before publishing; change the LICENSE copyright line if needed.
2. **Branch → default.** Everything is on `claude/chat-access-question-l5g1z7`. Publishing normally wants this content on `main` as the default branch. I have **not** merged to main (no permission to push elsewhere). Decide: open a PR and merge, or fast-forward `main`. Say the word and I'll prepare the PR.
3. **Make the repo public.** That's a GitHub setting (and an org-policy decision) — your action, not something I can or should do.
4. **The honesty framing is load-bearing for going public.** The README and `00-INDEX` now lead with "research / not verified end-to-end." Keep that prominent — the strongest reputational risk for an open accessibility repo is over-claiming "real editing works for blind users" when the screen-reader run hasn't happened. The expert-review docs (kept in-repo) are an asset here: they show the claims were stress-tested.
5. **Optional governance files** — `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md` are absent. Not blockers, but conventional for a public repo; add if you expect outside contributors. I can draft them.

## Recommended pre-publish sequence

1. Confirm license + holder (decision 1) → I adjust the LICENSE line.
2. Optionally add governance files (decision 5).
3. Open a PR from `claude/chat-access-question-l5g1z7` → `main`, review the diff, merge (decision 2).
4. Flip the repo to public (decision 3).

Engineering-wise the repo is clean and self-consistent (109/109 tests, honest evidence tags, full provenance). The remaining items are licensing confirmation, branch/merge, and the GitHub visibility flip — all yours to authorize.
