import sys, glob, os, csv
import numpy as np
from PIL import Image
# Violet-foliage detector (the user's flash, 2026-09-28): pixels that jump for ONE frame and return, AND turn bluer/
# pinker than green (a normal map drawn as colour). Found the three flashes in error.mp4 where tile means could not.
def run(t, t0=None, t1=None):
    d = f"D:/fable2_flash/ab110/logs/sweep/{t}_burst"
    fs = sorted(glob.glob(d + "/f*.jpg"), key=lambda p: int(os.path.basename(p)[1:-4]))
    times = {r["file"]: r["time"] for r in csv.DictReader(open(d + "/frames.csv"))}
    a = [np.asarray(Image.open(f).convert("RGB"), dtype=np.int16) for f in fs]
    hits = []
    for i in range(1, len(a) - 1):
        x, p, n = a[i], a[i-1], a[i+1]
        flip = (np.abs(x - p).max(-1) > 50) & (np.abs(x - n).max(-1) > 50) & (np.abs(p - n).max(-1) < 25)
        viol = (x[..., 2] - x[..., 1]) - (p[..., 2] - p[..., 1])    # blue up vs green, against the frame before
        v = int((flip & (viol > 35)).sum())
        if v >= 12:
            hits.append((times.get(os.path.basename(fs[i]), "?"), v))
    return len(a), hits
for t in sys.argv[1:]:
    n, hits = run(t)
    print(t, "frames", n, "violet flips", len(hits), hits[:12])
