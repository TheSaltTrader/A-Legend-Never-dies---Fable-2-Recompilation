"""src_split.py: for a few (register, device offset) pairs from p3_sources.py's vote, the agreement of the draw's
register value with the device word at the exit of the innermost call whose range holds the draw packet - split by the
draw's execution ordinal (0 = first execution in the frame, >=1 = tile-pass replays) and by bin select (0x2A... etc
is not in p3_fe.bin; ordinal stands in)."""
import bisect, collections, struct, sys
PHYS = 0x1FFFFFFF
fe = open("p3_fe.bin", "rb").read()
REC = 16 + 0x400 * 4 + 0x928 * 4
draws = []
for i in range(0, len(fe) - REC + 1, REC):
    addr, issuer, ordn, fef = struct.unpack_from("<4I", fe, i)
    r2 = struct.unpack_from("<%dI" % 0x400, fe, i + 16)
    r4 = struct.unpack_from("<%dI" % 0x928, fe, i + 16 + 0x400 * 4)
    draws.append((addr & PHYS, ordn, fef, r2, r4))
g = open("p3_guest.bin", "rb").read()
DEV = 0x5000
calls = []
o = 0
while o + 24 + DEV <= len(g):
    hook, ci, co, c2i, c2o = struct.unpack_from("<5I", g, o)
    n = struct.unpack_from("<I", g, o + 20 + DEV)[0]
    if co != ci and ci and co:
        calls.append(((ci & PHYS) + 4, (co & PHYS) + 4, hook, o + 20))
    o += 24 + DEV + n
calls.sort()
los = [c[0] for c in calls]


def inner(addr):
    best = None
    k = bisect.bisect_right(los, addr)
    for j in range(max(0, k - 400), k):
        lo, hi, hook, off = calls[j]
        if lo <= addr < hi and (best is None or hi - lo < best[1] - best[0]):
            best = calls[j]
    return best


PAIRS = [(0x2000, 0x2880), (0x2082, 0x28C8), (0x2200, 0x2934), (0x2204, 0x2944), (0x414C, 0x2070), (0x4803, 0x048C)]
stat = collections.defaultdict(lambda: [0, 0])
fef_seen = collections.Counter()
for addr, ordn, fef, r2, r4 in draws:
    c = inner(addr)
    if not c:
        continue
    fef_seen[fef] += 1
    for reg, doff in PAIRS:
        v = r2[reg - 0x2000] if reg < 0x4000 else r4[reg - 0x4000]
        d = struct.unpack_from(">I", g, c[3] + doff)[0]
        key = (reg, "ord0" if ordn == 0 else "ord1+", fef)
        stat[key][0] += 1
        stat[key][1] += (v == d)
print("fe frames among joined draws:", dict(fef_seen))
for reg, doff in PAIRS:
    for grp in ("ord0", "ord1+"):
        for fef in sorted(fef_seen):
            n, ok = stat[(reg, grp, fef)]
            if n:
                print("0x%04X dev+0x%04X %-5s fe-frame %d: %5d draws, %5.1f%% agree" % (reg, doff, grp, fef, n, 100.0 * ok / n))
