#!/bin/sh
# Bower Lake (hero 1) with right-stick sweeps, the user's settings: how often the
# s92 readback wait fires ("awaited N ... M still open ... V pages valid") and the
# guest fps there. lake_await.sh <tag> "<extra tune>" [secs]
tag="$1"; extra="$2"; secs="${3:-120}"
cd "/c/users/renoi/claudecode/Fable 2 Recompile Xbox/fable2recomp" || exit 1
if tasklist | grep -qi "fable2\|ng2.exe"; then echo "a game is running - refusing"; exit 1; fi
rm -f out/fable2.log
pad="out/build/win-amd64-Release/pad_script.txt"
rm -f "$pad"
tune="fable2_60fps=true;draw_resolution_scale_x=2;draw_resolution_scale_y=2"
[ -n "$extra" ] && tune="$tune;$extra"
FABLE2_TUNE="$tune" FABLE2_PAD_SCRIPT="14:a,19:down,21:a,25:a" python tools/play_probe.py --seconds "$secs" --interval "$secs" --tag "$tag" \
  --extra=--log_flush_interval=1 > "out/${tag}_probe.out" 2>&1 &
sleep 60
for i in 1 2; do
  printf 'r:1,0:9\n' > "$pad"; sleep 11
  printf 'r:-1,0:9\n' > "$pad"; sleep 11
done
printf 'l:0,1:8\n' > "$pad"; sleep 10
wait
cp out/fable2.log "out/${tag}.log" 2>/dev/null
echo "=== $tag  tune: $extra"
grep "region .* is loading" "out/${tag}.log" | tail -1 | cut -c12-24,49-100
grep "guest fps" "out/${tag}.log" | sed 's/.*\[swap\] //' | cut -c1-16 | tail -n +8 | tr '\n' ' '; echo
grep "fence waits" "out/${tag}.log" | sed 's/.*\[gpu\] //' | grep -o "awaited[^,]*, [^,]*, [^,]*" | tail -8
