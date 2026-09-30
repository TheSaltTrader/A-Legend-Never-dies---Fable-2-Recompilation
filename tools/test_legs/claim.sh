#!/bin/sh
# claim.sh MINUTES NOTE: wait until the machine is free, then write this session's claim into the game lock.
# claim.sh release NOTE: clear this session's own claim (never another session's line).
#
# The lock (~/.game-test-lock) is ADVISORY between sessions; the process list is the truth. A claim is honoured only
# while the game it names is alive: 90 s with no fable2.exe / ng2.exe process while the lock still reads busy means a
# stale line left by a session that stopped without clearing it. 2026-09-29: the first liveness fix broke out of the
# inner wait and a second guard re-imposed the text test, so a release leg sat 24 minutes on the NG2 session's stale
# line (found by the supervisor). Liveness now lives INSIDE free(), so every path agrees, and a replaced stale line is
# logged. Never overwrites a live claim of another session (2026-09-27: a blind printf clobbered NG2's); re-reads its
# own claim after writing it (2026-09-27 10:11: NG2's runner wrote over ours seconds later and both games ran).
LOCK="$HOME/.game-test-lock"
SESSION="claudecode-4c"   # the name this session's legs claim under (the session messages as claudecode-bd; both stay in the line)
idle=0
game_running() { tasklist | grep -qi "fable2.exe\|ng2.exe"; }
lock_free_text() { grep -q "machine-free\|game=none" "$LOCK" 2>/dev/null || [ ! -f "$LOCK" ]; }
free() {
  if game_running; then idle=0; return 1; fi
  if lock_free_text; then return 0; fi
  [ "$idle" -ge 90 ] && return 0   # busy text, no game for 90 s: stale
  return 1
}
if [ "$1" = "release" ]; then
  grep -q "session=$SESSION" "$LOCK" 2>/dev/null && printf 'session=%s (claudecode-bd) game=none until=now note=machine-free (%s)\n' "$SESSION" "$2" > "$LOCK"
  exit 0
fi
i=0
while :; do
  until free; do
    if [ $((i % 120)) -eq 0 ]; then echo "claim: waiting $(date +%T) - lock: $(cat "$LOCK" 2>/dev/null) - game: $(tasklist | grep -i "fable2.exe\|ng2.exe" | awk '{print $1, $2}' | tr '\n' ' ')"; fi
    sleep 15; i=$((i+15)); idle=$((idle+15))
    game_running && idle=0
    [ $i -gt 3600 ] && { echo "claim: lock still held after 60 min: $(cat "$LOCK" 2>/dev/null)"; exit 1; }
  done
  sleep 20
  free || continue
  if ! lock_free_text; then echo "claim: replacing stale claim (no game process for ${idle}s): $(cat "$LOCK" 2>/dev/null)"; fi
  printf 'session=%s (claudecode-bd) game=fable2 until=%s note=reserved for 4c (%s)\n' "$SESSION" "$(date -d "+$1 minutes" +%H:%M)" "$2" > "$LOCK"
  # Re-read after writing: someone else's line, or a game that appeared meanwhile, means we lost the race - launch
  # nothing, wait again.
  sleep 3
  if grep -q "session=$SESSION" "$LOCK" && ! game_running; then break; fi
  echo "claim: lost the race to $(cat "$LOCK") - waiting again"
done
echo "claimed: $(cat "$LOCK")"
