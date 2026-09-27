#!/bin/sh
# ONE native bridge leg with window captures: bridge_leg.sh <TAG> "<extra FABLE2_TUNE entries>"
# Stages the clean MAIN pair (bin_main_bridge) into the native build dir, runs the standard leg
# runner with the bridge on, captures the presented windows 30 s into the stand with WGC + GDI
# into D:\fable2_flash\gameplay\<TAG>_windows, then restores the v1.0.0 pair and the saves.
# The capture folder is built from the tag INSIDE python (no shell escaping) and ASSERTED to
# exist afterwards: a quoting slip once wrote two legs into one folder and destroyed a leg.
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
  sleep 30
  PID=$(grep -o "launched pid [0-9]*" "$T/$TAG.out" | grep -o "[0-9]*$")
  python - "$PID" "$TAG" <<'PY'
import subprocess, sys, os
pid, tag = sys.argv[1], sys.argv[2]
out = os.path.join(r"D:\fable2_flash\gameplay", tag + "_windows")
r = subprocess.run([sys.executable, r"D:\fable2_flash\gameplay\grab_windows.py", pid, out], capture_output=True, text=True)
print("\n".join(l for l in r.stdout.splitlines() if "native shadow" in l or "mean rgb" in l))
assert os.path.isfile(os.path.join(out, "win0_wgc.png")), "CAPTURE MISSING: " + out
print("capture folder OK:", out)
PY
fi
i=0; while [ $i -lt 200 ] && ! grep -q "done:\|ABANDON\|ABORT" "$T/$TAG.out"; do sleep 3; i=$((i+3)); done
cp -p "$BK/rexgpu-xenos.dll" "$BK/rexruntime.dll" "$B/"
echo "restored pair $(sha256sum "$B/rexgpu-xenos.dll" | cut -c1-12)"
python "$G/saves.py" restore | cut -c1-70
