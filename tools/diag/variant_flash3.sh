#!/bin/sh
# Flash A/B, protocol v3: as variant_flash2.sh (log-driven boot, pan-pan-walk) but
# the score is taken during a slow continuous 360-degree pan of 70 s, so the count
# covers the trees in every direction instead of one view that varies run to run.
MY="/c/Users/renoi/AppData/Local/Temp/claude/C--users-renoi-claudecode/7d093996-252d-49c9-a593-f1ba6e716609/scratchpad"
BD="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/fable2recomp/out/build/win-amd64-Release"
LOG="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/fable2recomp/out/fable2.log"
pad="$BD/pad_script.txt"
waitfor() { i=0; while [ $i -lt "$2" ]; do grep -q "$1" "$LOG" 2>/dev/null && return 0; sleep 1; i=$((i+1)); done; return 1; }
for spec in "$@"; do
  tag="${spec%%|*}"; tune="${spec#*|}"
  powershell -NoProfile -Command "Get-Process fable2 -ErrorAction SilentlyContinue | ForEach-Object { \$null = \$_.CloseMainWindow() }; for (\$i = 0; \$i -lt 40; \$i++) { if (-not (Get-Process fable2 -ErrorAction SilentlyContinue)) { break }; Start-Sleep -Milliseconds 500 }" > /dev/null
  sleep 3; rm -f "$pad"
  powershell -NoProfile -File "$MY/launch_hero.ps1" -Pad "" -Tune "$tune" > /dev/null
  echo "=== $tag  ($tune)"
  waitfor "scene none -> menu" 60 || { echo "no title camera seen"; continue; }
  sleep 4
  printf 'a:0.2\nwait:3\ndown:0.2\nwait:0.8\na:0.2\nwait:3\na:0.2\n' > "$pad"
  waitfor "region 'bowerlake' is loading" 30 || { printf 'a:0.2\n' > "$pad"; waitfor "region 'bowerlake' is loading" 30 || { echo "world never loaded"; continue; }; }
  sleep 22
  printf 'r:-0.7,0:1.6\n' > "$pad"; sleep 7
  printf 'r:0.7,0:1.6\nwait:0.5\nl:0,1:8\n' > "$pad"; sleep 13
  printf 'r:0.22,0:72\n' > "$pad"; sleep 1
  grep -c "Tuning override (FABLE2_TUNE)" "$LOG" | sed 's/^/tune lines: /'
  python "$MY/flash_probe.py" "$MY/flash_$tag" 70 | grep "captured\|FLAGGED"
  grep "fence waits" "$LOG" | tail -1 | sed 's/.*\[gpu\] //' | grep -o "resolve readback [^,]*\|submissions [^(]*\|[0-9]* mirror copies\|superseded[^,]*" | tr '\n' ';'; echo
  grep "guest fps" "$LOG" | tail -3 | sed 's/.*\[swap\] //' | cut -c1-5 | tr '\n' ' '; echo
done
