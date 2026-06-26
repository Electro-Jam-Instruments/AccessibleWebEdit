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
{"decision":"block","reason":"AUTONOMOUS WORK LOOP ACTIVE (AccessibleWebEdit prototype). The Chromium fetch is complete, so there is buildable work. FIRST LINE of your reply must be a one-sentence summary (screen-reader accessibility). Then: read docs/TODOs/MASTER-TASKLIST.md and CLAUDE.md, pick the NEXT unchecked actionable task (Phase 1 baseline build -> Phase 2 V1 NVDA slice), do it, and update the task list with evidence. Do NOT stop until results/PROTOTYPE-COMPLETE exists. If you are genuinely blocked waiting on a tracked background job, say so briefly and stop (the harness will re-invoke you on completion). To halt this loop entirely, create the file scripts/STOP-LOOP."}
JSON
exit 0
