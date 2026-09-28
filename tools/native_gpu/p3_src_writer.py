"""src_writer.py FRAME SRC_FULL_TXT: every PER-DRAW register of the clean population joined to the call that wrote the
packet which last set it (p3_fe_wpkt.bin), and its value searched in that call's r3-r10 (immediate) and in the 256-byte
samples behind its pointer arguments (big-endian dwords): the vote is (writer hook, where) - where = rN or
[rN+off]. Memory-loaded registers (p3_fe_src.bin != 0) are reported apart (their source is an address, not a call)."""
import bisect, collections, re, struct, sys
PHYS = 0x1FFFFFFF
CF = int(sys.argv[1])
per = [int(m.group(1), 16) for m in re.finditer(r"^0x([0-9A-F]{4})\s+PER-DRAW", open(sys.argv[2]).read(), re.M)]
fe = open("p3_fe.bin", "rb").read()
fs = open("p3_fe_src.bin", "rb").read()
fw = open("p3_fe_wpkt.bin", "rb").read()
REC = 16 + 0x400 * 4 + 0x928 * 4
SREC = 0x928 * 4
WREC = (0x400 + 0x928) * 4
src = open("p3_src.bin", "rb").read()
calls = []
off = 0
while off + 52 <= len(src):
    hdr = struct.unpack_from("<13I", src, off)
    hook, tid, ci, co = hdr[:4]
    a8 = hdr[4:12]
    n = hdr[12]
    off += 52
    samples = []
    for _ in range(n):
        k, va = struct.unpack_from("<2I", src, off)
        samples.append((k, struct.unpack_from(">64I", src, off + 8)))
        off += 8 + 256
    if co != ci and ci and co:
        calls.append(((ci & PHYS) + 4, (co & PHYS) + 4, hook, a8, samples))
calls.sort(key=lambda c: c[0])
los = [c[0] for c in calls]


def inner(addr):
    best = None
    k = bisect.bisect_right(los, addr)
    for j in range(max(0, k - 400), k):
        c = calls[j]
        if c[0] <= addr < c[1] and (best is None or c[1] - c[0] < best[1] - best[0]):
            best = c
    return best


votes = collections.defaultdict(collections.Counter)
tot, memld, nowriter = collections.Counter(), collections.Counter(), collections.Counter()
nd = 0
for di, i in enumerate(range(0, len(fe) - REC + 1, REC)):
    addr, issuer, ordn, fef = struct.unpack_from("<4I", fe, i)
    if fef != CF or not inner(addr & PHYS):
        continue
    nd += 1
    for r in per:
        j = r - 0x2000 if r < 0x4000 else 0x400 + (r - 0x4000)
        v = struct.unpack_from("<I", fe, i + 16 + j * 4)[0]
        tot[r] += 1
        if r >= 0x4000 and struct.unpack_from("<I", fs, di * SREC + (r - 0x4000) * 4)[0]:
            memld[r] += 1
            continue
        wp = struct.unpack_from("<I", fw, di * WREC + j * 4)[0] & PHYS
        c = inner(wp) if wp else None
        if not c:
            nowriter[r] += 1
            continue
        hook, a8, samples = c[2], c[3], c[4]
        found = None
        for k, a in enumerate(a8):
            if a == v and v not in (0, 1, 0xFFFFFFFF):
                found = "r%d" % (k + 3); break
        if not found:
            for k, words in samples:
                if v in words and v not in (0, 1, 0xFFFFFFFF, 0x3F800000):
                    found = "[r%d+0x%X]" % (k + 3, words.index(v) * 4); break
        votes[r][(hook, found or "not in args/samples")] += 1
print("clean draws:", nd)
for r in per:
    if not tot[r]:
        continue
    top = votes[r].most_common(2)
    t = "; ".join("hook%d %s %d" % (h, w, n) for (h, w), n in top)
    print("0x%04X  n %5d  mem %5d  no-writer %5d | %s" % (r, tot[r], memld[r], nowriter[r], t))
