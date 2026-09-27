"""Offline test of the tonemap-by-reinterpretation hypothesis (pre-registered in HANDOVER, 2026-09-24).
The native HDR scene (raw RGBA16F dump of 14010500/00030000) -> encode each channel as Xenos 7e3 (fmt 3,
2_10_10_10_FLOAT) -> read the 10 bits as UNORM10 (fmt 2/10, the post views over the same EDRAM) -> 8-bit.
Compared region by region with the plugin window, beside the native window's own ratios."""
import numpy as np, glob, os
from PIL import Image
B = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\wt-fable2-nativegpu\out\build\win-amd64-Release\ngpu_rts"
G = r"D:\fable2_flash\gameplay"
W = np.array([.2126, .7152, .0722])
f = sorted(glob.glob(os.path.join(B, "*_rt_14010500_00030000_1280x720_*.f16")))[-1]
hdr = np.fromfile(f, dtype=np.float16).reshape(720, 1280, 4).astype(np.float32)[..., :3]
print("dump", os.path.basename(f), "HDR lum p50/p90/max", np.percentile(hdr @ W, [50, 90]).round(3), float((hdr @ W).max()).__round__(3))

def f32_to_7e3(v):
    v = np.nan_to_num(v, nan=0.0)
    bits = v.astype(np.float32).view(np.uint32).astype(np.int64)
    out = np.zeros(v.shape, np.int64)
    pos = v > 0
    big = v >= 31.875
    den = pos & ~big & (bits < 0x3E800000)
    nrm = pos & ~big & ~den
    shift = np.clip(125 - (bits >> 23), 0, 31)
    b = np.where(den, (0x800000 | (bits & 0x7FFFFF)) >> shift, bits + 0xC2000000 - (1 << 32))
    b = np.where(nrm | den, b, 0) & 0xFFFFFFFF
    r = ((b + 0x7FFF + ((b >> 16) & 1)) >> 16) & 0x3FF
    out = np.where(big, 0x3FF, np.where(pos, r, 0))
    return out

pred = np.round(f32_to_7e3(hdr) / 1023.0 * 255.0)   # unorm10 read of the 7e3 bits, resolved to 8 bits
a = Image.open(os.path.join(G, "RAWDUMP1_windows", "win0_wgc.png")).convert("RGB")
p = Image.open(os.path.join(G, "RAWDUMP1_windows", "win1_wgc.png")).convert("RGB")
A = np.asarray(a).astype(float); P = np.asarray(p.resize((a.size[0], int(p.size[1] * a.size[0] / p.size[0])))).astype(float)
off = P.shape[0] - A.shape[0]; Pa = np.zeros_like(A); Pa[-off:] = P[:A.shape[0] + off]
Pr = np.zeros_like(A); Pr[38:38 + 720, 1:1281] = pred   # dump -> window client coords
R = {'sky': (40, 70, 300, 900), 'far_hills': (60, 150, 0, 1000), 'island_shrine': (160, 230, 560, 760), 'arches': (170, 340, 300, 600),
     'lake_region': (260, 330, 700, 1200), 'ferns': (420, 540, 600, 960), 'near_ground': (600, 758, 1, 1281)}
print(f"{'region':14s} native-window/plugin | REINTERPRETED/plugin")
for n, (y0, y1, x0, x1) in R.items():
    pl = (Pa[y0:y1, x0:x1] @ W).mean()
    print(f"{n:14s} {(A[y0:y1, x0:x1] @ W).mean() / pl:.3f}                | {(Pr[y0:y1, x0:x1] @ W).mean() / pl:.3f}")
Image.fromarray(pred.astype(np.uint8)).save(r"C:\Users\renoi\.claude\jobs\6397a53c\tmp\reinterp_pred.png")
