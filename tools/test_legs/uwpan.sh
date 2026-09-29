#!/bin/sh
# uwpan.sh TAG TUNE: STATIONARY - pan only, no walking (arms at different fps see the same scene). was uwarm.sh TAG TUNE: one arm per session (FABLE2_TUNE, '-' = default), no live switches. was uwoods.sh TAG VAR: the user's settings (pack on, dd 150, fov 60, 3x/720), Bower Lake woods, walk + pan; live A/B of
# cvar VAR in one session: 60 s default, 60 s VAR=false, 60 s VAR=true. One 190 s burst; phases split by pad-set times.
T="$1"; TUNE="$2"; [ "$TUNE" = "-" ] && TUNE=""; L=/d/fable2_flash/ab110/logs; P=/d/fable2_flash/ab110/R113/pad_script.txt
tasklist | grep -qi "fable2.exe\|ng2.exe" && { echo "$T REFUSED: a game is running"; exit 1; }
rm -f $T.out $L/$T.log $L/$T.stand_start
(sh claim.sh 10 "woods A/B $T" >/dev/null && FABLE2_TUNE="$TUNE" FABLE2_WORLD_HEIGHT=720 FABLE2_SCALE=3 PAD_ROUTE="wait:1" sh arm_leg_tune.sh R113 $T 2 fairfax 480 > $T.out 2>&1; sh claim.sh release $T) > /dev/null 2>&1 &
for k in $(seq 1 4000); do [ -f $L/$T.stand_start ] && break; grep -q ABANDON $T.out 2>/dev/null && break; sleep 1; done
[ -f $L/$T.stand_start ] || { echo "$T VOID"; wait; exit 1; }
sleep 3; echo "$T travel: $(sh area.sh R113 $T 1 0 | cut -c1-70)"
W=""; k=0; while [ $k -lt 22 ]; do W="${W}r:0.6,0:2
r:-0.6,0:2
r:0,0.4:0.8
r:0,-0.4:0.8
"; k=$((k+1)); done
printf 'release
%s%s%s' "$W" "$W" "$W" > $P; sleep 2
powershell -NoProfile -ExecutionPolicy Bypass -File burst.ps1 "D:/fable2_flash/ab110/logs/sweep/${T}_burst" 200 > /dev/null
PID=$(grep -o "pid [0-9]*" $T.out | head -1 | cut -d' ' -f2); [ -n "$PID" ] && taskkill //PID $PID //F > /dev/null 2>&1
wait
echo "$T overrides (0 = none, default arm): $(grep -ac 'Tuning override (FABLE2_TUNE)' $L/$T.log) | $(grep -a 'Tuning override' $L/$T.log | cut -c60-150 | tr '
' ';')"
