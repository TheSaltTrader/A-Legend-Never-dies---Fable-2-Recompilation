"""src_const.py FRAME: for the per-draw registers of p3_src_full (read from src_full.txt), on the clean population
(draws decoded in FRAME and joined to the innermost writing call): the share that are LOAD_ALU_CONSTANT-loaded
(p3_fe_src.bin source != 0), and for those, where the source address sits relative to the writing call's arguments
r3-r10 (equal to an argument, inside [arg, arg+64 KB), or unrelated) and relative to the device object."""
import bisect, collections, re, struct, sys
import numpy as np
PHYS = 0x1FFFFFFF
CF = int(sys.argv[1])
per = [int(m.group(1), 16) for m in re.finditer(r"^0x([0-9A-F]{4})\s+PER-DRAW", open(sys.argv[2]).read(), re.M)]
per4 = [r for r in per if r >= 0x4000]
fe = open("p3_fe.bin", "rb").read()
fs = open("p3_fe_src.bin", "rb").read()
REC = 16 + 0x400 * 4 + 0x928 * 4
SREC = 0x928 * 4
src = open("p3_src.bin", "rb").read()
calls = {}
off = 0
while off + 52 <= len(src):
    hdr = struct.unpack_from("<13I", src, off)
    hook, tid, ci, co = hdr[:4]
    a8 = hdr[4:12]
    n = hdr[12]
    off += 52 + n * (8 + 256)
    if co != ci and ci and co:
        calls[((ci & PHYS) + 4, (co & PHYS) + 4, hook)] = a8
keys = sorted(calls)
los = [k[0] for k in keys]


def inner(addr):
    best = None
    k = bisect.bisect_right(los, addr)
    for j in range(max(0, k - 400), k):
        lo, hi, hook = keys[j]
        if lo <= addr < hi and (best is None or hi - lo < best[1] - best[0]):
            best = keys[j]
    return best


dev = 0x44142480 & PHYS
tot = collections.Counter()
mem = collections.Counter()
rel = collections.defaultdict(collections.Counter)
srcs = collections.defaultdict(collections.Counter)
nd = 0
for di, i in enumerate(range(0, len(fe) - REC + 1, REC)):
    addr, issuer, ordn, fef = struct.unpack_from("<4I", fe, i)
    if fef != CF:
        continue
    c = inner(addr & PHYS)
    if not c:
        continue
    nd += 1
    s4 = struct.unpack_from("<%dI" % 0x928, fs, di * SREC)
    a8 = [a & PHYS for a in calls[c]]
    for r in per4:
        tot[r] += 1
        sa = s4[r - 0x4000]
        if not sa:
            continue
        mem[r] += 1
        sa &= PHYS
        srcs[r][sa >> 12] += 1
        how = "unrelated"
        for k, a in enumerate(a8):
            if a == sa:
                how = "= r%d" % (k + 3); break
            if a <= sa < a + 0x10000:
                how = "in [r%d, +64K)" % (k + 3); break
        if how == "unrelated" and dev <= sa < dev + 0x5000:
            how = "device +0x%X" % (sa - dev)
        rel[r][how] += 1
print("clean draws:", nd)
for r in per4:
    if not tot[r]:
        continue
    top = ", ".join("%s %d" % kv for kv in rel[r].most_common(3))
    pages = len(srcs[r])
    print("0x%04X  memory-loaded %5.1f%%  source pages %4d  | %s" % (r, 100.0 * mem[r] / tot[r], pages, top))
