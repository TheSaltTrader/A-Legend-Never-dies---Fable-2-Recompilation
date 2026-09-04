"""Locate the CRT's _setjmp and longjmp in the guest image.

The recompiler has to model these specially - a longjmp must transfer control
rather than return - and getting it wrong is quiet. On Ninja Gaiden II the
missing pair turned "abort this resource load" into "return normally and carry
on with invalid state", which overran a stack buffer and crashed minutes later
on a garbage pointer, miles from the cause. So find them before running, not
after.

Both are recognisable by shape rather than by name (the XEX is stripped):

  _setjmp   saves LR, CR, r13-r31 and f14-f31 (and on Xbox 360 the VMX128
            non-volatiles) into the buffer in r3, then `li r3, 0; blr`.
  longjmp   is the mirror - it *loads* that same set out of r3 - and does not
            return to its caller: it ends by restoring LR and branching.

So: score every 4-byte-aligned window on how many stores-to-r3 (setjmp) or
loads-from-r3 (longjmp) it contains, and report the densest.

    python tools/find_setjmp.py [--top 10]

_setjmp usually has no .pdata entry of its own - it sits inside longjmp's
range - which is exactly why it has to be named explicitly in the manifest.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xex_image import XexImage, pdata_functions  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BLR = 0x4E800020
WINDOW = 96 * 4  # bytes; comfortably larger than either routine


def rA(w):
    return (w >> 16) & 0x1F


def is_store_to(w, reg):
    """std / stfd / stw / stvx-family store based on `reg`."""
    op = w >> 26
    if op == 62 and (w & 3) == 0 and rA(w) == reg:      # std
        return True
    if op in (54, 55) and rA(w) == reg:                  # stfd / stfdu
        return True
    if op == 36 and rA(w) == reg:                        # stw
        return True
    if op == 31 and rA(w) == reg and ((w >> 1) & 0x3FF) in (231, 487, 199, 359):
        return True                                      # stvx / stvxl / stvebx
    return False


def is_load_from(w, reg):
    op = w >> 26
    if op == 58 and (w & 3) == 0 and rA(w) == reg:       # ld
        return True
    if op in (50, 51) and rA(w) == reg:                  # lfd / lfdu
        return True
    if op == 32 and rA(w) == reg:                        # lwz
        return True
    if op == 31 and rA(w) == reg and ((w >> 1) & 0x3FF) in (103, 359, 39, 7):
        return True                                      # lvx / lvxl / lvebx
    return False


def scan(img, section_va, section_size, predicate):
    """Best-scoring window start for `predicate`, densest first."""
    base = img.offset(section_va)
    data = img.data
    n = section_size & ~3
    words = struct.unpack_from(f">{n // 4}I", data, base)

    hits = [1 if predicate(w) else 0 for w in words]
    span = WINDOW // 4
    running = sum(hits[:span])
    scored = [(running, 0)]
    for i in range(1, len(hits) - span):
        running += hits[i + span - 1] - hits[i - 1]
        scored.append((running, i))
    scored.sort(key=lambda t: -t[0])
    return [(score, section_va + i * 4) for score, i in scored]


def dedupe(candidates, limit):
    """Collapse overlapping windows - a good match scores well at every offset."""
    out = []
    for score, va in candidates:
        if any(abs(va - prev) < WINDOW for _, prev in out):
            continue
        out.append((score, va))
        if len(out) >= limit:
            break
    return out


def function_start(pdata, va):
    import bisect
    i = bisect.bisect_right(pdata, va) - 1
    return pdata[i] if i >= 0 else None


def show(img, pdata, label, candidates):
    from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN
    md = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
    print(f"\n=== {label} candidates ===")
    for score, va in candidates:
        owner = function_start(pdata, va)
        own = (f"pdata fn 0x{owner:08X}"
               + ("  <-- IS the function start" if owner == va else
                  f" (+0x{va - owner:X} inside it)")) if owner else "no pdata owner"
        print(f"\n0x{va:08X}  score {score}  {own}")
        for addr in range(va - 8, va + 40, 4):
            if not img.contains(addr):
                continue
            w = img.word(addr)
            d = list(md.disasm(struct.pack(">I", w), addr))
            text = f"{d[0].mnemonic} {d[0].op_str}".strip() if d else f"<{w:08X}>"
            mark = "->" if addr == va else "  "
            print(f"  {mark} 0x{addr:08X}  {text}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xex", default=os.path.join(ROOT, "assets", "default.xex"))
    ap.add_argument("--top", type=int, default=4)
    args = ap.parse_args()

    img = XexImage.load(args.xex)
    pdata = pdata_functions(img)
    text = next(((va, size) for name, va, size in img.sections if name == ".text"), None)
    if not text:
        sys.exit("no .text section")

    saves = scan(img, text[0], text[1], lambda w: is_store_to(w, 3))
    loads = scan(img, text[0], text[1], lambda w: is_load_from(w, 3))

    show(img, pdata, "_setjmp (saves into r3)", dedupe(saves, args.top))
    show(img, pdata, "longjmp (restores from r3)", dedupe(loads, args.top))
    return 0


if __name__ == "__main__":
    sys.exit(main())
