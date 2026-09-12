"""Pre-cache step 1: what a region load's hitches are made of.

Reads out/fable2.log (or a path given), finds every "region '<name>' is
loading" line, and for the 60 s that follow buckets by 5 s:
  - graphics pipelines created (plugin debug line)
  - fence waits by reason (the [gpu] line: resolve readback / cache clear /
    frame pacing, count x total ms)
  - texture-pack uploads and re-uploads ([texpack] lines)
  - guest fps and hitch count ([swap] line)
so the dominant cost is a number, not a guess.
"""
import re, sys, collections

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\fable2recomp\out\fable2.log"
lines = open(path, encoding="utf-8", errors="replace").read().split("\n")

def ts(l):
    m = re.match(r"\[\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d\.\d+)\]", l)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else None

events = []   # (t, kind, payload)
for l in lines:
    t = ts(l)
    if t is None:
        continue
    if "is loading -> stage" in l:
        m = re.search(r"region '([^']+)'", l)
        events.append((t, "region", m.group(1) if m else "?"))
    elif "Creating graphics pipeline" in l:
        events.append((t, "pipeline", None))
    elif "[gpu] fence waits in" in l:
        m = re.findall(r"([a-z ]+?) (\d+) x ([\d.]+) ms", l)
        events.append((t, "fence", {k.strip(): (int(n), float(ms)) for k, n, ms in m}))
    elif "[texpack]" in l:
        m = re.search(r"\[texpack\] (.*)", l)
        events.append((t, "texpack", m.group(1)[:70]))
    elif "[swap]" in l:
        m = re.search(r"\[swap\] ([\d.]+) guest fps.*hitches (\d+)", l)
        if m:
            events.append((t, "swap", (float(m.group(1)), int(m.group(2)))))

regions = [(t, p) for t, k, p in events if k == "region"]
if not regions:
    sys.exit("no region load in this log")
for t0, name in regions:
    print(f"\n=== region '{name}' loading at +0.0 s ===")
    print("  bucket   pipelines   fence waits (reason: n x ms)                    texpack   fps/hitches")
    for b in range(0, 60, 5):
        lo, hi = t0 + b, t0 + b + 5
        pipes = sum(1 for t, k, _ in events if k == "pipeline" and lo <= t < hi)
        fences = [p for t, k, p in events if k == "fence" and lo <= t < hi]
        fsum = collections.defaultdict(lambda: [0, 0.0])
        for f in fences:
            for reason, (n, ms) in f.items():
                fsum[reason][0] += n; fsum[reason][1] += ms
        ftxt = ", ".join(f"{r}: {n} x {ms:.0f}" for r, (n, ms) in fsum.items() if n) or "-"
        tex = sum(1 for t, k, _ in events if k == "texpack" and lo <= t < hi)
        swaps = [p for t, k, p in events if k == "swap" and lo <= t < hi]
        stxt = " ".join(f"{fps:.0f}/{h}" for fps, h in swaps) or "-"
        print(f"  {b:2d}-{b+5:2d} s  {pipes:9d}   {ftxt:<52} {tex:7d}   {stxt}")
