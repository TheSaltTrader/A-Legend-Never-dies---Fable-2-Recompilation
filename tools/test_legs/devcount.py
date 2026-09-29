import os
import sys, glob
import numpy as np
from PIL import Image
# Alternation-independent: each frame's mean brightness against the burst's robust centre (median, MAD).
# Catches a steady veil that flashcount (neighbour test) cannot; camera sweeps past bright objects can false-positive.
for t in sys.argv[1:]:
    fs = sorted(glob.glob(f"D:/fable2_flash/ab110/logs/sweep/{t}_burst/f*.jpg"), key=lambda p: int(os.path.basename(p)[1:-4]))
    v = np.array([np.asarray(Image.open(f).convert("L"), dtype=np.float32).mean() for f in fs])
    med = np.median(v); mad = np.median(np.abs(v - med)) * 1.4826 + 1e-6
    dev = np.abs(v - med) / mad > 6
    run = best = 0
    for d in dev:
        run = run + 1 if d else 0; best = max(best, run)
    print(t, "frames", len(v), "deviant", int(dev.sum()), "longest run", best)
