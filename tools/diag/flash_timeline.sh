#!/bin/sh
# When does the tree start flashing after a load? Close any game, launch hero 1
# with the tune, then score the tree region for 8 s every ~20 s until T seconds.
#   flash_timeline.sh <tag> "<tune>" [T]
tag="$1"; tune="$2"; T="${3:-240}"
MY="/c/Users/renoi/AppData/Local/Temp/claude/C--users-renoi-claudecode/7d093996-252d-49c9-a593-f1ba6e716609/scratchpad"
powershell -NoProfile -Command "Get-Process fable2 -ErrorAction SilentlyContinue | ForEach-Object { \$null = \$_.CloseMainWindow() }; for (\$i = 0; \$i -lt 40; \$i++) { if (-not (Get-Process fable2 -ErrorAction SilentlyContinue)) { break }; Start-Sleep -Milliseconds 500 }" > /dev/null
sleep 3
powershell -NoProfile -File "$MY/launch_hero.ps1" -Pad "14:a,19:down,21:a,25:a" -Tune "$tune" > /dev/null
t0=$(date +%s)
sleep 55
echo "=== $tag ($tune)"
while :; do
  now=$(( $(date +%s) - t0 ))
  [ $now -ge $T ] && break
  printf 't=%3ds  ' "$now"
  python "$MY/flash_region.py" 1580 400 160 240 8 | head -1 | sed 's/frames [0-9]* in [0-9.]*s ([0-9]* fps): //'
  sleep 12
done
