"""Instrumented Start-menu transition probe.

One tight loop, one clock: grab the primary screen (downscaled via GDI
StretchBlt), read the guest pause flag (0x834B2467), and drive the Start
button through pad_script.txt. Every frame is classified so I can see exactly
what is on screen at the instant the flag flips and the aspect changes:

  ULTRAWIDE  - content reaches both edges (edge to edge)
  BARS169    - bright centre, dark left+right columns (16:9 pillarbox)
  BLACK      - whole frame dark, centre included (the fade veil)
  MENU?      - bright but not clearly one of the above (UI on screen)

Saves each frame as a small JPEG named with index, ms, flag, class, and the
three column means, and prints a compact timeline with the two presses marked.
"""
import ctypes as C, ctypes.wintypes as W, subprocess, time, os, sys
import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\renoi\AppData\Local\Temp\claude\C--users-renoi-claudecode\741cdc14-389f-4a29-9804-8d01e95b98c5\scratchpad\menuprobe"
DUR = float(sys.argv[2]) if len(sys.argv) > 2 else 7.0
PRESS1 = 1.3    # s: open menu
PRESS2 = 4.2    # s: close menu
EXE_DIR = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\fable2recomp\out\build\win-amd64-Release"
PAD = os.path.join(EXE_DIR, "pad_script.txt")
FLAG_VA = 0x834B2467
HOST = 0x100000000 + FLAG_VA

os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    try: os.remove(os.path.join(OUT, f))
    except OSError: pass

# --- guest flag reader ---
k = C.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.restype = C.c_void_p
k.ReadProcessMemory.restype = C.c_bool
pid = int(subprocess.check_output(["tasklist","/fi","imagename eq fable2.exe","/fo","csv","/nh"],text=True).split(",")[1].strip('"'))
hp = k.OpenProcess(0x0410, False, pid)
_b = (C.c_char*1)(); _g = C.c_size_t(0)
def read_flag():
    if k.ReadProcessMemory(hp, C.c_void_p(HOST), _b, 1, C.byref(_g)) and _g.value:
        return _b.raw[0]
    return -1

# --- GDI fast screen grab (primary monitor, downscaled) ---
u = C.windll.user32; g = C.windll.gdi32
u.SetProcessDPIAware()
SW = u.GetSystemMetrics(0); SH = u.GetSystemMetrics(1)
CW = 600; CH = int(SH * CW / SW)
hdc_screen = u.GetDC(0)
hdc_mem = g.CreateCompatibleDC(hdc_screen)
hbmp = g.CreateCompatibleBitmap(hdc_screen, CW, CH)
g.SelectObject(hdc_mem, hbmp)
g.SetStretchBltMode(hdc_mem, 3)  # COLORONCOLOR (fast)
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
    a = np.frombuffer(buf, dtype=np.uint8).reshape(CH, CW, 4)[:, :, :3]  # BGR
    return a

def press():
    with open(PAD, "w") as f: f.write("start:0.35\n")

# --- capture loop ---
frames = []; meta = []
t0 = time.perf_counter()
did1 = did2 = False
while True:
    t = time.perf_counter() - t0
    if t >= DUR: break
    if not did1 and t >= PRESS1: press(); did1 = True; mark1 = t
    if not did2 and t >= PRESS2: press(); did2 = True; mark2 = t
    fl = read_flag()
    img = grab().copy()
    frames.append(img); meta.append((t, fl))
n = len(frames)
dur = time.perf_counter() - t0
print("captured %d frames in %.1fs = %d fps  screen %dx%d -> %dx%d" % (n, dur, round(n/dur), SW, SH, CW, CH))

# --- classify + save ---
def stats(img):
    gray = img.mean(axis=2)
    h, w = gray.shape
    left = gray[:, :int(w*0.12)].mean()
    right = gray[:, int(w*0.88):].mean()
    center = gray[:, int(w*0.40):int(w*0.60)].mean()
    whole = gray.mean()
    return whole, left, right, center

def classify(whole, left, right, center):
    if center < 22 and whole < 22: return "BLACK"
    if center > 55 and left < 26 and right < 26: return "BARS169"
    if center > 45 and left > 42 and right > 42: return "ULTRAW"
    return "MENUq"

rows = []
for i,(img,(t,fl)) in enumerate(zip(frames, meta)):
    wh,l,r,c = stats(img)
    cls = classify(wh,l,r,c)
    rows.append((i, int(t*1000), fl, cls, wh, l, r, c))
    name = "f_%04d_%05dms_fl%d_%s_L%03d_R%03d_C%03d.jpg" % (i, int(t*1000), fl, cls, l, r, c)
    Image.fromarray(img[:, :, ::-1]).save(os.path.join(OUT, name), quality=70)

# --- timeline: print class runs + flag runs, mark presses ---
print("\nTIMELINE (ms : class : flag) - runs collapsed")
prev = None
for (i,ms,fl,cls,wh,l,r,c) in rows:
    key = (cls, fl)
    if key != prev:
        marks = ""
        if did1 and abs(ms - int(mark1*1000)) < 40: marks += "  <<< PRESS-START(open)"
        print("  %5dms  %-7s flag=%d  (L%3d C%3d R%3d)%s" % (ms, cls, fl, l, c, r, marks))
        prev = key
# explicit press timing
print("\nPresses: open at %dms, close at %dms" % (int(mark1*1000), int(mark2*1000) if did2 else -1))
# find transition windows: first BARS169 or BLACK after each press
def first_after(ms_start, classes):
    for (i,ms,fl,cls,wh,l,r,c) in rows:
        if ms >= ms_start and cls in classes: return (i,ms,cls)
    return None
print("first BLACK after open press:", first_after(int(mark1*1000), {"BLACK"}))
print("first BARS169 after open press:", first_after(int(mark1*1000), {"BARS169"}))
if did2:
    print("first BLACK after close press:", first_after(int(mark2*1000), {"BLACK"}))
    print("first ULTRAW after close press:", first_after(int(mark2*1000), {"ULTRAW"}))
print("\nOUT:", OUT)
k.CloseHandle(hp)
u.ReleaseDC(0, hdc_screen); g.DeleteDC(hdc_mem); g.DeleteObject(hbmp)
