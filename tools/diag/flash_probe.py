"""One-frame flash detector: grab the primary screen as fast as GDI allows
(~45 fps at 600 px wide), score every frame for near-white and magenta pixel
fractions and the brightness of the upper half (the canopy), and flag frames
whose score jumps above BOTH neighbourhoods and reverts - a one-frame flash,
not a pan. Saves a contact sheet (m-2..m+2) per flagged frame.
usage: flash_probe.py <outdir> <seconds>"""
import ctypes as C, ctypes.wintypes as W, os, sys, time
from collections import deque
import numpy as np
from PIL import Image, ImageDraw

OUT = sys.argv[1]; DUR = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    try: os.remove(os.path.join(OUT, f))
    except OSError: pass

u = C.windll.user32; g = C.windll.gdi32
u.SetProcessDPIAware()
SW = u.GetSystemMetrics(0); SH = u.GetSystemMetrics(1)
CW = 640; CH = int(SH * CW / SW)
hdc_screen = u.GetDC(0)
hdc_mem = g.CreateCompatibleDC(hdc_screen)
hbmp = g.CreateCompatibleBitmap(hdc_screen, CW, CH)
g.SelectObject(hdc_mem, hbmp)
g.SetStretchBltMode(hdc_mem, 3)
SRCCOPY = 0x00CC0020

class BITMAPINFOHEADER(C.Structure):
    _fields_ = [("biSize",W.DWORD),("biWidth",C.c_long),("biHeight",C.c_long),
                ("biPlanes",W.WORD),("biBitCount",W.WORD),("biCompression",W.DWORD),
                ("biSizeImage",W.DWORD),("biXPelsPerMeter",C.c_long),("biYPelsPerMeter",C.c_long),
                ("biClrUsed",W.DWORD),("biClrImportant",W.DWORD)]
bmi = BITMAPINFOHEADER(); bmi.biSize = C.sizeof(BITMAPINFOHEADER)
bmi.biWidth = CW; bmi.biHeight = -CH; bmi.biPlanes = 1; bmi.biBitCount = 32; bmi.biCompression = 0
buf = (C.c_char * (CW*CH*4))()

def grab():
    g.StretchBlt(hdc_mem, 0,0,CW,CH, hdc_screen, 0,0,SW,SH, SRCCOPY)
    g.GetDIBits(hdc_mem, hbmp, 0, CH, buf, C.byref(bmi), 0)
    return np.frombuffer(buf, dtype=np.uint8).reshape(CH, CW, 4)[:, :, :3].copy()  # BGR

def score(img):
    b = img[:, :, 0].astype(np.int16); gg = img[:, :, 1].astype(np.int16); r = img[:, :, 2].astype(np.int16)
    white = float(((b > 215) & (gg > 215) & (r > 215)).mean())
    magenta = float(((r > 140) & (b > 140) & (gg < 90)).mean())
    top = float(img[: int(CH * 0.5)].mean())
    return white, magenta, top

# ring of the last 9 frames; the middle one is judged when the ring is full
ring = deque(maxlen=9)
scores = []          # (t, white, magenta, top) for every frame
flags = []           # (index, reason, values)
t0 = time.perf_counter()
n = 0
def judge():
    i = 4  # middle of the ring
    (ti, imi, si) = ring[i]
    before = [ring[j][2] for j in range(0, 4)]
    after = [ring[j][2] for j in range(5, 9)]
    reasons = []
    w, m, top = si
    if w - max(max(x[0] for x in before), max(x[0] for x in after)) > 0.015: reasons.append("white+%.1f%%" % (w * 100))
    if m - max(max(x[1] for x in before), max(x[1] for x in after)) > 0.008: reasons.append("magenta+%.1f%%" % (m * 100))
    if top - max(max(x[2] for x in before), max(x[2] for x in after)) > 14: reasons.append("bright+%.0f" % top)
    if reasons:
        idx = n - 5
        flags.append((idx, ti, reasons, si))
        sheet = Image.new("RGB", (5 * CW, CH + 16), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for k, j in enumerate(range(2, 7)):
            tj, imj, sj = ring[j]
            sheet.paste(Image.fromarray(imj[:, :, ::-1]), (k * CW, 16))
            d.text((k * CW + 4, 2), "%.3fs w%.1f m%.1f t%.0f%s" % (tj, sj[0]*100, sj[1]*100, sj[2], " <==" if j == 4 else ""), fill=(255, 255, 0))
        sheet.save(os.path.join(OUT, "flash_%04d_%05dms.jpg" % (idx, int(ti * 1000))), quality=80)

while True:
    t = time.perf_counter() - t0
    if t >= DUR: break
    img = grab()
    s = score(img)
    scores.append((t,) + s)
    ring.append((t, img, s))
    n += 1
    if len(ring) == 9:
        judge()
dur = time.perf_counter() - t0
print("captured %d frames in %.1fs = %d fps  %dx%d" % (n, dur, round(n / dur), CW, CH))
ws = np.array([s[1] for s in scores]); ms = np.array([s[2] for s in scores]); ts = np.array([s[3] for s in scores])
print("white%%: median %.2f max %.2f   magenta%%: median %.2f max %.2f   top mean: median %.0f max %.0f" %
      (np.median(ws)*100, ws.max()*100, np.median(ms)*100, ms.max()*100, np.median(ts), ts.max()))
print("FLAGGED one-frame spikes: %d" % len(flags))
for idx, ti, reasons, si in flags:
    print("  frame %4d  %6.2fs  %s" % (idx, ti, " ".join(reasons)))
with open(os.path.join(OUT, "scores.csv"), "w") as f:
    for s in scores: f.write("%.3f,%.4f,%.4f,%.1f\n" % s)
