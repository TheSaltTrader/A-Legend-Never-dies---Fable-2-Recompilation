import re, glob, os, numpy as np, datetime
OUT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\wt-fable2-nativegpu\out"
files = sorted(glob.glob(os.path.join(OUT, "SKYCONST1*.log")), key=lambda f: -int(re.search(r"\.(\d+)\.log$", f).group(1)) if re.search(r"\.(\d+)\.log$", f) else 0)
series = {}   # (reg, comp) -> list of (t, value)
for f in files:
    for l in open(f, encoding="utf-8", errors="replace"):
        if "INPUT CONSTS" not in l: continue
        m = re.match(r"\[\S+ (\d\d):(\d\d):(\d\d\.\d+)\]", l)
        t = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
        for reg, vals in re.findall(r" c(\d+)\([^)]*-> ([^)]*)\)", l):
            v = [float(x) for x in vals.split()]
            for k in range(4): series.setdefault((int(reg), k), []).append((t, v[k]))
caps = sorted(glob.glob(r"D:\fable2_flash\gameplay\SKYCONST1_series\s*\win0_wgc.png"))
tc = [os.path.getmtime(c) for c in caps]
tc = [datetime.datetime.fromtimestamp(x) for x in tc]
tc = [x.hour * 3600 + x.minute * 60 + x.second + x.microsecond / 1e6 for x in tc]
sky = np.array([23.963, 25.446, 25.957, 25.400, 19.401, 16.062, 16.299, 15.374, 16.721, 18.677])[:len(tc)]
res = []
for key, pts in series.items():
    pts.sort(); ts = np.array([p[0] for p in pts]); vs = np.array([p[1] for p in pts])
    samp = []
    for t in tc:
        i = np.searchsorted(ts, t - 1.0) - 1   # the value ~1 s before the capture's mtime
        samp.append(vs[max(i, 0)])
    samp = np.array(samp)
    if samp.std() < 1e-9: continue
    c = np.corrcoef(samp, sky)[0, 1]
    res.append((abs(c), c, key, samp.min(), samp.max()))
res.sort(reverse=True)
print("registers moving across the captures:", len(res), "(of", len(series), "components logged)")
for a, c, key, lo, hi in res[:12]:
    print(f"  c{key[0]}.{'xyzw'[key[1]]}: corr with native sky {c:+.3f}   range {lo:.5g} .. {hi:.5g}")
