"""Launch fable2 for a fixed time, stop it, and summarise what happened.

Bring-up questions ("how far does it get?") need an answer that does not
depend on a human watching a window, and they need to be repeatable, because
a recompiled title does not always behave the same way twice.

    python tools/boot.py --seconds 30 [--runs 3] [--config Release] [-- args...]

Prints, per run: how long the guest survived, whether it faulted, the last
few log lines, and counts of the failure signatures worth watching for.

IMPORTANT: the process is stopped by the PID this script started, never by
matching a window title. Matching on title once killed a two-day job in
another project that happened to share a name.
"""

import argparse
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Signatures worth counting. The GPU ones come from ng2recomp, where a
# mis-parsed command stream showed up exactly like this: rendering stops
# while the guest carries on, so the window looks frozen rather than crashed.
SIGNATURES = {
    "fatal": re.compile(r"\[(?:FATAL|ERROR|error|critical)\]"),
    "unregistered fn": re.compile(r"invalid or unregistered function"),
    "ring buffer": re.compile(r"PRIMARY RINGBUFFER: Failed"),
    "bad register": re.compile(r"WriteRegister index out of bounds"),
    "unimpl kernel": re.compile(r"(?:not implemented|unimplemented)", re.I),
}
PROGRESS = re.compile(r"fable2: (.+)")


def run_once(exe, game_dir, log_path, seconds, extra):
    if os.path.exists(log_path):
        os.remove(log_path)

    cmd = [exe,
           "--game_data_root", game_dir,
           "--log_file", log_path,
           "--log_level", "debug"] + extra

    started = time.monotonic()
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(exe),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    exited_early = None
    while time.monotonic() - started < seconds:
        if proc.poll() is not None:
            exited_early = round(time.monotonic() - started, 1)
            break
        time.sleep(0.25)

    if proc.poll() is None:
        proc.terminate()          # by PID, not by window title
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    return exited_early, proc.returncode


def summarise(log_path, exited_early, code, tail_lines):
    if not os.path.exists(log_path):
        print("  no log file was written")
        return
    text = open(log_path, encoding="utf-8", errors="replace").read()
    lines = text.splitlines()

    print(f"  log: {len(lines)} lines")
    if exited_early is None:
        print("  guest survived the whole window (stopped by us)")
    else:
        print(f"  process exited on its own after {exited_early}s, code {code}")

    milestones = [m.group(1) for line in lines
                  for m in [PROGRESS.search(line)] if m]
    if milestones:
        print("  progress: " + " -> ".join(milestones))

    counts = {name: sum(1 for line in lines if rx.search(line))
              for name, rx in SIGNATURES.items()}
    hits = {k: v for k, v in counts.items() if v}
    print("  signatures: " + (", ".join(f"{k}={v}" for k, v in hits.items())
                              if hits else "none"))

    if tail_lines:
        print(f"  --- last {tail_lines} lines ---")
        for line in lines[-tail_lines:]:
            print("   " + line)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--config", default="Release")
    ap.add_argument("--tail", type=int, default=25,
                    help="log lines to print at the end of each run (0 = none)")
    ap.add_argument("rest", nargs=argparse.REMAINDER,
                    help="arguments after -- are passed to fable2")
    args = ap.parse_args()

    extra = args.rest[1:] if args.rest[:1] == ["--"] else args.rest
    exe = os.path.join(ROOT, "out", "build", f"win-amd64-{args.config}", "fable2.exe")
    if not os.path.exists(exe):
        sys.exit(f"{exe} not found - run tools\\build.cmd {args.config} first")

    game_dir = os.path.join(ROOT, "game")
    log_path = os.path.join(ROOT, "out", "fable2.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)

    for run in range(1, args.runs + 1):
        print(f"=== run {run}/{args.runs} ({args.seconds:g}s) ===")
        exited_early, code = run_once(exe, game_dir, log_path, args.seconds, extra)
        summarise(log_path, exited_early, code, args.tail)
        if run < args.runs:
            time.sleep(2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
