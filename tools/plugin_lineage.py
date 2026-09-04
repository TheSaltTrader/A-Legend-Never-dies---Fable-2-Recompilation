"""Work out which Xenia branch the SDK's GPU plugin came from, and what it lacks.

    python tools/plugin_lineage.py --master <xenia-src> --canary <xenia-canary-src>

The question this answers is a real one: Fable II plays fine on Xenia Canary and
renders the scene flat blue here, so is `rexgpu-xenos.dll` Canary-derived, and
is it missing something Canary has?

Method: every cvar our build actually registers (from FABLE2_DUMP_CVARS, i.e.
observed rather than assumed) is checked against the cvar definitions in each
Xenia checkout. A cvar that only exists on one branch is a fingerprint.
"""

import argparse
import os
import re
import sys

DEFINE = re.compile(
    r"DEFINE_(?:bool|int32|int64|uint32|uint64|double|string|path)\(\s*([A-Za-z0-9_]+)")


def ours(path):
    names, name = set(), None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("#"):
            continue
        if not line.startswith(" ") and line.strip():
            name = line.strip()
            names.add(name)
    return names


def defined_in(tree):
    """cvar name -> the file that defines it, for a Xenia checkout."""
    out = {}
    for base, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d != ".git"]
        for f in files:
            if not f.endswith((".cc", ".cpp", ".h")):
                continue
            p = os.path.join(base, f)
            try:
                text = open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            rel = os.path.relpath(p, tree).replace("\\", "/")
            for m in DEFINE.finditer(text):
                out.setdefault(m.group(1), rel)
    return out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ng2 = os.path.join(root, "..", "..", "Ninja Gaiden 2 Xbox360")
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--master", default=os.path.join(ng2, "xenia-src"))
    ap.add_argument("--canary", default=os.path.join(ng2, "xenia-canary-src"))
    ap.add_argument("--cvars", default=os.path.join(root, "out", "cvars.txt"))
    args = ap.parse_args()

    for p in (args.master, args.canary, args.cvars):
        if not os.path.exists(p):
            sys.exit(f"{p} does not exist")

    mine = ours(args.cvars)
    m = defined_in(args.master)
    c = defined_in(args.canary)
    only_c, only_m = set(c) - set(m), set(m) - set(c)

    print(f"our build registers {len(mine)} cvars")
    print(f"xenia master defines {len(m)}, canary defines {len(c)}\n")

    hit_c = sorted(mine & only_c)
    hit_m = sorted(mine & only_m)
    print(f"LINEAGE")
    print(f"   canary-only cvars we have: {len(hit_c)}")
    print(f"   master-only cvars we have: {len(hit_m)}")
    verdict = ("Canary-derived" if len(hit_c) > len(hit_m) * 2 else
               "master-derived" if len(hit_m) > len(hit_c) * 2 else "mixed")
    print(f"   -> {verdict}")
    if hit_m:
        print(f"   but it also keeps master-only names ({', '.join(hit_m)}),")
        print(f"   so it is an OLDER Canary snapshot, from around the rename.")

    missing = {k: v for k, v in c.items() if k not in mine}
    gpu = {k: v for k, v in missing.items() if "/gpu/" in v}
    print(f"\nCANARY GPU CVARS OUR PLUGIN DOES NOT HAVE: {len(gpu)}")
    for k, v in sorted(gpu.items()):
        print(f"   {k:<44} {v}")

    kernel = {k: v for k, v in missing.items()
              if "/kernel/" in v or "/cpu/" in v}
    print(f"\nCANARY CPU/KERNEL CVARS WE DO NOT HAVE: {len(kernel)}")
    for k, v in sorted(kernel.items())[:20]:
        print(f"   {k:<44} {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
