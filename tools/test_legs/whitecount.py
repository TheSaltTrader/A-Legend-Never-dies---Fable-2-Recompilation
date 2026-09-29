import sys, glob, os, statistics as st
from PIL import Image, ImageStat
for t in sys.argv[1:]:
    fs = sorted(glob.glob(f"{t}_burst/f*.jpg"))
    v = [ImageStat.Stat(Image.open(f).convert("L")).mean[0] for f in fs]
    med = st.median(v)
    w = [os.path.basename(f) for f, x in zip(fs, v) if x > med + 40]
    ch = [abs(b - a) for a, b in zip(v, v[1:])]
    print(t, len(v), "median", round(med, 1), "max", round(max(v), 1), "white", len(w), w[:6], "motion", round(st.mean(ch), 2))
