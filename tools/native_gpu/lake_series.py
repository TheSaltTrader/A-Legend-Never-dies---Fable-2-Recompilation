"""lake_series.py TAG [TAG...]: lake-region luminance ratio native/plugin for every capture of a
bridge_leg_series.sh run (exactly the SERIES1/2 measurement: rows 260:330, cols 700:1200 of the
native WGC capture, plugin resized to its width and bottom-aligned). Prints capture times too."""
import numpy as np, glob, os, sys, time
from PIL import Image
W = np.array([.2126, .7152, .0722])
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for t in sys.argv[1:]:
    out = []
    for d in sorted(glob.glob(os.path.join(t + "_series", "s*"))):
        a = os.path.join(d, 'win0_wgc.png'); p = os.path.join(d, 'win1_wgc.png')
        if not (os.path.exists(a) and os.path.exists(p)): out.append(f"{os.path.basename(d)} --"); continue
        A = np.asarray(Image.open(a).convert('RGB')).astype(float); P = Image.open(p).convert('RGB')
        P = np.asarray(P.resize((A.shape[1], int(P.size[1] * A.shape[1] / P.size[0])))).astype(float)
        off = P.shape[0] - A.shape[0]; Pa = np.zeros_like(A); Pa[-off:] = P[:A.shape[0] + off]
        r = (A[260:330, 700:1200] @ W).mean() / max((Pa[260:330, 700:1200] @ W).mean(), 1)
        out.append(f"{os.path.basename(d)} {time.strftime('%H:%M:%S', time.localtime(os.path.getmtime(a)))} {r:.3f}")
    print(t, "lake-region ratio:"); print("  " + "\n  ".join(out))
