#!/bin/sh
# uwpanload.sh TAG TUNE STANDSECS BURSTSECS SCALE: uwpanarr.sh plus a DELIBERATE CPU LOAD during the loading screen (prediction 2, 2026-09-29 14:43): from the travel's "[cam] loading map" line until 3 s after "scene loading -> world", four busy Python processes spin; NO screen capture during the load; the capture opens at the world entry as in uwpanarr.sh. Tests whether arming is a generic load-phase perturbation (not the split, not the recorder). Was: uwpanarr.sh TAG TUNE STANDSECS BURSTSECS SCALE: uwpans.sh with the capture opened AT THE WORLD ENTRY (the log's next "scene loading -> world" after the travel is issued) instead of ~36 s later - covers arrival +0..+BURSTSECS s WITHOUT capturing during the loading screen (the two arrival legs that captured through the load, OPTB_REV30_1 and SPLITOFF_REV_1, were both dirty; a capture during the load is itself a CPU/disk load on a timing-dependent defect; 2026-09-29 14:32). Was: uwpans.sh TAG TUNE STANDSECS BURSTSECS SCALE (a copy of uwpanx.sh with the internal scale as $5, default 3; verify by the "internal scale NxN" line). Was: uwpan.sh TAG TUNE: STATIONARY - pan only, no walking (arms at different fps see the same scene). was uwarm.sh TAG TUNE: one arm per session (FABLE2_TUNE, '-' = default), no live switches. was uwoods.sh TAG VAR: the user's settings (pack on, dd 150, fov 60, 3x/720), Bower Lake woods, walk + pan; live A/B of
# cvar VAR in one session: 60 s default, 60 s VAR=false, 60 s VAR=true. One 190 s burst; phases split by pad-set times.
T="$1"; TUNE="$2"; [ "$TUNE" = "-" ] && TUNE=""; L=/d/fable2_flash/ab110/logs; P=/d/fable2_flash/ab110/R113/pad_script.txt
tasklist | grep -qi "fable2.exe\|ng2.exe" && { echo "$T REFUSED: a game is running"; exit 1; }
rm -f $T.out $L/$T.log $L/$T.stand_start
(sh claim.sh 10 "woods A/B $T" >/dev/null && FABLE2_TUNE="$TUNE" FABLE2_WORLD_HEIGHT=720 FABLE2_SCALE=${5:-3} PAD_ROUTE="wait:1" sh arm_leg_tune.sh R113 $T 2 fairfax ${3:-480} > $T.out 2>&1; sh claim.sh release $T) > /dev/null 2>&1 &
for k in $(seq 1 4000); do [ -f $L/$T.stand_start ] && break; grep -q ABANDON $T.out 2>/dev/null && break; sleep 1; done
[ -f $L/$T.stand_start ] || { echo "$T VOID"; wait; exit 1; }
sleep 3
W0=$(grep -ac "scene loading -> world" $L/$T.log)
M0=$(grep -ac "\[cam\] loading map" $L/$T.log)
( for k in $(seq 1 120); do [ "$(grep -ac "\[cam\] loading map" $L/$T.log)" -gt "$M0" ] && break; sleep 0.5; done
  echo "$T CPU LOAD ON at the loading map $(date +%H:%M:%S.%N | cut -c1-12)"
  for c in 1 2 3 4; do python -c "import time
t=time.time()
while time.time()-t<90: pass" & done
  for k in $(seq 1 240); do [ "$(grep -ac "scene loading -> world" $L/$T.log)" -gt "$W0" ] && break; sleep 0.5; done; sleep 3
  pkill -f "while time.time" 2>/dev/null; taskkill //IM python.exe //F >/dev/null 2>&1
  echo "$T CPU LOAD OFF $(date +%H:%M:%S.%N | cut -c1-12)" ) &
( for k in $(seq 1 120); do [ "$(grep -ac "scene loading -> world" $L/$T.log)" -gt "$W0" ] && break; sleep 0.5; done
  echo "$T capture opens at world entry $(date +%H:%M:%S.%N | cut -c1-12)"
  powershell -NoProfile -ExecutionPolicy Bypass -File burst.ps1 "D:/fable2_flash/ab110/logs/sweep/${T}_burst" ${4:-60} > /dev/null ) &
BP=$!
echo "$T travel: $(sh area.sh R113 $T 1 0 | cut -c1-70)"
W=""; k=0; while [ $k -lt 22 ]; do W="${W}r:0.6,0:2
r:-0.6,0:2
r:0,0.4:0.8
r:0,-0.4:0.8
"; k=$((k+1)); done
printf 'release
%s%s%s' "$W" "$W" "$W" > $P; sleep 2
wait $BP
PID=$(grep -o "pid [0-9]*" $T.out | head -1 | cut -d' ' -f2); [ -n "$PID" ] && taskkill //PID $PID //F > /dev/null 2>&1
wait
echo "$T overrides (0 = none, default arm): $(grep -ac 'Tuning override (FABLE2_TUNE)' $L/$T.log) | $(grep -a 'Tuning override' $L/$T.log | cut -c60-150 | tr '
' ';')"
