"""Launch fable2, press buttons on a schedule, and photograph the result.

The title screen says "Press A to start", and there is no way to know whether
the game gets past it without actually pressing it and looking.

Two rules, both learned the hard way on ng2recomp:

  * **Input must be real.** SDL3 takes the keyboard from raw input and ignores
    synthesised window messages, so PostMessage(WM_KEYDOWN) does nothing at
    all. This uses SendInput, after bringing the window to the foreground and
    checking that it actually got there - otherwise the keystroke lands in
    whatever window did.
  * **Never capture the screen region under the window**, and never resolve the
    window by title. PrintWindow cannot read a D3D12 swapchain, a region grab
    photographs whatever is on top, and a title match can land on somebody
    else's window. Windows Graphics Capture bound to the HWND of the PID we
    launched is the only correct answer.

    python tools/play_probe.py --press 20:return --press 26:return \\
                               --seconds 40 --interval 4

`--press SECONDS:KEY` is repeatable. KEY is a name from KEYS below.
The SDK's keyboard-to-controller emulation is off by default, so this passes
--mnk_mode=true unless told otherwise.
"""

import argparse
import ctypes
import os
import subprocess
import sys
import time

import win32gui
import win32process
from windows_capture import WindowsCapture

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Windows virtual-key codes for the keys worth pressing here.
KEYS = {
    "return": 0x0D, "enter": 0x0D, "space": 0x20, "escape": 0x1B,
    "backspace": 0x08, "tab": 0x09, "lshift": 0xA0, "lctrl": 0xA2,
    "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27,
    "f4": 0x73, "f10": 0x79,
}
# Letters, so a probe can try both the likely stick keys (wasd) and whatever
# the SDK happens to bind the face buttons to.
KEYS.update({chr(c).lower(): c for c in range(ord("A"), ord("Z") + 1)})
KEYS.update({str(d): 0x30 + d for d in range(10)})

# Pin the controller mapping instead of guessing at the SDK's defaults.
#
# Guessing cost real time: `right` looked like it advanced past character
# select when in fact `tab` had, two presses earlier, and a whole 220-second
# probe sat on an unhighlighted card doing nothing. The values are SDL key
# names (the runtime carries SDL_GetKeyName's table - "Space", "Return",
# "Left", "Keypad Space" and so on), so these are unambiguous.
EXPLICIT_BINDS = [
    "--keybind_a=Space",
    "--keybind_b=Escape",
    "--keybind_x=X",
    "--keybind_y=Y",
    "--keybind_start=Return",
    "--keybind_back=Backspace",
    "--keybind_dpad_up=Up",
    "--keybind_dpad_down=Down",
    "--keybind_dpad_left=Left",
    "--keybind_dpad_right=Right",
    "--keybind_lstick_up=W",
    "--keybind_lstick_down=S",
    "--keybind_lstick_left=A",
    "--keybind_lstick_right=D",
]

KEYEVENTF_KEYUP = 0x0002
INPUT_KEYBOARD = 1


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", ctypes.c_ushort), ("wScan", ctypes.c_ushort),
                ("dwFlags", ctypes.c_ulong), ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulonglong))]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("padding", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_ulong), ("u", _U)]


def send_key(vk, hold=0.05):
    """Press and release. `hold` matters: a menu that samples the pad once a
    frame can miss a press shorter than a frame or two, and a missed press
    looks exactly like a key that is not bound to anything."""
    for flags in (0, KEYEVENTF_KEYUP):
        inp = INPUT(type=INPUT_KEYBOARD)
        inp.ki = KEYBDINPUT(wVk=vk, wScan=0, dwFlags=flags, time=0,
                            dwExtraInfo=None)
        ctypes.windll.user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
        time.sleep(hold if flags == 0 else 0.05)


def hwnd_for_pid(pid):
    """Top-level visible window owned by exactly this PID."""
    found = []

    def cb(h, _):
        if not win32gui.IsWindowVisible(h):
            return
        _, wpid = win32process.GetWindowThreadProcessId(h)
        if wpid == pid and win32gui.GetWindowText(h):
            found.append(h)

    win32gui.EnumWindows(cb, None)
    return found[0] if found else None


