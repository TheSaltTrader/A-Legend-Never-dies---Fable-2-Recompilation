import sys, glob, os, csv
import numpy as np
from PIL import Image
# A flash frame differs from BOTH neighbours while the neighbours resemble each other (a camera move changes gradually).
for t in sys.argv[1:]:
    d = f"D:/fable2_flash/ab110/logs/sweep/{t}_burst"
    fs = sorted(glob.glob(d + "/f*.jpg"), key=lambda p: int(os.path.basename(p)[1:-4]))
    times = {r["file"]: r["time"] for r in csv.DictReader(open(d + "/frames.csv"))}
    a = [np.asarray(Image.open(f).convert("RGB").resize((96, 40)), dtype=np.float32) for f in fs]
    hits = []
    for i in range(1, len(a) - 1):
        dp = np.abs(a[i] - a[i-1]).mean(); dn = np.abs(a[i] - a[i+1]).mean(); nn = np.abs(a[i-1] - a[i+1]).mean()
        if min(dp, dn) > 25 and nn < min(dp, dn) * 0.5:
            hits.append((os.path.basename(fs[i]), times.get(os.path.basename(fs[i]), "?"), round(min(dp, dn), 1)))
    print(t, "frames", len(a), "flash frames", len(hits), hits[:8])
