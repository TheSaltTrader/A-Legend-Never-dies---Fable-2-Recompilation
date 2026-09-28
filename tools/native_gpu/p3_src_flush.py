"""src_flush.py FRAME: each clean draw joined to its writing call (p3_src.bin ranges); if that call is a flush (hook 64 /
65) with an entry snapshot (p3_flush_entry.bin, keyed by hook + cursor at entry), compare the draw's VS c0-c63 and PS
c0-c63 with the device shadow at the flush ENTRY (+0x780 VS, +0x1780 PS)."""
import bisect, collections, struct, sys
PHYS = 0x1FFFFFFF
CF = int(sys.argv[1])
fe = open("p3_fe.bin", "rb").read()
REC = 16 + 0x400 * 4 + 0x928 * 4
src = open("p3_src.bin", "rb").read()
calls = []
off = 0
while off + 52 <= len(src):
    hdr = struct.unpack_from("<13I", src, off)
    hook, tid, ci, co = hdr[:4]
    n = hdr[12]
    off += 52 + n * 264
    if co != ci and ci and co:
        calls.append(((ci & PHYS) + 4, (co & PHYS) + 4, hook, ci))
calls.sort()
los = [c[0] for c in calls]
fl = open("p3_flush_entry.bin", "rb").read()
snap = {}
for o in range(0, len(fl) - 12 - 0x2000 + 1, 12 + 0x2000):
    hook, tid, ci = struct.unpack_from("<3I", fl, o)
    snap[(hook, ci)] = o + 12


def inner(addr):
    best = None
    k = bisect.bisect_right(los, addr)
    for j in range(max(0, k - 400), k):
        c = calls[j]
        if c[0] <= addr < c[1] and (best is None or c[1] - c[0] < best[1] - best[0]):
            best = c
    return best


agree = collections.Counter()
tot = collections.Counter()
nd = nsnap = 0
for i in range(0, len(fe) - REC + 1, REC):
    addr, issuer, ordn, fef = struct.unpack_from("<4I", fe, i)
    if fef != CF:
        continue
    c = inner(addr & PHYS)
    if not c:
        continue
    nd += 1
    so = snap.get((c[2], c[3]))
    if so is None:
        continue
    nsnap += 1
    base = i + 16 + 0x400 * 4
    for blk, sh in ((0x000, 0x0000), (0x100, 0x1000)):
        for k in range(0, 64 * 4):
            reg = 0x4000 + blk * 4 + k if blk == 0 else 0x4400 + k
            v = struct.unpack_from("<I", fe, base + (reg - 0x4000) * 4)[0]
            d = struct.unpack_from(">I", fl, so + sh + k * 4)[0]
            tot[reg] += 1
            agree[reg] += (v == d)
print("clean draws %d, with a flush-entry snapshot %d" % (nd, nsnap))
for name, lo in (("VS", 0x4000), ("PS", 0x4400)):
    line = []
    for c4 in range(0, 64):
        regs = [lo + c4 * 4 + j for j in range(4)]
        t = sum(tot[r] for r in regs)
        if t:
            line.append("c%d %.0f%%" % (c4, 100.0 * sum(agree[r] for r in regs) / t))
    print(name, ": ", ", ".join(line))
