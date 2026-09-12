"""Find where the code at a disc address landed in the title-updated image.

The update re-links the executable, so every address moves, but the code
itself mostly does not: a window of instructions from the disc image matches
one window in the patched image, with the position-dependent fields masked -
branch displacements, and the immediates of lis/addi/ori that materialise
addresses. Scores every 4-aligned position of the patched .text against the
window and reports the best.

    python tools/relocate.py 0x83000200 [--words 48] [--top 3]
    python tools/relocate.py 0x82B9C8E8 0x8238DF58 0x8233AEB4

The disc image is assets/default.xex (decoded by xex_image); the patched one
is out/guest_image_tu1.bin (FABLE2_DUMP_IMAGE). Override with --disc/--patched.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xex_image import XexImage  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def masked(word):
    """The instruction with its relocatable field zeroed, or None for a
    wildcard (a branch, whose displacement depends on position)."""
    op = word >> 26
    if op in (18, 16):          # b / bc: displacement is position-dependent
        return None
    if op in (15, 14, 24):      # lis / addi / ori: address halves move
        return word & 0xFFFF0000
    return word


def load(path, patched):
    if patched:
        return XexImage.load_raw(path)
    os.environ.pop("FABLE2_IMAGE", None)
    return XexImage.load(path)


def text_range(img):
    for name, va, size in img.sections:
        if name == ".text":
            return va, size
    raise SystemExit("no .text section")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addresses", nargs="+")
    ap.add_argument("--words", type=int, default=48, help="window size in instructions")
    ap.add_argument("--before", type=int, default=8, help="instructions before the address")
    ap.add_argument("--top", type=int, default=3)
    ap.add_argument("--disc", default=os.path.join(ROOT, "assets", "default.xex"))
    ap.add_argument("--patched", default=os.path.join(ROOT, "out", "guest_image_tu1.bin"))
    args = ap.parse_args()

    disc = load(args.disc, False)
    new = load(args.patched, True)
    tva, tsize = text_range(new)
    tsize &= ~3
    new_words = struct.unpack_from(">%dI" % (tsize // 4), new.data, new.offset(tva))

    for a in args.addresses:
        addr = int(a, 16)
        start = addr - args.before * 4
        window = [masked(disc.word(start + i * 4)) for i in range(args.words)]
        fixed = [(i, w) for i, w in enumerate(window) if w is not None]
        if not fixed:
            print(f"0x{addr:08X}: window is all branches; nothing to match")
            continue
        # First pass on a few anchor words, full score only where they hit.
        anchors = fixed[: min(6, len(fixed))]
        best = []
        n = len(new_words)
        for pos in range(0, n - args.words):
            ok = True
            for i, w in anchors:
                if (masked(new_words[pos + i]) or 0) != w:
                    ok = False
                    break
            if not ok:
                continue
            score = sum(1 for i, w in fixed if masked(new_words[pos + i]) == w)
            best.append((score, pos))
        best.sort(reverse=True)
        print(f"0x{addr:08X} (disc): {len(fixed)} fixed words of {args.words}")
        if not best:
            print("   no candidate shares the anchor words")
            continue
        for score, pos in best[: args.top]:
            new_addr = tva + pos * 4 + args.before * 4
            print(f"   -> 0x{new_addr:08X}  score {score}/{len(fixed)}"
                  f"  (shift {new_addr - addr:+#x})")
            if score < len(fixed):
                diffs = [i for i, w in fixed if masked(new_words[pos + i]) != w]
                print(f"      differing words at offsets: {', '.join(f'{(i - args.before) * 4:+d}' for i in diffs[:8])}")


if __name__ == "__main__":
    main()
