#!/bin/sh
# armlegs.sh FIRST LAST: the ARMING-STATE series (2026-09-29 13:00). Runs dump legs ARM_FIRST..ARM_LAST back to back
# (uwpandd2.sh: stationary woods pan, user settings, 3x/720, per-draw dump armed at the first pan line, filter 1, no
# hashes), each 45 s burst + 1500 dumped frames. Each leg is gated by the reader on its in-burst ring waits before its
# flash count is read; a clean leg outside the band is void. Stops at a VOID launch or when D: has < 20 GB free.
F="${1:-1}"; L="${2:-12}"; LOGS=/d/fable2_flash/ab110/logs
k=$F
while [ $k -le $L ]; do
  T="ARM_$k"
  free=$(df -k /d | tail -1 | awk '{print $4}'); [ "$free" -lt 20000000 ] && { echo "$T: D: below 20 GB free - stopping"; exit 2; }
  tasklist | grep -qi "fable2.exe\|ng2.exe" && { echo "$T: a game is running - stopping"; exit 1; }
  echo "$(date +%H:%M:%S) $T launching"
  sh uwpandd2.sh $T "gpu_draw_dump_file=D:/fable2_flash/ab110/logs/$T.dd gpu_draw_dump_filter=1" 45 1500 2>&1 | tail -2
  [ -f $LOGS/$T.stand_start ] || { echo "$T VOID launch - stopping"; exit 1; }
  echo "$(date +%H:%M:%S) $T done: $(ls $LOGS/sweep/${T}_burst 2>/dev/null | wc -l) frames, dump $(du -m $LOGS/$T.dd 2>/dev/null | cut -f1) MB"
  sleep 5
  k=$((k+1))
done
