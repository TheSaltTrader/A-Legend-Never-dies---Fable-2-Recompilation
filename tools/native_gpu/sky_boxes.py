"""sky_boxes.py TAG...: the native [SCENE] sky (inverted from the window at E=0.05) in DISJOINT sky boxes, per capture -
lockstep across boxes = a cause acting on the whole sky pass; different phase/amplitude = something moving across it."""
import numpy as np, glob, os, sys
from PIL import Image
E = 0.05; W = np.array([.2126, .7152, .0722])
boxes = {"L": (45, 120, 0, 320), "CL": (45, 120, 320, 640), "CR": (45, 120, 640, 960), "R": (45, 120, 960, 1280)}
for tag in sys.argv[1:]:
    ser = {k: [] for k in boxes}
    for d in sorted(glob.glob(os.path.join(r"D:\fable2_flash\gameplay", tag + "_series", "s*"))):
        f = os.path.join(d, "win0_wgc.png")
        if not os.path.exists(f): continue
        x = np.clip(np.asarray(Image.open(f).convert("RGB")).astype(np.float64) / 255.0, 0, 0.995)
        c = (x / (1 - x) / E) @ W
        for k, (r0, r1, c0, c1) in boxes.items(): ser[k].append(c[r0:r1, c0:c1].mean())
    print(tag)
    for k, v in ser.items():
        v = np.array(v); print(f"  box {k:2s}: " + " ".join(f"{a:6.2f}" for a in v) + f" | amplitude x{v.max() / v.min():.2f}, argmin s{int(v.argmin()):02d}, argmax s{int(v.argmax()):02d}")
    ks = list(boxes); M = np.array([ser[k] for k in ks]); N = M / M.mean(1, keepdims=True)
    cc = np.corrcoef(N)
    print("  pairwise corr of normalised series: " + ", ".join(f"{ks[i]}-{ks[j]} {cc[i, j]:+.3f}" for i in range(4) for j in range(i + 1, 4)))
