"""region_pixel_shift.py ONDIR OFFDIR r0 r1 c0 c1 [frame]: [SCENE] per-pixel native ON/OFF luminance ratio inside one
rect (same binary, one cvar), and native/plugin for the moved vs unmoved pixel populations. Answers "did the rect's
SUBJECT move, or the background showing through it"."""
import numpy as np, sys, os
on, off = sys.argv[1], sys.argv[2]; r0, r1, c0, c1 = map(int, sys.argv[3:7]); fr = int(sys.argv[7]) if len(sys.argv) > 7 else 1500
W = np.array([.2126, .7152, .0722])
def nat(d): return np.nan_to_num(np.fromfile(os.path.join(d, f"f{fr:06d}_res_F99DF000_1280x720.f16"), dtype=np.float16).astype(np.float32).reshape(720, 1280, 4)[r0:r1, c0:c1, :3]) @ W
def plg(d): return np.fromfile(os.path.join(d, f"guestscene_199E0000_f{fr:06d}_1280x720_rgb32f.bin"), dtype=np.float32).reshape(720, 1280, 3)[r0:r1, c0:c1] @ W
a, b, p = nat(on), nat(off), plg(on)
ok = b > 1e-3
q = np.where(ok, a / np.where(ok, b, 1), 1)
moved = ok & (np.abs(q - 1) > 0.03)
print(f"pixels {q.size}; moved (|on/off-1|>0.03) {moved.sum()} ({100 * moved.mean():.1f}%)")
print(f"on/off ratio: moved median {np.median(q[moved]) if moved.any() else float('nan'):.3f} p10 {np.percentile(q[moved], 10) if moved.any() else float('nan'):.3f} p90 {np.percentile(q[moved], 90) if moved.any() else float('nan'):.3f}")
for nm, m in (("moved", moved), ("unmoved", ok & ~moved)):
    if m.any(): print(f"{nm:8s} lum ON {a[m].mean():.3f} OFF {b[m].mean():.3f} plugin {p[m].mean():.3f} | ON/plugin {a[m].mean() / p[m].mean():.3f} OFF/plugin {b[m].mean() / p[m].mean():.3f}")
