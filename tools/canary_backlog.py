"""The Xenia Canary GPU commits made after the ReXGlue SDK forked.

    python tools/canary_backlog.py                 # the worklist, newest first
    python tools/canary_backlog.py --show <sha>    # one commit's diff, mapped to our paths
    python tools/canary_backlog.py --status        # counts by state

THE FORK POINT

The SDK is a fork of Canary. Probing distinctive changes against our tree dates
it to about 2026-08-01: `[GPU] Implement wide 1D texture support` (2026-07-31)
is present, `[GPU] Host RT polygon offset for Z-fighting decals` (2026-08-01) is
not. Everything Canary landed in src/xenia/gpu from then on is a candidate.

Note the Canary checkout must NOT be a shallow clone - it ships as one commit
with no history. `git fetch --unshallow` first, or this tool sees nothing.

STATE COMES FROM PORTED.md, NOT FROM GUESSWORK

Each commit is listed with a state read from docs/CANARY-PORTED.md, so the
worklist is a ledger rather than a fresh opinion each run. A commit nobody has
judged reads as TODO, which is the point: an unexamined commit must be visible,
not absent. Add a line to that file when you port, skip or dismiss one, and say
WHY - "not applicable, Vulkan-only" is a decision; silence is not.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CANARY = os.path.join(ROOT, "..", "..", "Ninja Gaiden 2 Xbox360",
                      "xenia-canary-src")
LEDGER = os.path.join(ROOT, "docs", "CANARY-PORTED.md")
SINCE = "2026-07-31"

# Canary path fragment -> which of our backends it can affect.
def area(files):
    has = lambda frag: any(frag in f for f in files)
    if has("/vulkan/"):
        if not (has("/d3d12/") or any("/vulkan/" not in f and "/gpu/" in f
                                      for f in files)):
            return "vulkan"
    if has("/d3d12/"):
        return "d3d12"
    return "shared"


def git(*args):
    return subprocess.run(["git"] + list(args), cwd=CANARY, capture_output=True,
                          text=True, encoding="utf-8", errors="replace").stdout


def ledger():
    """sha -> (state, note) from docs/CANARY-PORTED.md."""
    out = {}
    if not os.path.exists(LEDGER):
        return out
    for line in open(LEDGER, encoding="utf-8"):
        m = re.match(r"\|\s*`?([0-9a-f]{7,40})`?\s*\|\s*(\w+)\s*\|\s*(.*?)\s*\|", line)
        if m:
            out[m.group(1)[:9]] = (m.group(2).upper(), m.group(3))
    return out


def commits():
    # TOPOLOGICAL, oldest first, and dated by COMMIT date - not author date.
    # Author date is the wrong order to port in: fbdb1f281 is authored
    # 2026-08-02 and 947075f88 2026-07-31, but 947075f88 was committed second
    # and its diff already assumes fbdb1f281's coordinate_dimension. Sorting by
    # author date puts the dependent commit first and its context will not
    # match.
    raw = git("log", "--reverse", "--topo-order",
              "--format=%H%x01%cd%x01%s", "--date=short",
              "--since=" + SINCE, "--", "src/xenia/gpu")
    out = []
    for line in raw.splitlines():
        if not line.strip():
            continue
        sha, date, subject = line.split("\x01", 2)
        files = git("show", "--pretty=", "--name-only", sha).split()
        out.append((sha[:9], date, subject, files))
    return out


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--show", help="print one commit's full diff")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--area", choices=["shared", "d3d12", "vulkan"])
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(CANARY, ".git")):
        sys.exit("Canary git checkout not found: %s" % CANARY)
    if os.path.exists(os.path.join(CANARY, ".git", "shallow")):
        sys.exit("The Canary clone is SHALLOW - it has no history to read.\n"
                 "Run:  git -C %s fetch --unshallow" % CANARY)

    if args.show:
        print(git("show", args.show))
        return 0

    led = ledger()
    rows = commits()
    if args.area:
        rows = [r for r in rows if area(r[3]) == args.area]

    counts = {}
    print("%-10s %-10s %-7s %-8s %s" % ("sha", "date", "area", "state", "subject"))
    print("-" * 100)
    for sha, date, subject, files in rows:
        state, note = led.get(sha, ("TODO", ""))
        counts[state] = counts.get(state, 0) + 1
        line = "%-10s %-10s %-7s %-8s %s" % (sha, date, area(files), state,
                                             subject[:60])
        print(line)
        if note and state != "TODO":
            print("%38s%s" % ("", note[:80]))

    print("\n%d commits since %s" % (len(rows), SINCE))
    for k in sorted(counts):
        print("   %-8s %d" % (k, counts[k]))
    if counts.get("TODO"):
        print("\n%d unexamined. A commit nobody has judged is not the same as one\n"
              "that does not apply - record the decision in docs/CANARY-PORTED.md."
              % counts["TODO"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
