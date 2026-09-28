"""src_full.py: whole-register-file stage-2 census on the CLEAN population (draws decoded in the census frame, joined to
the innermost call whose cursor range holds the draw packet; the device object at that call's exit). For every register
that varies across these draws: the device word that equals it on the most draws (candidates from the first 64 draws,
then scored on all), its agreement, and the verdict: DEVICE (>= 95%), PARTIAL (80-95%), PER-DRAW (< 80% or none).
Usage (in the arm folder): src_full.py [census_frame]"""
import bisect, collections, struct, sys
import numpy as np
PHYS = 0x1FFFFFFF
CF = int(sys.argv[1]) if len(sys.argv) > 1 else 2500
fe = open("p3_fe.bin", "rb").read()
REC = 16 + 0x400 * 4 + 0x928 * 4
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


regs, devs, hooks = [], [], collections.Counter()
for i in range(0, len(fe) - REC + 1, REC):
    addr, issuer, ordn, fef = struct.unpack_from("<4I", fe, i)
    if fef != CF:
        continue
    c = inner(addr & PHYS)
    if not c:
        continue
    regs.append(np.frombuffer(fe, dtype="<u4", count=0x400 + 0x928, offset=i + 16))
    devs.append(np.frombuffer(g, dtype=">u4", count=DEV // 4, offset=c[3]).astype("<u4"))
    hooks[c[2]] += 1
R = np.stack(regs)   # n x 3368 (0x2000.., then 0x4000..)
D = np.stack(devs)   # n x 5120
n = len(R)
print("clean draws (fe frame %d, joined): %d; writing hooks: %s" % (CF, n, dict(hooks.most_common(6))))
names = [0x2000 + k for k in range(0x400)] + [0x4000 + k for k in range(0x928)]
verdict = collections.Counter()
rows = []
for j, reg in enumerate(names):
    col = R[:, j]
    if np.all(col == col[0]):
        verdict["constant"] += 1
        continue
    head = min(512, n)
    m = (D[:head] == col[:head, None]).sum(axis=0)   # agreement over the first 512 draws, every device word
    idx = np.argsort(-m)[:16]
    best, bshare = None, 0.0
    for k in idx:
        share = float(np.mean(D[:, k] == col))
        if share > bshare:
            best, bshare = k, share
    v = "DEVICE" if bshare >= 0.95 else "PARTIAL" if bshare >= 0.80 else "PER-DRAW"
    verdict[v] += 1
    rows.append((reg, best, bshare, v, len(np.unique(col))))
print("registers:", dict(verdict))
for reg, best, share, v, uniq in rows:
    if v != "DEVICE":
        print("0x%04X  %-8s  best dev+0x%04X %5.1f%%  (%d distinct values)" % (reg, v, (best or 0) * 4, 100 * share, uniq))
