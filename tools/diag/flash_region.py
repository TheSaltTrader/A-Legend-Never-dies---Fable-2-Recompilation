"""Score one screen region at ~40 fps for the white/violet tree flash: the
fraction of frames in which the region is mostly near-white or violet.
usage: flash_region.py <x> <y> <w> <h> <seconds> [outdir]"""
import ctypes as C, ctypes.wintypes as W, os, sys, time
import numpy as np

X, Y, Wd, Ht = (int(v) for v in sys.argv[1:5]); DUR = float(sys.argv[5])
OUT = sys.argv[6] if len(sys.argv) > 6 else None
u = C.windll.user32; g = C.windll.gdi32
u.SetProcessDPIAware()
hdc_screen = u.GetDC(0)
hdc_mem = g.CreateCompatibleDC(hdc_screen)
hbmp = g.CreateCompatibleBitmap(hdc_screen, Wd, Ht)
g.SelectObject(hdc_mem, hbmp)
SRCCOPY = 0x00CC0020

class BITMAPINFOHEADER(C.Structure):
    _fields_ = [("biSize",W.DWORD),("biWidth",C.c_long),("biHeight",C.c_long),
                ("biPlanes",W.WORD),("biBitCount",W.WORD),("biCompression",W.DWORD),
                ("biSizeImage",W.DWORD),("biXPelsPerMeter",C.c_long),("biYPelsPerMeter",C.c_long),
                ("biClrUsed",W.DWORD),("biClrImportant",W.DWORD)]
bmi = BITMAPINFOHEADER(); bmi.biSize = C.sizeof(BITMAPINFOHEADER)
bmi.biWidth = Wd; bmi.biHeight = -Ht; bmi.biPlanes = 1; bmi.biBitCount = 32; bmi.biCompression = 0
buf = (C.c_char * (Wd*Ht*4))()

def grab():
    g.BitBlt(hdc_mem, 0, 0, Wd, Ht, hdc_screen, X, Y, SRCCOPY)
    g.GetDIBits(hdc_mem, hbmp, 0, Ht, buf, C.byref(bmi), 0)
    return np.frombuffer(buf, dtype=np.uint8).reshape(Ht, Wd, 4)[:, :, :3].copy()

t0 = time.perf_counter(); n = 0; hits = 0; series = []
while time.perf_counter() - t0 < DUR:
    img = grab(); n += 1
    b = img[:, :, 0].astype(np.int16); gg = img[:, :, 1].astype(np.int16); r = img[:, :, 2].astype(np.int16)
    white = float(((b > 200) & (gg > 200) & (r > 200)).mean())
    violet = float(((b > 150) & (r > 90) & (gg < 120) & (b > gg + 60)).mean())
    flash = (white + violet) > 0.12
    hits += flash; series.append(1 if flash else 0)
dur = time.perf_counter() - t0
runs = sum(1 for i in range(1, len(series)) if series[i] and not series[i-1])
print("frames %d in %.1fs (%d fps): FLASH frames %d = %.1f%%, %d separate flashes" % (n, dur, round(n/dur), hits, 100.0*hits/max(n,1), runs))
print("pattern (first 120):", "".join("#" if s else "." for s in series[:120]))
