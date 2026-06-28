#!/usr/bin/env bash
# Stop hook for the AccessibleWebEdit autonomous work loop.
# Blocks the agent from stopping while there is buildable work toward the
# prototype, with four safety rails. Outputs JSON on stdout per the Claude Code
# Stop-hook contract: {"decision":"block","reason":...} keeps working; empty/exit-0 allows stop.
#
# Rails (any one => allow stop):
#   1. kill-switch        scripts/STOP-LOOP exists
#   2. completion         results/PROTOTYPE-COMPLETE exists
#   3. don't-spin         scripts/chromium-fetch.done ABSENT (fetch still downloading)
#   4. runaway cap        scripts/.loop-count > 40 (then reset)

ROOT="${AWE_ROOT:-C:/Dev/Projects/45 - AccWebEdit}"
SCRIPTS="$ROOT/scripts"
RESULTS="$ROOT/results"

# Drain stdin (hook receives JSON) so the pipe never blocks.
cat >/dev/null 2>&1 || true

# 1. kill-switch
[ -f "$SCRIPTS/STOP-LOOP" ] && exit 0

# 2. completion sentinel
[ -f "$RESULTS/PROTOTYPE-COMPLETE" ] && exit 0

# 3. fetch still running -> allow stop; harness re-invokes on background completion
[ ! -f "$SCRIPTS/chromium-fetch.done" ] && exit 0

# 3b. genuinely waiting on a tracked background job (e.g. a long build) -> allow
# stop; the harness re-invokes on the job's completion (or a fallback wakeup).
# The continuation removes this marker as soon as it has real work to do.
[ -f "$SCRIPTS/.awaiting-build" ] && exit 0

# 4. runaway cap
CFILE="$SCRIPTS/.loop-count"
count=0
[ -f "$CFILE" ] && count=$(tr -dc '0-9' < "$CFILE" 2>/dev/null)
[ -z "$count" ] && count=0
if [ "$count" -ge 40 ]; then
  echo 0 > "$CFILE"
  echo '{"systemMessage":"AccessibleWebEdit work-loop cap (40 iterations) reached - allowing stop and resetting the counter. Delete scripts/.loop-count to re-arm."}'
  exit 0
fi
count=$((count + 1))
echo "$count" > "$CFILE"

# Block: keep working on the next actionable task.
cat <<'JSON'
{"decision":"block","reason":"AUTONOMOUS WORK LOOP ACTIVE (AccessibleWebEdit -- EDITING model). FIRST LINE of your reply must be a one-sentence summary (screen-reader accessibility). The DESIGN + locked decisions D1-D5 are in docs/research/18-caret-selection-and-table-editing-model.md -- follow it. Read docs/TODOs/MASTER-TASKLIST.md '★ ACTIVE ROADMAP' and pick the NEXT unchecked sub-step, Phases E1->E7 top-down (E1 text caret inside a cell is foundational; do it first). Selection rule: same-cell => TEXT selection (AXTreeData anchor/focus -> ITextRangeProvider); cross-cell => CELL block (per-cell kSelected -> ISelectionProvider2); drive exactly one and fire the matching event. Work each phase the CORRECT WAY: (a) build producer infra (editor Pos/anchor model + Bridge + AX nodes in blite/blite_host_win.cc), (b) verify with a native UIA probe (scripts/uiaprobe*.cpp; add one if needed -- caret-position / GetSelection range / cell GetSelection), (c) verify with NVDA by DRIVING KEYSTROKES (launch nvda -m -c C:/awe/nvda-config --log-level=12 --log-file, wait 30s, launch host --viewer, PostMessage WM_KEYDOWN arrows/Shift to the hwnd like commit 12e0ac8, grep the log for utterances, then nvda -q), (d) add the coupled visual (selection highlight / caret in cell / cell-block from the one LayOut). Keep the single layout pass and the deferred-a11y-off-input pattern (RequestSync/FlushPendingSync). Build: sync blite/blite_host_win.cc to C:/src/chromium/src/ui/accessibility/blite/ then run scripts/_blite-link.ps1 in background; kill any running blite_host_win.exe first. Commit each verified step with evidence and tick the task list. Do NOT stop until results/PROTOTYPE-COMPLETE exists (recreate it when E1-E7 are all PROVEN). If genuinely blocked on a tracked background build, write scripts/.awaiting-build and stop (harness re-invokes on completion). To halt entirely, create scripts/STOP-LOOP."}
JSON
exit 0
