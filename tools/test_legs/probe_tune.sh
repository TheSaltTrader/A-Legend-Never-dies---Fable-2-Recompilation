#!/bin/sh
# probe_res.sh TAG WORLD_HEIGHT SCALE: Fairfax stand at an internal resolution; shots of gameplay (+20 s) and the
# pause menu (+31 s); then the log lines that say what size was applied.
T="$1"; R=/d/fable2_flash/ab110/R113; L=/d/fable2_flash/ab110/logs
rm -f $T.out $L/$T.log $L/$T.stand_start
(sh claim.sh 6 "internal res probe $T" >/dev/null && FABLE2_TUNE="$4" FABLE2_WORLD_HEIGHT="$2" FABLE2_SCALE="$3" PAD_ROUTE="wait:27
start:0.2
wait:8
b:0.2
wait:10" sh arm_leg_tune.sh R113 $T 2 fairfax 55 > $T.out 2>&1; sh claim.sh release $T) > /dev/null 2>&1 &
for k in $(seq 1 4000); do [ -f $L/$T.stand_start ] && break; grep -q ABANDON $T.out 2>/dev/null && break; sleep 1; done
[ -f $L/$T.stand_start ] || { echo "VOID: no stand"; wait; cat $T.out; exit 1; }
sleep 20; powershell -NoProfile -ExecutionPolicy Bypass -File shotfull.ps1 "D:/fable2_flash/ab110/logs/sweep/${T}_play.png"; powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 "D:/fable2_flash/ab110/logs/sweep/${T}_play.jpg"
sleep 11; powershell -NoProfile -ExecutionPolicy Bypass -File shot.ps1 "D:/fable2_flash/ab110/logs/sweep/${T}_pause.jpg"
wait; cat $T.out
grep -a "render width\|render height\|internal scale\|GPU: internal" $L/$T.log | head -5 | cut -c13-170
grep -a "guest fps" $L/$T.log | tail -2 | cut -c13-75; grep -a "LOCKSTEP:" $L/$T.log | tail -1 | grep -o "draws ([0-9]* failed"
