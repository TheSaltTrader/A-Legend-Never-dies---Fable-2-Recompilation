#!/bin/sh
# costleg.sh TAG TUNE [SCALE] [SECS]: the split's COST leg (2026-09-29 13:10). Fairfax castle (the 1.1.5 measurement spot),
# internal scale SCALE (default 1: the CPU-bound case under the 60 fps vsync cap), NO travel: stand SECS s (default 150)
# panning, with ngpu_backend_frame_trace=true forced into TUNE so R113/ngpu_frames.bin holds one record per swap
# (the busiest thread's QueryThreadCycleTime, the 1.1.5 method). The file is copied to logs/TAG.frames.bin. Read with
# framea2.py; the fps from the log's [swap] lines. Arm check: "Tuning override" lines for every cvar in TUNE and the
# "[ngpu] FRAME TRACE: recording" line.
T="$1"; TUNE="ngpu_backend_frame_trace=true $2"; SC="${3:-1}"; SECS="${4:-150}"
L=/d/fable2_flash/ab110/logs; R=/d/fable2_flash/ab110/R113; P=$R/pad_script.txt
tasklist | grep -qi "fable2.exe\|ng2.exe" && { echo "$T REFUSED: a game is running"; exit 1; }
rm -f $T.out $L/$T.log $L/$T.stand_start $R/ngpu_frames.bin
(sh claim.sh 10 "cost $T" >/dev/null && FABLE2_TUNE="$TUNE" FABLE2_WORLD_HEIGHT=720 FABLE2_SCALE=$SC PAD_ROUTE="wait:1" sh arm_leg_tune.sh R113 $T 2 fairfax $SECS > $T.out 2>&1; sh claim.sh release $T) > /dev/null 2>&1 &
for k in $(seq 1 4000); do [ -f $L/$T.stand_start ] && break; grep -q ABANDON $T.out 2>/dev/null && break; sleep 1; done
[ -f $L/$T.stand_start ] || { echo "$T VOID"; wait; exit 1; }
sleep 5
W=""; k=0; while [ $k -lt 40 ]; do W="${W}r:0.6,0:2
r:-0.6,0:2
r:0,0.4:0.8
r:0,-0.4:0.8
"; k=$((k+1)); done
printf 'release\n%s' "$W" > $P.tmp; mv -f $P.tmp $P
wait
cp -p $R/ngpu_frames.bin $L/$T.frames.bin 2>/dev/null && echo "$T frames.bin: $(stat -c %s $L/$T.frames.bin) bytes" || echo "$T: NO ngpu_frames.bin"
echo "$T overrides: $(grep -a 'Tuning override' $L/$T.log | cut -c60-150 | tr '\n' ';') | trace: $(grep -a -c 'FRAME TRACE: recording' $L/$T.log) | scale: $(grep -a -o 'internal scale [0-9x]*' $L/$T.log | head -1)"
