import numpy as np
D = r"D:\fable2_flash\ngpu_dumps_20260924\GRAW1"
raw = np.fromfile(D + r"\guestscene_199DF000_f001500_raw.bin", dtype=np.uint8)
nat = np.nan_to_num(np.fromfile(D + r"\f001500_rt_14010500_00030000_1280x720_1432d.f16", dtype=np.float16).astype(np.float32).reshape(720, 1280, 4)[..., :3])
W = np.array([.2126, .7152, .0722])

def outer(y, width, lb):
    macro = ((y >> 5) * (width >> 5)) << (lb + 7)
    micro = ((y & 6) << 2) << lb
    return macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + lb)) + ((y & 1) << 4)
def inner(x, y, lb, base):
    macro = (x >> 5) << (lb + 7)
    micro = (x & 7) << lb
    off = base + (macro + ((micro & ~15) << 1) + (micro & 15))
    return ((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)

def decode(mode, pitch=1280):
    ys, xs = np.mgrid[0:720, 0:1280]
    if mode == "64":
        o = inner(xs, ys, 3, outer(ys, pitch, 3)); idx = o[..., None] + np.arange(8)
    else:
        o0 = inner(2 * xs, ys, 2, outer(ys, 2 * pitch, 2)); o1 = inner(2 * xs + 1, ys, 2, outer(ys, 2 * pitch, 2))
        idx = np.concatenate([o0[..., None] + np.arange(4), o1[..., None] + np.arange(4)], -1)
    b = raw[np.clip(idx, 0, raw.size - 1)].astype(np.uint16)
    hv = ((b[..., 0::2] << 8) | b[..., 1::2]).astype(np.uint16)
    return np.nan_to_num(hv.view(np.float16).astype(np.float32)[..., :3])

def bratio(img, axis, stride):
    L = np.log1p(np.clip(img @ W, 0, 100))
    d = np.abs(np.diff(L, axis=axis)).mean(axis=1 - axis)
    idx = np.arange(d.size)
    return d[(idx + 1) % stride == 0].mean() / d[(idx + 1) % stride != 0].mean()

for name, img in (("native", nat), ("64 be16", decode("64")), ("2x32 be16", decode("2x32"))):
    print(f"{name:10s} " + "  ".join(f"x/{s}:{bratio(img, 1, s):.2f}" for s in (4, 8, 16, 32)) + " | " +
          "  ".join(f"y/{s}:{bratio(img, 0, s):.2f}" for s in (2, 4, 8, 16, 32)))
