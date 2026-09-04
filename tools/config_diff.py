"""Diff Xenia Canary's settings against ours, for cvars both actually have.

    python tools/config_diff.py [--canary ../canary/xenia-canary.config.toml] \
                                [--ours out/cvars.txt]

WHY
---
Canary plays Fable II through the point where our build renders the scene flat
blue - same disc, same build (v0.0.0.26), same community patches, confirmed by
playing it. The GPU plugin here is Xenia-derived, so a setting Canary sets
differently is the cheapest possible explanation, and the cheapest thing to
try.

This only reports names present on BOTH sides. A cvar we have and Canary does
not (or the reverse) says nothing about the bug and would just be noise - the
two projects are not the same program, they only share ancestry.

`out/cvars.txt` comes from running our build with FABLE2_DUMP_CVARS=<path>, so
"ours" is what the build really registered, not what a header suggests.
"""

import argparse
import os
import re
import sys

VALUE = re.compile(r'^([A-Za-z0-9_]+)\s*=\s*(.+?)\s*(?:#.*)?$')


def read_canary(path):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if not line or line.lstrip().startswith("#"):
            continue
        m = VALUE.match(line)
        if not m:
            continue
        name, value = m.group(1), m.group(2).strip()
        # Canary writes a tab before its comment; the regex above keeps
        # everything before it, so trim any trailing tabbed remnant.
        value = value.split("\t")[0].strip()
        out[name] = value.strip('"')
    return out


def read_ours(path):
    """The `value` line of each block in our FABLE2_DUMP_CVARS output."""
    out = {}
    name = None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("#"):
            continue
        if not line.startswith(" ") and line.strip():
            name = line.strip()
            continue
        m = re.match(r"\s+value\s+(.*)$", line.rstrip("\n"))
        if m and name:
            out[name] = m.group(1).strip()
    return out


def normalise(v):
    v = v.strip().strip('"')
    if v in ("1", "true", "True"):
        return "true"
    if v in ("0", "false", "False"):
        return "false"
    # 1 and 1.000000 are the same number
    try:
        f = float(v)
        return f"{f:g}"
    except ValueError:
        return v


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--canary",
                    default=os.path.join(root, "..", "canary",
                                         "xenia-canary.config.toml"))
    ap.add_argument("--ours", default=os.path.join(root, "out", "cvars.txt"))
    ap.add_argument("--all", action="store_true",
                    help="also list the names only one side has")
    args = ap.parse_args()

    for p in (args.canary, args.ours):
        if not os.path.exists(p):
            sys.exit(f"{p} does not exist")

    canary = read_canary(args.canary)
    ours = read_ours(args.ours)
    shared = sorted(set(canary) & set(ours))

    print(f"canary {len(canary)} settings, ours {len(ours)} cvars, "
          f"{len(shared)} shared\n")

    diffs = [(n, canary[n], ours[n]) for n in shared
             if normalise(canary[n]) != normalise(ours[n])]

    # Keybinds differ by design and drown everything else out.
    diffs = [d for d in diffs if not d[0].startswith("keybind")]

    print(f"{len(diffs)} shared setting(s) differ:\n")
    print(f"   {'setting':<44} {'canary (works)':<22} ours")
    for name, c, o in diffs:
        print(f"   {name:<44} {c[:20]:<22} {o[:24]}")

    if args.all:
        print("\nonly canary has:", ", ".join(sorted(set(canary) - set(ours))[:30]))
        print("\nonly ours has:  ", ", ".join(sorted(set(ours) - set(canary))[:30]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
