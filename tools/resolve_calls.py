"""Run codegen, register every function it could not resolve, repeat.

`rexglue codegen` fails validation with

    UnresolvedCall (8):
      0x82CD7948 from 0x82CD795C: b 0x82CD7948 ... - target not in any function

for each branch whose target sits inside another function's body. Those targets
are almost always .pdata-less helpers (adjustor thunks, one-line getters,
forwarders) that got absorbed into the preceding function. Registering one
often exposes the next, because a newly-registered forwarder's own `b` target
is now a call from a real function - so this has to be iterated to a fixpoint
rather than done once.

    python tools/resolve_calls.py [--max-rounds 20] [--dry-run]

Each round appends to config/functions.toml via add_function.py's logic and
re-runs codegen. Exits 0 when codegen succeeds, 1 if it stops making progress.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import add_function  # noqa: E402
from xex_image import XexImage, pdata_functions  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = "fable2_manifest.toml"
TOML = os.path.join("config", "functions.toml")

# "  0x82CD7948 from 0x82CD795C: ..." - the first address is the target.
UNRESOLVED = re.compile(r"^\s*0x([0-9A-Fa-f]{8}) from 0x([0-9A-Fa-f]{8}):", re.M)


def rexglue():
    sdk = os.environ.get("REXSDK")
    if not sdk:
        sys.exit("REXSDK is not set - point it at the ReXGlue SDK root.")
    return os.path.join(sdk, "bin", "rexglue.exe")


def run_codegen():
    proc = subprocess.run([rexglue(), "codegen", MANIFEST],
                          cwd=ROOT, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def register(img, pdata, known, addr, source):
    if addr in known or not img.contains(addr):
        return False
    size = add_function.function_extent(img, addr)
    section = img.section_of(addr) or "?"

    import bisect
    i = bisect.bisect_right(pdata, addr) - 1
    owner = pdata[i] if i >= 0 else 0
    note = (f"absorbed into pdata fn 0x{owner:08X}"
            if owner and owner != addr else "no pdata entry")

    with open(os.path.join(ROOT, TOML), "a", encoding="utf-8") as f:
        f.write(f"\n# {section}, {note}; branched to from 0x{source:08X}.\n")
        for line in add_function.describe(img, addr, size):
            f.write(line + "\n")
        f.write(f'0x{addr:08X} = {{ name = "sub_{addr:08X}_missed", '
                f"size = 0x{size:X} }}\n")
    known.add(addr)
    print(f"  + 0x{addr:08X}  size 0x{size:<5X} ({note})")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--max-rounds", type=int, default=25)
    ap.add_argument("--dry-run", action="store_true",
                    help="report what round 1 would add, change nothing")
    args = ap.parse_args()

    img = XexImage.load(os.path.join(ROOT, "assets", "default.xex"))
    pdata = pdata_functions(img)

    total = 0
    for round_no in range(1, args.max_rounds + 1):
        code, log = run_codegen()
        if code == 0:
            print(f"round {round_no}: codegen succeeded "
                  f"({total} function(s) registered in total)")
            return 0

        targets = [(int(t, 16), int(s, 16)) for t, s in UNRESOLVED.findall(log)]
        if not targets:
            print(f"round {round_no}: codegen failed for a reason that is not "
                  f"an unresolved call:")
            print(log[-2000:])
            return 1

        known = add_function.existing_addresses(os.path.join(ROOT, TOML))
        print(f"round {round_no}: {len(targets)} unresolved call(s)")
        if args.dry_run:
            for t, s in targets:
                print(f"    0x{t:08X} from 0x{s:08X}")
            return 0

        added = 0
        for target, source in targets:
            if register(img, pdata, known, target, source):
                added += 1
        total += added
        if added == 0:
            print("  no progress - every target is already registered or "
                  "outside the image; needs a human")
            return 1

    print(f"gave up after {args.max_rounds} rounds")
    return 1


if __name__ == "__main__":
    sys.exit(main())
