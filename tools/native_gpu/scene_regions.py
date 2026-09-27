"""scene_regions.py DUMPDIR [frame]: [SCENE] native (resolve F99DF000 .f16) vs plugin (guest copy 199E0000 rgb32f) at one
frame - luminance and mean RGB per region, native/plugin ratios. Regions in SCENE coordinates (window rows - 38)."""
import numpy as np, sys, os
d = sys.argv[1]; fr = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
W = np.array([.2126, .7152, .0722])
nat = np.nan_to_num(np.fromfile(os.path.join(d, f"f{fr:06d}_res_F99DF000_1280x720.f16"), dtype=np.float16).astype(np.float32).reshape(720, 1280, 4)[..., :3])
plg = np.fromfile(os.path.join(d, f"guestscene_199E0000_f{fr:06d}_1280x720_rgb32f.bin"), dtype=np.float32).reshape(720, 1280, 3)
regs = {"ferns": (382, 502, 600, 960), "ground": (362, 662, 0, 500), "sky_R": (7, 82, 960, 1280), "sky_CL": (7, 82, 320, 640),
        "lake": (222, 292, 700, 1200), "near_ground": (562, 720, 1, 1280)}
for k, (r0, r1, c0, c1) in regs.items():
    n = nat[r0:r1, c0:c1]; p = plg[r0:r1, c0:c1]
    nl, pl = (n @ W).mean(), (p @ W).mean()
    nc, pc = n.reshape(-1, 3).mean(0), p.reshape(-1, 3).mean(0)
    print(f"[SCENE f{fr}] {k:11s} lum native {nl:7.3f} plugin {pl:7.3f} ratio {nl / pl:6.3f} | RGB native {nc.round(3)} plugin {pc.round(3)} | R/G native {nc[0] / max(nc[1], 1e-6):.2f} plugin {pc[0] / max(pc[1], 1e-6):.2f}")
