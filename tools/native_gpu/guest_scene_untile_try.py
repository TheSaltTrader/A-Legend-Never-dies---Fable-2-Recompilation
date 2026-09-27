import numpy as np, itertools
D = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\wt-fable2-nativegpu\out\build\win-amd64-Release\ngpu_rts"
raw = np.fromfile(D + r"\guestscene_199DF000_f001500_raw.bin", dtype=np.uint8)
nat = np.nan_to_num(np.fromfile(D + r"\f001500_rt_14010500_00030000_1280x720_1432d.f16", dtype=np.float16).astype(np.float32).reshape(720, 1280, 4)[..., :3])
W = np.array([.2126, .7152, .0722])
NL = np.log1p(np.clip(nat @ W, 0, 100))

def outer(y, width, lb):
    macro = ((y >> 5) * (width >> 5)) << (lb + 7)
    micro = ((y & 6) << 2) << lb
    return macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + lb)) + ((y & 1) << 4)
def inner(x, y, lb, base):
    macro = (x >> 5) << (lb + 7)
    micro = (x & 7) << lb
    off = base + (macro + ((micro & ~15) << 1) + (micro & 15))
    return ((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)

def decode(mode, pitch, swap):
    h, w = 720, 1280
    ys, xs = np.mgrid[0:h, 0:w]
    if mode == "64":
        o = inner(xs, ys, 3, outer(ys, pitch, 3))
        idx = o[..., None] + np.arange(8)[None, None, :]
    else:  # "2x32": each 8-byte texel stored as two 4-byte texels side by side in a 2*pitch-wide 32bpp tiling
        o0 = inner(2 * xs, ys, 2, outer(ys, 2 * pitch, 2)); o1 = inner(2 * xs + 1, ys, 2, outer(ys, 2 * pitch, 2))
        idx = np.concatenate([o0[..., None] + np.arange(4), o1[..., None] + np.arange(4)], -1)
    idx = np.clip(idx, 0, raw.size - 1)
    b = raw[idx].astype(np.uint16)
    if swap == "be16": hv = (b[..., 0::2] << 8) | b[..., 1::2]
    elif swap == "le16": hv = (b[..., 1::2] << 8) | b[..., 0::2]
    elif swap == "be32": # 8in32 then halves
        bb = b.reshape(h, w, 2, 4)[..., ::-1].reshape(h, w, 8); hv = (bb[..., 1::2] << 8) | bb[..., 0::2]
    hv = hv.astype(np.uint16)
    f = hv.view(np.float16).astype(np.float32)
    return np.nan_to_num(f[..., :3])

def score(img):
    L = np.log1p(np.clip(img @ W, 0, 100)); best = (-2, 0, 0)
    for dy in range(-24, 25, 4):
        for dx in range(-24, 25, 4):
            A = NL[max(0, dy):720 + min(0, dy), max(0, dx):1280 + min(0, dx)]; B = L[max(0, -dy):720 + min(0, -dy), max(0, -dx):1280 + min(0, -dx)]
            c = np.corrcoef(A.ravel()[::11], B.ravel()[::11])[0, 1]
            if c > best[0]: best = (c, dy, dx)
    return best

res = []
for mode, pitch, swap in itertools.product(["64", "2x32"], [1280, 1312, 1344], ["be16", "le16", "be32"]):
    try:
        img = decode(mode, pitch, swap); c = score(img); res.append((c[0], mode, pitch, swap, c[1], c[2]))
        print(f"{mode:5s} pitch {pitch} {swap}: corr {c[0]:.3f} at dy {c[1]} dx {c[2]}", flush=True)
    except Exception as e:
        print(mode, pitch, swap, "error", e)
best = max(res)
print("BEST", best)
np.save(r"C:\Users\renoi\.claude\jobs\6397a53c\tmp\untile_best.npy", decode(best[1], best[2], best[3]))
