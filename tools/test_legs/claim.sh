#!/bin/sh
# claim.sh MINUTES NOTE: wait until the game lock reads free AND no fable2.exe/ng2.exe runs (20 s settle, re-checked),
# then write a claudecode-4c claim. Never overwrites someone else's claim (2026-09-27: a blind printf clobbered NG2's).
# release: claim.sh release NOTE
LOCK="$HOME/.game-test-lock"
free() { grep -q "machine-free\|game=none" "$LOCK" && ! tasklist | grep -qi "fable2.exe\|ng2.exe"; }
if [ "$1" = "release" ]; then
  grep -q "session=claudecode-4c" "$LOCK" && printf 'session=claudecode-4c game=none until=now note=machine-free (%s)\n' "$2" > "$LOCK"
  exit 0
fi
i=0
while :; do
  until free; do sleep 15; i=$((i+15)); [ $i -gt 3600 ] && { echo "claim: lock still held after 60 min: $(cat "$LOCK")"; exit 1; }; done
  sleep 20
  free || continue
  printf 'session=claudecode-4c game=fable2 until=%s note=reserved for 4c (%s)\n' "$(date -d "+$1 minutes" +%H:%M)" "$2" > "$LOCK"
  # Re-read after writing (2026-09-27 10:11: NG2's runner wrote its claim over ours seconds later and both games ran).
  # Someone else's line, or a game that appeared meanwhile, means we lost the race: launch nothing, wait again.
  sleep 3
  if grep -q "session=claudecode-4c" "$LOCK" && ! tasklist | grep -qi "fable2.exe\|ng2.exe"; then break; fi
  echo "claim: lost the race to $(cat "$LOCK") - waiting again"
done
echo "claimed: $(cat "$LOCK")"
