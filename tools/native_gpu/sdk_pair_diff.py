"""sdk_pair_diff.py LEG_DIR CONTROL_DIR [frame]: PHASE A per-pair judgement. Both dirs are leg dump folders
(out/ngpu_<TAG>) holding the native scene resolve (F99DF000 .f16) and the plugin's guest copy (199E0000 rgb32f) at the
frame. Prints, per 32x32 block of the 1280x720 scene:
  - the LAKE STATE of each leg (lake region native/plugin luminance: ~0.37 / ~0.99 / ~1.31 - pair legs in one state),
  - CHANGED: blocks whose native luminance moved by more than 10% between the two legs (same-state floor: ~5 at 0.99,
    0 at 0.37, n=2 each),
  - of those, TOWARD (the leg is closer to the plugin than the control) and AWAY.
Blocks where the plugin's luminance is below 0.02 are skipped (ratios meaningless)."""
import numpy as np, sys, os

fr = int(sys.argv[3]) if len(sys.argv) > 3 else 1500
W = np.array([.2126, .7152, .0722], dtype=np.float32)


def load(d):
    nat = np.nan_to_num(np.fromfile(os.path.join(d, f"f{fr:06d}_res_F99DF000_1280x720.f16"), dtype=np.float16)
                        .astype(np.float32).reshape(720, 1280, 4)[..., :3]) @ W
    plg = np.fromfile(os.path.join(d, f"guestscene_199E0000_f{fr:06d}_1280x720_rgb32f.bin"), dtype=np.float32).reshape(720, 1280, 3) @ W
    return nat, plg


def lake(nat, plg):
    r0, r1, c0, c1 = 222, 292, 700, 1200
    return nat[r0:r1, c0:c1].mean() / max(plg[r0:r1, c0:c1].mean(), 1e-6)


a_n, a_p = load(sys.argv[1])
b_n, b_p = load(sys.argv[2])
print(f"lake state: leg {lake(a_n, a_p):.3f}  control {lake(b_n, b_p):.3f}")
blk = lambda x: x.reshape(720 // 16, 16, 1280 // 32, 32).mean((1, 3)) if False else x[:704].reshape(22, 32, 40, 32).mean((1, 3))
an, bn, ap, bp = blk(a_n), blk(b_n), blk(a_p), blk(b_p)
valid = (ap > 0.02) & (bp > 0.02) & (bn > 1e-4)
changed = valid & (np.abs(an / np.maximum(bn, 1e-6) - 1) > 0.10)
toward = changed & (np.abs(an - ap) < np.abs(bn - bp))
print(f"blocks {int(valid.sum())} valid of {valid.size}; CHANGED {int(changed.sum())} (toward {int(toward.sum())}, away {int((changed & ~toward).sum())})")
for r, c in list(zip(*np.nonzero(changed)))[:40]:
    print(f"  block row {r * 32:3d} col {c * 32:4d}: leg {an[r, c]:.3f} control {bn[r, c]:.3f} plugin {ap[r, c]:.3f}/{bp[r, c]:.3f}")
