#!/bin/sh
# Like bridge_leg.sh, but captures the presented windows EVERY 5 s for 12 samples during the stand, into
# D:\fable2_flash\gameplay\<TAG>_early\sNN\ - to tell a within-run flip from a between-run difference.
# Stages the MAIN pair, runs the standard leg runner with the bridge on, restores the v1.0.0 pair and the saves.
set -u
TAG="$1"; EXTRA="${2:-}"
case "$TAG" in *[!A-Za-z0-9_]*|'') echo "bad tag '$TAG'"; exit 2;; esac
W="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/wt-fable2-nativegpu"
B="$W/out/build/win-amd64-Release"
S=/d/fable2_flash/seam_record/bin_main_bridge
BK="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/RexBlue/win-amd64/bin/dll_backup_20260924_v1.0.0_prefixA2"
T=/c/Users/renoi/.claude/jobs/6397a53c/tmp
G=/d/fable2_flash/gameplay
python "$G/saves.py" verify > /dev/null || { echo "saves differ from snapshot - not running"; exit 2; }
cp -p "$S/rexgpu-xenos.dll" "$S/rexruntime.dll" "$B/"
cd "$W"
(SAVE_INDEX=1 EXPECT_REGION=bowerlake sh tools/native_gpu/ab_untile_leg.sh "$TAG" 1 60 "ngpu_bridge=true;ngpu_bridge_log=true;ngpu_bridge_draws=true${EXTRA:+;$EXTRA}" > "$T/$TAG.out" 2>&1 &)
i=0; while [ $i -lt 240 ] && ! grep -q "standing for\|ABANDON\|ABORT" "$T/$TAG.out" 2>/dev/null; do sleep 2; i=$((i+2)); done
grep "world loaded\|ABANDON\|ABORT" "$T/$TAG.out"
if grep -q "standing for" "$T/$TAG.out"; then
  PID=$(grep -o "launched pid [0-9]*" "$T/$TAG.out" | grep -o "[0-9]*$")
  python - "$PID" "$TAG" <<'PY'
import subprocess, sys, os, time
pid, tag = sys.argv[1], sys.argv[2]
root = os.path.join(r"D:\fable2_flash\gameplay", tag + "_early")
ok = 0
for k in range(16):
    time.sleep(0.2 if k == 0 else 1.0)
    out = os.path.join(root, "s%02d" % k)
    subprocess.run([sys.executable, r"D:\fable2_flash\gameplay\grab_windows.py", pid, out], capture_output=True, text=True)
    ok += os.path.isfile(os.path.join(out, "win0_wgc.png"))
print("series captures written:", ok, "of 12 in", root)
PY
fi
i=0; while [ $i -lt 200 ] && ! grep -q "done:\|ABANDON\|ABORT" "$T/$TAG.out"; do sleep 3; i=$((i+3)); done
cp -p "$BK/rexgpu-xenos.dll" "$BK/rexruntime.dll" "$B/"
echo "restored pair $(sha256sum "$B/rexgpu-xenos.dll" | cut -c1-12)"
python "$G/saves.py" restore | cut -c1-70