def focus(hwnd):
    """Bring the window forward and confirm it. A keystroke sent to the wrong
    foreground window is silent and looks exactly like the game ignoring it."""
    # Retried: the first press of a run landed with the window not yet
    # foreground twice in a row (2026-09-12), once around the fullscreen
    # switch, and a schedule that depends on that press is then off by one
    # screen. A tap of ALT first is the documented way past the foreground
    # lock when this process has not received input recently.
    for attempt in range(6):
        try:
            if attempt:
                ctypes.windll.user32.keybd_event(0x12, 0, 0, 0)        # ALT down
                ctypes.windll.user32.keybd_event(0x12, 0, 2, 0)        # ALT up
            win32gui.ShowWindow(hwnd, 9)          # SW_RESTORE
            win32gui.SetForegroundWindow(hwnd)
        except Exception:
            pass
        time.sleep(0.3)
        if win32gui.GetForegroundWindow() == hwnd:
            return True
    return False


def main():
    ctypes.windll.shcore.SetProcessDpiAwareness(2)   # must be the first thing

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--interval", type=float, default=4.0)
    ap.add_argument("--outdir", default="out/shots")
    ap.add_argument("--tag", default="play")
    ap.add_argument("--press", action="append", default=[],
                    metavar="SECONDS:KEY", help="repeatable, e.g. 20:return")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--exe", help="run this executable instead of our build "
                                  "(for comparing against Xenia Canary)")
    ap.add_argument("--exe-args", action="append", default=[],
                    help="arguments for --exe, repeatable; ours are not added")
    ap.add_argument("--no-mnk", action="store_true")
    ap.add_argument("--manual", action="store_true",
                    help="hands off: send no keys and never steal focus, so a "
                         "person can drive with a controller. Frames and the log "
                         "are still captured. Any --press is ignored.")
    ap.add_argument("--hold", type=float, default=0.20,
                    help="seconds to hold each key down")
    ap.add_argument("--extra", action="append", default=[], metavar="ARG",
                    help="extra argument passed to the game, repeatable")
    ap.add_argument("--freeze-report", action="store_true",
                    help="report the last second at which the picture changed")
    args = ap.parse_args()

    # SECONDS:KEY or SECONDS:KEY:HOLD. The per-press hold is what makes it
    # possible to WALK: a 0.25 s tap moves the stick for a quarter second,
    # which is a twitch, not a journey.
    schedule = []
    for spec in args.press:
        parts = spec.split(":")
        when, name = parts[0], parts[1].lower()
        hold = float(parts[2]) if len(parts) > 2 else None
        if name not in KEYS:
            sys.exit(f"unknown key {name!r}; known: {', '.join(sorted(KEYS))}")
        schedule.append([float(when), name, False, hold])
    schedule.sort()

    outdir = os.path.join(ROOT, args.outdir)
    os.makedirs(outdir, exist_ok=True)

    if args.exe:
        # Comparison mode: drive some other emulator with the same schedule and
        # the same measurement, so "does it do this too?" is answered the same
        # way for both.
        exe = args.exe
        if not os.path.exists(exe):
            sys.exit(f"{exe} not found")
        cmd = [exe] + args.exe_args
    else:
        exe = os.path.join(ROOT, "out", "build", f"win-amd64-{args.config}",
                           "fable2.exe")
        if not os.path.exists(exe):
            sys.exit(f"{exe} not found - build first")

        cmd = [exe, "--game_data_root", os.path.join(ROOT, "game"),
               "--log_file", os.path.join(ROOT, "out", "fable2.log"),
               "--log_level", "debug"]
        if not args.no_mnk:
            cmd += ["--mnk_mode=true"]       # a bare --mnk_mode is ignored
            cmd += EXPLICIT_BINDS
        cmd += args.extra

    if args.manual and args.press:
        # Said rather than silently dropped: a schedule that looks honoured and
        # is not would make the frames impossible to interpret.
        print("--manual: ignoring %d scheduled press(es); you are driving."
              % len(args.press))
        args.press = []
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe))
    print(f"launched pid {proc.pid}")

    hwnd, deadline = None, time.time() + 30
    while time.time() < deadline and hwnd is None:
        if proc.poll() is not None:
            print(f"process exited early rc={proc.returncode}")
            return 1
        hwnd = hwnd_for_pid(proc.pid)
        if hwnd is None:
            time.sleep(0.25)
    if hwnd is None:
        proc.kill()
        print("no window appeared")
        return 1
    print(f"window 0x{hwnd:X} '{win32gui.GetWindowText(hwnd)}'")
    if args.manual:
        print("MANUAL: no keys will be sent and focus will not be taken again.")
        print("        Drive with the controller or the keyboard; frames are still")
        print("        captured every %.0fs for %.0fs." % (args.interval, args.seconds))

    # An HWND exists slightly before Windows Graphics Capture will accept it;
    # binding too early throws "Failed to convert item to GraphicsCaptureItem"
    # and the whole run is lost. Give it a moment, and retry.
    started = time.time()
    state = {"n": 0, "next": started, "prev": None, "last_change": 0.0,
             "changes": 0, "same": 0}

    def build_capture():
        capture = WindowsCapture(window_hwnd=hwnd, cursor_capture=False,
                                 draw_border=False)
        capture.event(on_frame_arrived)
        capture.event(on_closed)
        return capture

    def on_frame_arrived(frame, control):
        now = time.time()
        elapsed = now - started
        if elapsed >= args.seconds:
            control.stop()
            return

        for item in schedule:
            when, name, done, hold = item
            if not done and elapsed >= when:
                item[2] = True
                # In manual mode this loop never runs - the schedule is empty -
                # so focus is taken exactly once, at launch, and never again.
                ok = focus(hwnd)
                send_key(KEYS[name], hold if hold is not None else args.hold)
                print(f"  {elapsed:5.1f}s  pressed {name}"
                      f"{'' if hold is None else f' for {hold}s'}"
                      f"{'' if ok else '  (WINDOW WAS NOT FOREGROUND)'}")

        if now < state["next"]:
            return
        state["next"] = now + args.interval
        state["n"] += 1

        # A frozen picture repeats byte for byte. Comparing the raw buffer is
        # the direct measure of the symptom - far better than counting [gpu]
        # log lines, which only exist at debug level, where ~510 APC lines a
        # second rotate the transition out of the log entirely.
        if args.freeze_report:
            try:
                buf = bytes(frame.frame_buffer)
                if state["prev"] is not None:
                    if buf == state["prev"]:
                        state["same"] += 1
                    else:
                        state["changes"] += 1
                        state["last_change"] = elapsed
                state["prev"] = buf
            except Exception:
                pass

        path = os.path.join(outdir, f"{args.tag}_{state['n']:02d}.png")
        try:
            frame.save_as_image(path)
            print(f"  {elapsed:5.1f}s  {os.path.basename(path)} "
                  f"({frame.width}x{frame.height})")
        except Exception as exc:            # a dropped frame must not stop the run
            print(f"  save failed: {exc}")

    def on_closed():
        print("capture closed")

    try:
        # start() is where a not-yet-ready window actually fails, with
        # "Failed to convert item to GraphicsCaptureItem" - and it loses the
        # whole run. Rebuild and retry rather than hand-retrying the probe.
        for attempt in range(1, 11):
            try:
                started = time.time()
                state["next"] = started
                build_capture().start()
                break
            except Exception as exc:
                if "GraphicsCaptureItem" not in str(exc) or attempt == 10:
                    raise
                print(f"  capture not ready yet ({attempt}/10); retrying")
                time.sleep(1.0)
    finally:
        if proc.poll() is None:
            proc.kill()                     # by PID, never by window title
            proc.wait(timeout=10)
        print(f"stopped pid {proc.pid}; {state['n']} frames in {outdir}")
        if args.freeze_report:
            print(f"FREEZE: last picture change at {state['last_change']:.0f}s "
                  f"of {args.seconds:.0f}s "
                  f"(changed {state['changes']}, identical {state['same']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
