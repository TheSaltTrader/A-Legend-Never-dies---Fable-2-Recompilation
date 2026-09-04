"""Diff one C++ function between the ReXGlue SDK and Xenia Canary.

    python tools/port_diff.py Update
    python tools/port_diff.py --list

ReXGlue's GPU code is a REFACTORED fork of Canary, so `git diff` is useless
here: different namespaces (`rex::` vs `xe::`), different file layout
(`src/graphics/pipeline/render_target/cache.cpp` vs
`src/xenia/gpu/render_target_cache.cc`), rex logging and rex cvars. A port has
to be done function by function, by hand, reading both.

This extracts the same function from both trees and prints a normalised diff so
the *logic* difference is visible instead of the renaming. Normalisation only
affects what is DISPLAYED for comparison - never what gets pasted into the
source.

Why the shared render-target cache first: the flat-blue scene reproduces on
BOTH the D3D12 and Vulkan backends, so the defect is in code they share.
"""

import argparse
import difflib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OURS = os.path.join(ROOT, "..", "rexglue-src", "src", "graphics", "pipeline",
                    "render_target", "cache.cpp")
CANARY = os.path.join(ROOT, "..", "..", "Ninja Gaiden 2 Xbox360",
                      "xenia-canary-src", "src", "xenia", "gpu",
                      "render_target_cache.cc")
CLASS = "RenderTargetCache"


def extract(path, cls, name):
    """The full text of `cls::name`, braces balanced, or None."""
    text = open(path, encoding="utf-8", errors="replace").read()
    for m in re.finditer(rf"\b{cls}::{name}\s*\(", text):
        # Walk back to the start of the declaration for the return type.
        start = text.rfind("\n", 0, m.start())
        start = text.rfind("\n", 0, start) if text[start + 1:m.start()].strip() == "" else start
        i = text.find("{", m.end())
        if i < 0:
            continue
        depth, j = 0, i
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        return text[start + 1:j + 1]
    return None


def normalise(text):
    """Strip the renaming so real logic differences stand out."""
    out = []
    for line in text.splitlines():
        s = line
        s = re.sub(r"\bxe::", "", s)
        s = re.sub(r"\brex::", "", s)
        s = re.sub(r"\bcvars::", "", s)
        s = re.sub(r"\bXELOGE?\b", "LOG", s)
        s = re.sub(r"\bREXLOG_[A-Z]+\b", "LOG", s)
        s = re.sub(r"\bXE_[A-Z_]+\b", "ASSERT", s)
        s = re.sub(r"\bREX_[A-Z_]+\b", "ASSERT", s)
        s = re.sub(r"\s+", " ", s).strip()
        if s:
            out.append(s)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("function", nargs="?")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--ours", default=OURS)
    ap.add_argument("--canary", default=CANARY)
    ap.add_argument("--cls", default=CLASS)
    ap.add_argument("--raw", action="store_true",
                    help="print Canary's version verbatim, ready to port from")
    args = ap.parse_args()

    for p in (args.ours, args.canary):
        if not os.path.exists(p):
            sys.exit(f"{p} does not exist")

    if args.list or not args.function:
        a = set(re.findall(rf"{args.cls}::([A-Za-z_]+)\s*\(",
                           open(args.ours, encoding="utf-8", errors="replace").read()))
        b = set(re.findall(rf"{args.cls}::([A-Za-z_]+)\s*\(",
                           open(args.canary, encoding="utf-8", errors="replace").read()))
        print("only in canary:", ", ".join(sorted(b - a)) or "(none)")
        print("only in ours:  ", ", ".join(sorted(a - b)) or "(none)")
        print("in both:       ", ", ".join(sorted(a & b)))
        return 0

    ours = extract(args.ours, args.cls, args.function)
    canary = extract(args.canary, args.cls, args.function)
    if canary is None:
        sys.exit(f"{args.function} not found in Canary")
    if args.raw or ours is None:
        if ours is None:
            print(f"// {args.function} does not exist in the SDK - "
                  f"Canary's version follows verbatim\n")
        print(canary)
        return 0

    diff = difflib.unified_diff(normalise(ours), normalise(canary),
                                fromfile=f"ours/{args.function}",
                                tofile=f"canary/{args.function}", lineterm="", n=3)
    n = 0
    for line in diff:
        print(line)
        n += 1
    if n == 0:
        print(f"{args.function}: identical after normalisation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
