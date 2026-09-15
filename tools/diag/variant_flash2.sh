#!/bin/sh
# Flash A/B at the reproducing spot. Per "tag|tune": close any game, launch with the
# tune (no timed boot script), drive the menus from the LOG's own state lines
# (title camera -> A, DOWN, A = Continue, A = left slot; retry A if the world does
# not start loading), wait for the world, walk 8 s up the path, score 25 s.
MY="/c/Users/renoi/AppData/Local/Temp/claude/C--users-renoi-claudecode/7d093996-252d-49c9-a593-f1ba6e716609/scratchpad"
BD="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/fable2recomp/out/build/win-amd64-Release"
LOG="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/fable2recomp/out/fable2.log"
pad="$BD/pad_script.txt"
waitfor() {  # waitfor <pattern> <max_s>
  i=0; while [ $i -lt "$2" ]; do grep -q "$1" "$LOG" 2>/dev/null && return 0; sleep 1; i=$((i+1)); done; return 1
}
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
  # the sequence that reproduces it: pan left, pan back, walk 8 s up the path
  printf 'r:-0.7,0:1.6\n' > "$pad"; sleep 7
  printf 'r:0.7,0:1.6\nwait:0.5\nl:0,1:8\n' > "$pad"; sleep 13
  grep -c "Tuning override (FABLE2_TUNE)" "$LOG" | sed 's/^/tune lines: /'
  python "$MY/flash_probe.py" "$MY/flash_$tag" 25 | grep "captured\|FLAGGED"
  grep "fence waits" "$LOG" | tail -1 | sed 's/.*\[gpu\] //' | grep -o "resolve readback [^,]*\|submissions [^(]*\|[0-9]* mirror copies" | tr '\n' ';'; echo
done
