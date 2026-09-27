import numpy as np, itertools
exec(open(r"C:\Users\renoi\.claude\jobs\6397a53c\tmp\blockiness.py").read().split("for name, img in")[0])

def inner_v(x, y, lb, base, xs_shift, ybit, bank_shift):
    macro = (x >> 5) << (lb + 7)
    micro = (x & 7) << lb
    off = base + (macro + ((micro & ~15) << 1) + (micro & 15))
    pipe = ((((y & ybit) >> (ybit.bit_length() - 2)) + (x >> xs_shift)) & 3) << 6
    return ((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << bank_shift) + pipe

def decode_v(xs_shift, ybit, bank_shift, lb=3, pitch=1280):
    ys, xs = np.mgrid[0:720, 0:1280]
    o = inner_v(xs, ys, lb, outer(ys, pitch, lb), xs_shift, ybit, bank_shift)
    idx = o[..., None] + np.arange(8)
    b = raw[np.clip(idx, 0, raw.size - 1)].astype(np.uint16)
    hv = ((b[..., 0::2] << 8) | b[..., 1::2]).astype(np.uint16)
    return np.nan_to_num(hv.view(np.float16).astype(np.float32)[..., :3])

res = []
for xs_shift, ybit, bank_shift in itertools.product([2, 3, 4], [4, 8, 16], [6, 7, 8]):
    img = decode_v(xs_shift, ybit, bank_shift)
    worst = max([bratio(img, 1, s) for s in (4, 8, 16, 32)] + [bratio(img, 0, s) for s in (2, 4, 8, 16, 32)])
    res.append((worst, xs_shift, ybit, bank_shift))
res.sort()
for r in res[:6]: print("worst-stride ratio %.2f  pipe x>>%d, y&%d, bank (y&16)<<%d" % r)
print("reference (current formula) = x>>3, y&8, <<7")
