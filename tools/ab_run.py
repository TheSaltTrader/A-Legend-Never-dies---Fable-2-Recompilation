"""Run the game twice with one cvar changed, and measure when the PICTURE dies.

    python tools/ab_run.py --cvar render_target_path_d3d12 --a rov --b rtv \
                           --seconds 260

Measures the symptom, not a proxy for it. The first version of this script
counted `[gpu]` log lines, which was worthless: pipeline creation logs at
`debug`, and running at debug buries the log in ~510 APC lines a second and
rotates away the very transition being looked for. So instead this captures the
window's own frames and reports the last second at which the image actually
changed. A frozen picture repeats byte-for-byte; that is directly the thing the
player sees.

Capture is Windows Graphics Capture bound to the HWND of the PID we launched -
never a screen-region grab (which photographs whatever is on top) and never a
window-title match (which can land on somebody else's window).

The shader cache is cleared between runs. A pipeline cached under the other
setting would otherwise be loaded straight back in, and the community guidance
for this game is explicit that stale cached shaders make its texture bug
linger.
"""

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import time

import win32gui
import win32process
from windows_capture import WindowsCapture

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_CACHE = os.path.join(os.path.expanduser("~"), "Documents", "fable2",
                            "cache", "shaders")


def clear_shader_cache():
    if os.path.isdir(SHADER_CACHE):
        shutil.rmtree(SHADER_CACHE, ignore_errors=True)
        return True
    return False


def hwnd_for_pid(pid):
    found = []

    def cb(h, _):
        if not win32gui.IsWindowVisible(h):
            return
        _, wpid = win32process.GetWindowThreadProcessId(h)
        if wpid == pid and win32gui.GetWindowText(h):
            found.append(h)

    win32gui.EnumWindows(cb, None)
    return found[0] if found else None


def run_once(label, cvar, value, seconds, config, interval, outdir):
    cleared = clear_shader_cache()
    log = os.path.join(ROOT, "out", f"ab_{label}.log")
    if os.path.exists(log):
        os.remove(log)

    exe = os.path.join(ROOT, "out", "build", f"win-amd64-{config}", "fable2.exe")
    cmd = [exe,
           "--game_data_root", os.path.join(ROOT, "game"),
           "--log_file", log, "--log_level", "info"]
    if cvar:
        cmd.append(f"--{cvar}={value}")

    print(f"\n=== {label}: {cvar}={value or '(default)'}"
          f"{'  [shader cache cleared]' if cleared else ''} ===", flush=True)
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    hwnd, deadline = None, time.time() + 30
    while time.time() < deadline and hwnd is None:
        if proc.poll() is not None:
            print(f"   process exited early rc={proc.returncode}")
            return
        hwnd = hwnd_for_pid(proc.pid)
        if hwnd is None:
            time.sleep(0.25)
    if hwnd is None:
        proc.kill()
        print("   no window appeared")
        return

    started = time.time()
    state = {"n": 0, "next": started, "prev": None, "last_change": 0.0,
             "changes": 0, "same": 0}

    def on_frame_arrived(frame, control):
        now = time.time()
        elapsed = now - started
        if elapsed >= seconds:
            control.stop()
            return
        if now < state["next"]:
            return
        state["next"] = now + interval
        state["n"] += 1
        try:
            buf = bytes(frame.frame_buffer)          # raw pixels, no encoding
        except Exception:
            return
        if state["prev"] is not None:
            if buf == state["prev"]:
                state["same"] += 1
            else:
                state["changes"] += 1
                state["last_change"] = elapsed
        state["prev"] = buf

    def on_closed():
        pass

    try:
        for attempt in range(1, 11):
            try:
                cap = WindowsCapture(window_hwnd=hwnd, cursor_capture=False,
                                     draw_border=False)
                cap.event(on_frame_arrived)
                cap.event(on_closed)
                started = time.time()
                state["next"] = started
                cap.start()
                break
            except Exception as exc:
                if "GraphicsCaptureItem" not in str(exc) or attempt == 10:
                    raise
                time.sleep(1.0)
    finally:
        if proc.poll() is None:
            proc.kill()                     # by PID, never by window title
            proc.wait(timeout=10)

    print(f"   samples {state['n']}  changed {state['changes']}  "
          f"identical {state['same']}")
    print(f"   LAST PICTURE CHANGE at {state['last_change']:.0f}s "
          f"of {seconds:.0f}s")
    if state["last_change"] and seconds - state["last_change"] > 3 * interval:
        print(f"   -> frozen for the final "
              f"{seconds - state['last_change']:.0f}s")
    else:
        print("   -> still moving at the end")


def main():
    ctypes.windll.shcore.SetProcessDpiAwareness(2)

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cvar", default="render_target_path_d3d12")
    ap.add_argument("--a", default="rov")
    ap.add_argument("--b", default="rtv")
    ap.add_argument("--seconds", type=float, default=260)
    ap.add_argument("--interval", type=float, default=3.0)
    ap.add_argument("--config", default="Release")
    ap.add_argument("--outdir", default="out/shots")
    args = ap.parse_args()

    for label, value in (("a", args.a), ("b", args.b)):
        run_once(label, args.cvar, value, args.seconds, args.config,
                 args.interval, args.outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
