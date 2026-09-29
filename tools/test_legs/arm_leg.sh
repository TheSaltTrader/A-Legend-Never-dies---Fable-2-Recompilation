#!/bin/sh
# arm_leg.sh ARM TAG SAVE REGION [SECS]: one stand leg of arm N/P/I (D:\fable2_flash\ab110\<ARM>), loading save card SAVE
# from a FRESH copy of the user's saves (user_snapshot -> user_<TAG>), asserting the loaded region starts with REGION,
# standing SECS s (default 75), then closing ONLY its own PID. No FABLE2_TUNE: each arm runs its own settings file.
set -u
ARM="$1"; TAG="$2"; SAVE="$3"; REGION="$4"; SECS="${5:-75}"
R=/d/fable2_flash/ab110; RW='D:\fable2_flash\ab110'
D="$R/$ARM"; DW="$RW\\$ARM"
T="$(cd "$(dirname "$0")" && pwd)"   # the folder these scripts live in (was the session job folder)
LOG="$R/logs/$TAG.log"; LOGW="$RW\\logs\\$TAG.log"
say() { echo "[$TAG $(date +%H:%M:%S)] $*"; }
waitfor() { i=0; while [ $i -lt "$3" ]; do grep -a -q "$1" "$2" 2>/dev/null && return 0; sleep 1; i=$((i+1)); done; return 1; }
tasklist | grep -qi "fable2.exe\|ng2.exe" && { say "ABORT: a game is running"; exit 2; }
grep -q "session=claudecode-4c.*reserved for 4c" ~/.game-test-lock || { say "ABORT: the lock is not ours (claim.sh first): $(cat ~/.game-test-lock)"; exit 2; }
mkdir -p "$R/logs"; rm -f "$LOG" "$D/pad_script.txt"
rm -rf "$R/user_$TAG"; cp -r "$R/user_snapshot" "$R/user_$TAG"
unset FABLE2_TUNE FABLE2_PAD_SCRIPT FABLE2_HUD
PID=$(powershell -NoProfile -Command "\$r = Start-Process -FilePath '$DW\fable2.exe' -ArgumentList @('--user_data_root=\"$RW\user_$TAG\"', '--log_file', '\"$LOGW\"', '--log_level', 'info', '--log_flush_interval', '1') -WorkingDirectory '$DW' -PassThru; \$r.Id" | tr -d '\r')
case "$PID" in ''|*[!0-9]*) say "ABORT: launch failed"; exit 3;; esac
say "launched $ARM pid $PID save $SAVE"
kill_mine() {
  powershell -NoProfile -Command "\$p = Get-Process -Id $PID -ErrorAction SilentlyContinue; if (\$p) { \$null = \$p.CloseMainWindow(); for (\$i = 0; \$i -lt 20; \$i++) { if (\$p.HasExited) { break }; Start-Sleep -Milliseconds 500 }; if (-not \$p.HasExited) { Stop-Process -Id $PID -Force } }" > /dev/null 2>&1
  sleep 3
}
if ! waitfor "scene none -> menu" "$LOG" 120; then say "ABANDON: no menu"; kill_mine; exit 4; fi
sleep 4
# 2026-09-28: the menu sequence is timed; a press landing mid-animation left the cursor on a new-game card and the leg
# waited 4.5 min to fail (ABX2). Now: no "menu -> loading" within 25 s = back out with B and send the sequence again.
try=1
while :; do
  { printf 'a:0.2\nwait:3\ndown:0.2\nwait:0.8\na:0.2\nwait:3\n'; i=1; while [ $i -lt "$SAVE" ]; do printf 'right:0.2\nwait:0.6\n'; i=$((i+1)); done; printf 'a:0.2\n'; } > "$D/pad_script.txt"
  waitfor "scene menu -> loading" "$LOG" 25 && break
  [ $try -ge 3 ] && break
  say "no load after the menu sequence (try $try): backing out and retrying"
  printf 'b:0.2\nwait:1.5\nb:0.2\nwait:1.5\nb:0.2\nwait:2\n' > "$D/pad_script.txt"; sleep 7
  try=$((try+1))
done
if ! waitfor "scene loading -> world" "$LOG" 150; then
  printf 'a:0.2\n' > "$D/pad_script.txt"
  waitfor "scene loading -> world" "$LOG" 90 || { say "ABANDON: world never loaded"; kill_mine; exit 6; }
fi
if ! grep -a -q "region '$REGION[a-z_0-9]*' is loading" "$LOG"; then
  say "ABANDON: WRONG SCENE - loaded: $(grep -a -o "region '[a-z_0-9]*' is loading" "$LOG" | sort -u | tr '\n' ' ')"; kill_mine; exit 7
fi
say "world: $(grep -a -o "region '[a-z_0-9]*' is loading" "$LOG" | tail -1); standing $SECS s"
if [ -n "${PAD_ROUTE:-}" ]; then printf '%s\n' "$PAD_ROUTE" > "$D/pad_script.txt"; else printf 'wait:%s\n' "$SECS" > "$D/pad_script.txt"; fi
date +%H:%M:%S > "$R/logs/$TAG.stand_start"
sleep "$SECS"
kill_mine
tasklist | grep -q " $PID " && say "WARNING: pid $PID still running" || say "closed pid $PID"
rm -rf "$R/user_$TAG"
