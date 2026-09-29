import sys, glob, os
import numpy as np
from PIL import Image
# Tile-local flash counter: 8x4 grid, a tile flashes when it differs from both neighbours while they resemble each
# other (the flashcount rule per tile), so a flash confined to part of the frame (trees) is seen.
for t in sys.argv[1:]:
    fs = sorted(glob.glob(f"D:/fable2_flash/ab110/logs/sweep/{t}_burst/f*.jpg"), key=lambda p: int(os.path.basename(p)[1:-4]))
    a = np.stack([np.asarray(Image.open(f).convert("RGB").resize((96, 40)), dtype=np.float32) for f in fs])
    tiles = a.reshape(len(a), 4, 10, 8, 12, 3).mean(axis=(2, 4))      # (n, 4, 8, 3)
    dp = np.abs(tiles[1:-1] - tiles[:-2]).mean(-1); dn = np.abs(tiles[1:-1] - tiles[2:]).mean(-1)
    nn = np.abs(tiles[:-2] - tiles[2:]).mean(-1); m = np.minimum(dp, dn)
    hit = (m > 30) & (nn < m * 0.5)
    frames = hit.any(axis=(1, 2))
    print(t, "frames", len(a), "flash frames", int(frames.sum()), "tile hits", int(hit.sum()))
