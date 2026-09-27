"""bc1_levels.py DIR BASE OUT.png: decode every staged BC1 level (levels_<BASE>_*_L<n>_<w>x<h>_rowbytes<r>.bin, written by
ngpu_dump_tex_levels_addr) and tile colour (top) and alpha (bottom) per level, each scaled to level 0's size.
A correct chain shows the same picture at every level, only softer."""
import glob, os, re, sys
import numpy as np
from PIL import Image

def c565(v):
    return np.array([((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31], np.int32)

def decode(buf, w, h, rowbytes):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = np.zeros((bh * 4, bw * 4, 4), np.uint8)
    for by in range(bh):
        for bx in range(bw):
            o = by * rowbytes + bx * 8
            c0 = buf[o] | (buf[o + 1] << 8); c1 = buf[o + 2] | (buf[o + 3] << 8)
            bits = buf[o + 4] | (buf[o + 5] << 8) | (buf[o + 6] << 16) | (buf[o + 7] << 24)
            a, b = c565(c0), c565(c1)
            if c0 > c1: pal = [a, b, (2 * a + b) // 3, (a + 2 * b) // 3]; al = [255] * 4
            else: pal = [a, b, (a + b) // 2, np.zeros(3, np.int32)]; al = [255, 255, 255, 0]
            for i in range(16):
                k = (bits >> (2 * i)) & 3
                out[by * 4 + i // 4, bx * 4 + i % 4, :3] = pal[k]; out[by * 4 + i // 4, bx * 4 + i % 4, 3] = al[k]
    return out[:h, :w]

d, base, outp = sys.argv[1], sys.argv[2], sys.argv[3]
files = sorted(glob.glob(os.path.join(d, f"levels_{base}_*_L*_*x*_rowbytes*.bin")), key=lambda f: int(re.search(r"_L(\d+)_", f).group(1)))
cols = []
W0 = None
for f in files:
    L, w, h, rb = map(int, re.search(r"_L(\d+)_(\d+)x(\d+)_rowbytes(\d+)", f).groups())
    img = decode(np.fromfile(f, np.uint8).tolist(), w, h, rb)
    W0 = W0 or (w, h)
    c = Image.fromarray(img[..., :3].copy()).resize(W0, Image.NEAREST)
    a = Image.fromarray(img[..., 3].copy()).convert("RGB").resize(W0, Image.NEAREST)
    col = Image.new("RGB", (W0[0], W0[1] * 2)); col.paste(c, (0, 0)); col.paste(a, (0, W0[1])); cols.append(col)
    print(f"L{L} {w}x{h}: alpha coverage {(img[..., 3] > 127).mean():.3f}, mean rgb {img[..., :3].reshape(-1, 3).mean(0).round(1)}")
sheet = Image.new("RGB", (W0[0] * len(cols) + 4 * (len(cols) - 1), W0[1] * 2), (255, 0, 255))
for i, c in enumerate(cols): sheet.paste(c, (i * (W0[0] + 4), 0))
sheet.save(outp)
