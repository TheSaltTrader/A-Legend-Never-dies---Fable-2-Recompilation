#!/bin/sh
# frontend_leg.sh TAG "CVARS": the FRONT END (title, main menu, save cards, the animated load screen) captured in
# both windows - native (win0) and plugin (win1) - every 2 s from launch until the world has loaded, through the
# standard leg runner (lock, own-PID kill, saves verified and restored). The plugin reference is unenhanced
# (bridge_leg_ref1x.sh does the settings swap). Captures: D:\fable2_flash\gameplay\<TAG>_front\tNNN\.
TAG="$1"; EXTRA="${2:-}"
case "$TAG" in *[!A-Za-z0-9_]*|'') echo "bad tag '$TAG'"; exit 2;; esac
T=/c/Users/renoi/.claude/jobs/6397a53c/tmp
ROOT="D:\\fable2_flash\\gameplay\\${TAG}_front"
# the capture loop: waits for the leg to report its PID, then grabs every 2 s for up to 150 s
( i=0; while [ $i -lt 120 ] && ! grep -q "launched pid" "$T/$TAG.out" 2>/dev/null; do sleep 1; i=$((i+1)); done
  PID=$(grep -o "launched pid [0-9]*" "$T/$TAG.out" | grep -o "[0-9]*$")
  [ -n "$PID" ] && python - "$PID" "$ROOT" <<'PY'
import subprocess, sys, os, time
pid, root = sys.argv[1], sys.argv[2]
t0 = time.time(); k = 0
while time.time() - t0 < 150:
    out = os.path.join(root, "t%03d" % int(time.time() - t0))
    r = subprocess.run([sys.executable, r"D:\fable2_flash\gameplay\grab_windows.py", pid, out], capture_output=True, text=True)
    k += 1
    if r.returncode != 0 and "not running" in (r.stdout + r.stderr).lower(): break
    time.sleep(2)
print("front-end captures:", k, "in", root)
PY
) > "$T/$TAG.front.txt" 2>&1 &
sh /d/fable2_flash/gameplay/bridge_leg_ref1x.sh "$TAG" "ngpu_hooked_draws=false${EXTRA:+;$EXTRA}"
wait
cat "$T/$TAG.front.txt"
