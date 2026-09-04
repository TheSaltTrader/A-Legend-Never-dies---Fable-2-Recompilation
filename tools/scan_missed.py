"""Find function-pointer targets the static analyzer never saw.

Registering these one crash at a time works but costs a rebuild per function.
Almost all of them are reachable statically if you look in the right places:

  * .rdata / .data hold vtables and dispatch tables - runs of code pointers.
  * .text materializes addresses inline as `lis rX, hi` + `addi rX, rX, lo`
    (or `ori`), which is how a function pointer gets stored into an object.

Anything those turn up that is not already a known function start is a
candidate. Candidates are validated before being written out: the target has
to be 4-aligned, live in an executable section, decode as a plausible entry,
and have a body that terminates cleanly.

    python tools/scan_missed.py                # report only
    python tools/scan_missed.py --write        # append to config/functions.toml
"""

import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xex_image import XexImage, pdata_functions  # noqa: E402
from add_function import function_extent, existing_addresses, describe  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTER_CPP = os.path.join(ROOT, "generated", "default", "ng2_register.cpp")


def known_functions(path=REGISTER_CPP):
    """Function starts codegen already emitted."""
    if not os.path.exists(path):
        raise SystemExit(f"{path} not found - run rexglue codegen first")
    text = open(path, encoding="utf-8", errors="replace").read()
    return {int(m, 16) for m in re.findall(r"SetFunction\(0x([0-9A-Fa-f]+),", text)}


def code_ranges(img):
    """(start, end) of every executable section."""
    out = []
    for name, va, size in img.sections:
        if name == ".text" or name.startswith(".embsec"):
            out.append((va, va + size))
    return out


def in_code(ranges, va):
    return any(lo <= va < hi for lo, hi in ranges)


def scan_data_pointers(img, ranges):
    """Code pointers sitting in .rdata / .data."""
    hits = collections.Counter()
    for name, va, size in img.sections:
        if name not in (".rdata", ".data"):
            continue
        base = img.offset(va)
        blob = img.data[base:base + size]
        for i in range(0, len(blob) - 3, 4):
            w = struct.unpack_from(">I", blob, i)[0]
            if w & 3:
                continue
            if in_code(ranges, w):
                hits[w] += 1
    return hits


def scan_inline_addresses(img, ranges):
    """`lis rX, hi` followed by `addi/ori rX, rX, lo` materializing a code
    address. The pair need not be adjacent, so track the last lis per
    register within a short window."""
    hits = collections.Counter()
    for lo, hi in ranges:
        pending = {}  # reg -> (hi_value, addr)
        for addr in range(lo, hi, 4):
            w = struct.unpack_from(">I", img.data, img.offset(addr))[0]
            op = (w >> 26) & 0x3F
            if op == 15:  # addis / lis
                rt = (w >> 21) & 0x1F
                ra = (w >> 16) & 0x1F
                if ra == 0:  # lis: ra==0 means "load immediate shifted"
                    pending[rt] = ((w & 0xFFFF) << 16, addr)
                else:
                    pending.pop(rt, None)
            elif op in (14, 24):  # addi (14) / ori (24)
                if op == 14:
                    rt, ra, imm = (w >> 21) & 0x1F, (w >> 16) & 0x1F, w & 0xFFFF
                    if imm & 0x8000:
                        imm -= 0x10000
                else:
                    ra, rt, imm = (w >> 21) & 0x1F, (w >> 16) & 0x1F, w & 0xFFFF
                src = pending.get(ra)
                if src and addr - src[1] <= 0x80:
                    target = (src[0] + imm) & 0xFFFFFFFF
                    if not (target & 3) and in_code(ranges, target):
                        hits[target] += 1
                if op == 14 and rt != ra:
                    pending.pop(rt, None)
    return hits


def plausible_entry(img, va):
    """Reject obvious non-entries: padding, mid-instruction, data."""
    w = img.word(va)
    if w in (0, 0xFFFFFFFF):
        return False
    try:
        size = function_extent(img, va)
    except SystemExit:
        return False
    return 4 <= size <= 0x4000


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xex", default=os.path.join(ROOT, "assets", "default.xex"))
    ap.add_argument("--toml", default=os.path.join(ROOT, "config", "functions.toml"))
    ap.add_argument("--write", action="store_true", help="append candidates to the TOML")
    ap.add_argument("--min-refs", type=int, default=1,
                    help="only take candidates referenced at least this often")
    args = ap.parse_args()

    img = XexImage.load(args.xex)
    ranges = code_ranges(img)
    known = known_functions()
    already = existing_addresses(args.toml)
    pdata = set(pdata_functions(img))

    print(f"known function starts: {len(known):,}   .pdata entries: {len(pdata):,}")

    data_hits = scan_data_pointers(img, ranges)
    inline_hits = scan_inline_addresses(img, ranges)
    print(f"code pointers in .rdata/.data: {len(data_hits):,} distinct")
    print(f"inline lis/addi code addresses: {len(inline_hits):,} distinct")

    combined = collections.Counter()
    combined.update(data_hits)
    combined.update(inline_hits)

    unknown = {a: n for a, n in combined.items()
               if a not in known and a not in already and n >= args.min_refs}
    print(f"referenced but not a known function start: {len(unknown):,}")

    good, rejected = [], 0
    for addr in sorted(unknown):
        if plausible_entry(img, addr):
            good.append(addr)
        else:
            rejected += 1
    print(f"  plausible entries: {len(good):,}   rejected: {rejected:,}")

    if good:
        sizes = [function_extent(img, a) for a in good]
        print(f"  sizes: min {min(sizes)}  median {sorted(sizes)[len(sizes)//2]}  "
              f"max {max(sizes)}")
        print("  sample:")
        for a in good[:10]:
            print(f"    0x{a:08X}  size 0x{function_extent(img, a):X}  "
                  f"refs {combined[a]}")

    if args.write and good:
        with open(args.toml, "a", encoding="utf-8") as f:
            f.write(f"\n# --- bulk scan: {len(good)} function-pointer targets the\n"
                    f"# static pass could not follow (tools/scan_missed.py) ---\n")
            for addr in good:
                size = function_extent(img, addr)
                f.write(f'0x{addr:08X} = {{ name = "sub_{addr:08X}_ptr", '
                        f"size = 0x{size:X} }}\n")
        print(f"\nappended {len(good)} entries to {args.toml}")
    elif good:
        print("\n(dry run - pass --write to append)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
