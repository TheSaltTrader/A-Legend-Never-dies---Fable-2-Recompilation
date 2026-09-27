"""sky_invert.py TAG [exposure]: native [SCENE] sky level recovered from the native WINDOW captures of a series leg run
with ngpu_exposure=E (present mode 17 = per-channel Reinhard c/(1+c) on scene*E, linear swapchain, measured).
Per pixel: c = x/(1-x)/E on each linear channel (x = byte/255), then BT.709 luminance, mean over the sky rows
(window rows 45:120 = scene rows 7:82). Pixels at x >= 0.995 are saturated and counted, not inverted."""
import numpy as np, glob, os, sys
from PIL import Image
tag = sys.argv[1]; E = float(sys.argv[2]) if len(sys.argv) > 2 else 0.05
W = np.array([.2126, .7152, .0722])
regs = {"sky": (45, 120, 0, 1280), "ground": (400, 700, 0, 500), "lake": (260, 330, 700, 1200)}
root = os.path.join(r"D:\fable2_flash\gameplay", tag + "_series")
for d in sorted(glob.glob(os.path.join(root, "s*"))):
    f = os.path.join(d, "win0_wgc.png")
    if not os.path.exists(f): continue
    x = np.asarray(Image.open(f).convert("RGB")).astype(np.float64) / 255.0
    out = []
    for k, (r0, r1, c0, c1) in regs.items():
        v = x[r0:r1, c0:c1]; sat = (v >= 0.995).any(-1)
        c = np.clip(v, 0, 0.995); c = c / (1 - c) / E
        out.append(f"{k} {(c @ W)[~sat].mean():.3f} (sat {100 * sat.mean():.1f}%)")
    print(f"[SCENE via WINDOW, E={E}] {tag} {os.path.basename(d)}: " + " | ".join(out))
