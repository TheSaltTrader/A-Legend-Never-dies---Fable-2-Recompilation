"""Find which quarantined function registration breaks the build.

Registering the whole "channel 2" set from scan_fnptrs.py (function pointers
materialised in code with lis/addi) took the game from *reaches character
select* to *unhandled read of guest 0x54, milliseconds after launch*. A null
dereference far from its cause is exactly the signature ng2recomp warns about,
and reading 51 candidate registrations to guess which one did it is not a
method.

So bisect. Each step re-enables a subset of the quarantined addresses,
regenerates the TOML block, rebuilds, launches the game, and asks one question:
did it survive? Then it halves.

    python tools/bisect_fnptrs.py [--seconds 20] [--tag "channel 2"]

The quarantined addresses are the lines in config/fnptr_exclude.txt carrying
the marker comment; everything else in that file stays excluded throughout.

A pass means "ran for --seconds with no guest access violation and no fatal",
which is a weaker claim than "still reaches character select" - a registration
could break something later. Re-run the full play probe once the bisect names
a culprit.
"""

import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXCLUDE = os.path.join(ROOT, "config", "fnptr_exclude.txt")
MARKER = "channel 2, quarantined"
LOG = os.path.join(ROOT, "out", "bisect.log")

BAD = re.compile(r"Unhandled guest access violation|\[FATAL\]|\[critical\]")


def read_exclude():
    """(preamble lines, quarantined addresses, other excluded lines)."""
    quarantined, others = [], []
    for line in open(EXCLUDE, encoding="utf-8").read().splitlines():
        if MARKER in line:
            quarantined.append(int(line.split("#")[0].strip(), 16))
        else:
            others.append(line)
    return others, quarantined


def write_exclude(others, still_excluded):
    with open(EXCLUDE, "w", encoding="utf-8") as f:
        f.write("\n".join(others).rstrip("\n") + "\n")
        for addr in sorted(still_excluded):
            f.write(f"0x{addr:08X}  # {MARKER} pending bisect\n")


def regenerate():
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "scan_fnptrs.py"), "--write"],
                       cwd=ROOT, capture_output=True, text=True)
    return r.returncode == 0


def build():
    r = subprocess.run(["cmd", "/c", os.path.join(ROOT, "tools", "build.cmd"),
                        "Release"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print("    BUILD FAILED:")
        for line in (r.stdout + r.stderr).splitlines()[-8:]:
            print("      " + line)
    return r.returncode == 0


def survives(seconds):
    exe = os.path.join(ROOT, "out", "build", "win-amd64-Release", "fable2.exe")
    if os.path.exists(LOG):
        os.remove(LOG)
    proc = subprocess.Popen([exe, "--game_data_root", os.path.join(ROOT, "game"),
                             "--log_file", LOG, "--log_level", "info"],
                            cwd=os.path.dirname(exe),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + seconds
    while time.time() < deadline and proc.poll() is None:
        time.sleep(0.25)
    died = proc.poll() is not None
    if not died:
        proc.terminate()                    # by PID, never by window title
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    text = open(LOG, encoding="utf-8", errors="replace").read() \
        if os.path.exists(LOG) else ""
    fatal = [ln for ln in text.splitlines() if BAD.search(ln)]
    if fatal:
        print(f"    FAIL: {fatal[0].strip()[-110:]}")
    elif died:
        print("    FAIL: exited on its own with nothing fatal logged")
    return not fatal and not died


def test(others, quarantined, enabled, seconds):
    """Enable `enabled`, keep the rest quarantined, and see if it survives."""
    write_exclude(others, set(quarantined) - set(enabled))
    if not regenerate():
        sys.exit("scan_fnptrs.py --write failed")
    if not build():
        return False
    return survives(seconds)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=20)
    args = ap.parse_args()

    others, quarantined = read_exclude()
    if not quarantined:
        sys.exit(f"no lines marked '{MARKER}' in {EXCLUDE}")
    print(f"{len(quarantined)} quarantined address(es) to bisect")

    print("\nbaseline: none enabled")
    if not test(others, quarantined, [], args.seconds):
        sys.exit("the baseline itself fails - the culprit is not in this set")
    print("    ok")

    print("\nsanity: all enabled (expected to fail)")
    if test(others, quarantined, quarantined, args.seconds):
        write_exclude(others, quarantined)
        regenerate()
        sys.exit("all of them together pass - nothing to bisect")

    suspects = list(quarantined)
    while len(suspects) > 1:
        half = suspects[:len(suspects) // 2]
        print(f"\ntrying {len(half)} of {len(suspects)}: "
              f"0x{half[0]:08X}..0x{half[-1]:08X}")
        if test(others, quarantined, half, args.seconds):
            print("    ok - culprit is in the other half")
            suspects = suspects[len(suspects) // 2:]
        else:
            suspects = half

    culprit = suspects[0]
    print(f"\nCULPRIT: 0x{culprit:08X}")
    # Leave the tree in the known-good state, with the culprit still excluded.
    write_exclude(others, quarantined)
    regenerate()
    build()
    print("tree restored to the fully-quarantined (known-good) set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
