#!/usr/bin/env python3
"""P3 register-map discovery: which XDK device-object word holds each GPU register at draw time.

Inputs (FABLE2_P3MAP run, fable2_p2_census.cpp):
  p3_guest.bin  records {u32 hook, cur_in, cur_out, cur2_in, cur2_out, u8 device[0x5000]} (device big-endian), one per
                guest call that advanced the ring cursor in the discovery frame
  p3_bridge.bin records {u32 packet_addr, u32 regs[0x2000..0x23FF], u32 regs[0x4000..0x4927]} (host order), one per
                bridge draw whose packet address fell inside a recorded call's range
Each bridge draw is joined to the guest call whose [cur_in, cur_out) holds its packet address (physical & 0x1FFFFFFF).
For every register the tool finds the device word offsets whose value EQUALS the register in every joined draw where
the register is not constant (a constant register matches anything; it is reported separately). Output: the map, the
registers with no device home, and how many joined draws each claim rests on.

Usage: p3_regmap.py --guest p3_guest.bin --bridge p3_bridge.bin [--json out.json]"""
import argparse, json, struct, sys
from collections import defaultdict

ap = argparse.ArgumentParser()
ap.add_argument("--guest", required=True)
ap.add_argument("--bridge", required=True)
ap.add_argument("--json", default=None)
args = ap.parse_args()

DEV = 0x5000
GREC = 20 + DEV
guest = open(args.guest, "rb").read()
calls = []
for off in range(0, len(guest) - GREC + 1, GREC):
    hook, ci, co, c2i, c2o = struct.unpack_from("<5I", guest, off)
    dev = struct.unpack_from(">%dI" % (DEV // 4), guest, off + 20)
    calls.append((ci & 0x1FFFFFFF, co & 0x1FFFFFFF, hook, dev))
print("guest calls: %d" % len(calls))

REGS = list(range(0x2000, 0x2400)) + list(range(0x4000, 0x4928))
BREC = 4 + 4 * len(REGS)
bridge = open(args.bridge, "rb").read()
draws = []
for off in range(0, len(bridge) - BREC + 1, BREC):
    (addr,) = struct.unpack_from("<I", bridge, off)
    vals = struct.unpack_from("<%dI" % len(REGS), bridge, off + 4)
    a = addr & 0x1FFFFFFF
    owner = None
    for c in calls:
        if c[0] <= a < c[1]:
            owner = c
            break
    if owner:
        draws.append((owner, vals))
print("bridge draws: %d, joined to a guest call: %d" % (len(bridge) // BREC, len(draws)))
if not draws:
    sys.exit("nothing joined")

# Registers that vary across the joined draws (a constant register is matched by any constant word - not evidence).
varying = []
for ri, r in enumerate(REGS):
    vs = set(d[1][ri] for d in draws)
    if len(vs) > 1:
        varying.append(ri)
print("registers varying across joined draws: %d of %d" % (len(varying), len(REGS)))

mapping = {}
nohome = []
QUORUM = 0.90   # a device word must equal the register in >= 90% of the joined draws (a few owners are mis-timed)
for ri in varying:
    votes = defaultdict(int)
    for owner, vals in draws:
        v = vals[ri]
        dev = owner[3]
        for i, w in enumerate(dev):
            if w == v:
                votes[i] += 1
    best = [i for i, c in votes.items() if c >= QUORUM * len(draws)]
    # Reject words that equal EVERY varying register often (e.g. a zero-heavy word): require the word itself to vary.
    best = [i for i in best if len(set(o[3][i] for o, _ in draws)) > 1]
    if best:
        mapping[REGS[ri]] = sorted(best)
    else:
        nohome.append(REGS[ri])

print("mapped: %d registers to device words; no device home: %d" % (len(mapping), len(nohome)))
# Summarise the mapping as runs: register block -> device offset block (same stride).
runs = []
for r in sorted(mapping):
    offs = mapping[r]
    if len(offs) == 1:
        o = offs[0] * 4
        if runs and runs[-1][1] + 1 == r and runs[-1][3] + 4 == o:
            runs[-1][1] = r
            runs[-1][3] = o
        else:
            runs.append([r, r, o, o])
print("unique mappings as runs (register range -> device byte offsets):")
for a, b, o1, o2 in runs:
    print("  0x%04X..0x%04X -> dev+0x%04X..0x%04X  (%d regs)" % (a, b, o1, o2, b - a + 1))
amb = {r: v for r, v in mapping.items() if len(v) > 1}
print("ambiguous (several device words always equal): %d, e.g. %s" %
      (len(amb), ", ".join("0x%04X:%s" % (r, [hex(x * 4) for x in v[:3]]) for r, v in list(amb.items())[:6])))
print("no device home (first 40): %s" % " ".join("0x%04X" % r for r in nohome[:40]))
if args.json:
    json.dump({"mapping": {hex(r): [o * 4 for o in v] for r, v in mapping.items()},
               "nohome": [hex(r) for r in nohome], "joined": len(draws), "calls": len(calls)},
              open(args.json, "w"), indent=1)
