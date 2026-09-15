#!/bin/sh
# Bower Lake flash A/B: load hero 1, run the one-frame flash detector for ~95 s
# while the camera sweeps (right stick) and the hero walks a little, then print
# the detector's verdict and the fps/fence lines.  flash_ab.sh <tag> "<extra tune>"
tag="$1"; extra="$2"
MY="/c/Users/renoi/AppData/Local/Temp/claude/C--users-renoi-claudecode/7d093996-252d-49c9-a593-f1ba6e716609/scratchpad"
cd "/c/users/renoi/claudecode/Fable 2 Recompile Xbox/fable2recomp" || exit 1
if tasklist | grep -qi "fable2\|ng2.exe"; then echo "a game is running - refusing"; exit 1; fi
rm -f out/fable2.log
pad="out/build/win-amd64-Release/pad_script.txt"
rm -f "$pad"
tune="fable2_60fps=true;draw_resolution_scale_x=2;draw_resolution_scale_y=2"
[ -n "$extra" ] && tune="$tune;$extra"
FABLE2_TUNE="$tune" FABLE2_PAD_SCRIPT="14:a,19:down,21:a,25:a" python tools/play_probe.py --seconds 165 --interval 165 --tag "$tag" \
  --extra=--log_flush_interval=1 > "out/${tag}_probe.out" 2>&1 &
sleep 62
python "$MY/flash_probe.py" "$MY/flash_$tag" 95 > "$MY/flash_$tag.txt" 2>&1 &
sleep 2
for i in 1 2 3; do
  printf 'r:1,0:9\n' > "$pad"; sleep 11
  printf 'r:-1,0:9\n' > "$pad"; sleep 11
done
printf 'l:0,1:5\n' > "$pad"; sleep 7
printf 'r:1,0:9\n' > "$pad"; sleep 11
printf 'r:-1,0:9\n' > "$pad"; sleep 11
wait
cp out/fable2.log "out/${tag}.log" 2>/dev/null
echo "=== $tag  tune: $extra"
cat "$MY/flash_$tag.txt"
grep "guest fps" "out/${tag}.log" | sed 's/.*\[swap\] //' | cut -c1-5 | tail -n +8 | tr '\n' ' '; echo
grep "fence waits" "out/${tag}.log" | tail -1 | sed 's/.*\[gpu\] //' | cut -c1-260
