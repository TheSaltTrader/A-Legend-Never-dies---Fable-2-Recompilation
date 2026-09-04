"""Report the health of a run, by enumerating what could be wrong from the log.

    python tools/health.py out/soak.log

"Fully running" is not a feeling. This turns a log into a checklist with
counts, so a claim like "no stops" is either backed by numbers or is not made.

Two rules it is built on, both learned here:

  * ABSENCE OF LOGGING IS NOT ABSENCE OF BEHAVIOUR. The runtime does not log a
    successful file open, so "no open errors" says nothing about whether the
    game loaded anything. Where a check cannot be made from the log, it says
    UNKNOWN rather than PASS.
  * The interesting failures here were never fatals. The freeze produced no
    error line at all - the tell was ~510 APCs a second with zero GPU work. So
    the checks below look for *shapes*, not just for the word "error".
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

TIME = re.compile(r"^\[\d{4}-\d\d-\d\d (\d\d:\d\d:\d\d)")

# Things that are always worth counting, with what each one means.
SIGNATURES = [
    ("fatal",            re.compile(r"\[FATAL\]|\[critical\]"),
     "a hard stop; any is too many"),
    ("unregistered fn",  re.compile(r"invalid or unregistered function"),
     "a function the recompiler never emitted - needs registering"),
    ("access violation", re.compile(r"Unhandled guest access violation"),
     "a bad guest pointer; often a mis-split function"),
    ("ring buffer",      re.compile(r"PRIMARY RINGBUFFER: Failed"),
     "the GPU command stream was misparsed"),
    ("bad register",     re.compile(r"WriteRegister index out of bounds"),
     "a register write outside the Xenos register file"),
    ("alloc reserved",   re.compile(r"attempting to reserve an already reserved"),
     "the guest allocator re-reserving; noisy at boot, not known to be harmful"),
    ("shader fail",      re.compile(r"(?i)failed to (translate|compile).*shader"),
     "a shader the plugin could not build"),
    ("unimplemented",    re.compile(r"(?i)not implemented|unimplemented"),
     "a kernel call with no implementation behind it"),
]

GPU = re.compile(r"\[gpu\]")
APC = re.compile(r"Completed delivery of APC")
HEARTBEAT = re.compile(r"XGIUserSetContextEx")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--quiet-gpu-seconds", type=int, default=20,
                    help="a GPU-silent stretch this long counts as a stall")
    args = ap.parse_args()

    if not os.path.exists(args.log):
        sys.exit(f"{args.log} does not exist")

    counts = Counter()
    per_sec = defaultdict(Counter)
    order = []
    examples = {}

    for line in open(args.log, encoding="utf-8", errors="replace"):
        m = TIME.match(line)
        t = m.group(1) if m else None
        if t and t not in per_sec:
            order.append(t)
        for name, rx, _ in SIGNATURES:
            if rx.search(line):
                counts[name] += 1
                examples.setdefault(name, line.strip()[:150])
        if t:
            if GPU.search(line):
                per_sec[t]["gpu"] += 1
            if APC.search(line):
                per_sec[t]["apc"] += 1
            if HEARTBEAT.search(line):
                per_sec[t]["beat"] += 1

    print(f"{args.log}: {len(order)} seconds of activity\n")

    print("signatures")
    for name, _, meaning in SIGNATURES:
        n = counts[name]
        mark = "ok  " if n == 0 else "SEE "
        print(f"   {mark} {name:<18} {n:>7}   {meaning}")
        if n and name in examples:
            print(f"            e.g. {examples[name]}")

    # The freeze shape: the guest still alive (heartbeat / APCs) with no GPU
    # work. This is the check that would have caught the RTV freeze without
    # anyone watching the screen.
    print("\nrendering")
    gpu_secs = [t for t in order if per_sec[t]["gpu"]]
    if not gpu_secs:
        print("   SEE  no GPU activity logged at all "
              "(expected at debug level; at info this is normal)")
    else:
        print(f"   ok   GPU active {gpu_secs[0]} .. {gpu_secs[-1]}")
        last = order.index(gpu_secs[-1])
        trailing = len(order) - 1 - last
        if trailing >= args.quiet_gpu_seconds:
            alive = sum(per_sec[t]["apc"] + per_sec[t]["beat"]
                        for t in order[last:])
            print(f"   SEE  GPU silent for the last {trailing}s "
                  f"while the guest stayed alive ({alive} APC/heartbeat lines "
                  f"in that stretch) - this is the freeze shape")
        else:
            print(f"   ok   GPU active to within {trailing}s of the end")

    beats = sum(per_sec[t]["beat"] for t in order)
    print(f"\nguest liveness\n   {beats} presence heartbeats "
          f"({'alive' if beats else 'none logged - info level hides them'})")

    print("\nUNKNOWN from a log alone: whether textures/characters actually "
          "drew, whether audio was audible, and whether a save round-trips. "
          "Those need a picture, an ear, or a reload.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
