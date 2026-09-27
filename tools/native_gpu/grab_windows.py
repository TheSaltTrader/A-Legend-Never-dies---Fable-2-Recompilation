"""grab_windows.py <pid> <outdir>: every visible top-level window of <pid>, captured TWO ways
that share no code with the renderer's own ngpu_shot: WGC (the window's presented frames) and a
GDI copy of its screen rectangle. Prints each window's title, rect, and mean colour per capture."""
import ctypes as C, ctypes.wintypes as W, os, sys, threading, time
import numpy as np
from PIL import Image, ImageGrab

PID = int(sys.argv[1]); OUT = sys.argv[2]; os.makedirs(OUT, exist_ok=True)
u = C.windll.user32; u.SetProcessDPIAware()
wins = []
EP = C.WINFUNCTYPE(C.c_bool, W.HWND, W.LPARAM)
def cb(h, l):
    pid = W.DWORD(); u.GetWindowThreadProcessId(h, C.byref(pid))
    if pid.value == PID and u.IsWindowVisible(h):
        t = C.create_unicode_buffer(256); u.GetWindowTextW(h, t, 256)
        r = W.RECT(); u.GetWindowRect(h, C.byref(r))
        wins.append((h, t.value, (r.left, r.top, r.right, r.bottom)))
    return True
u.EnumWindows(EP(cb), 0)
print(f"{len(wins)} visible window(s) for pid {PID}")
for i, (h, title, rect) in enumerate(wins):
    print(f"  [{i}] hwnd {h:#x} title {title!r} rect {rect}")
    # GDI copy of the screen rectangle (what is ON SCREEN there, whatever is on top)
    try:
        g = ImageGrab.grab(bbox=rect, all_screens=True).convert("RGB")
        g.save(os.path.join(OUT, f"win{i}_gdi.png"))
        print(f"      GDI  mean rgb {tuple(int(x) for x in np.asarray(g).reshape(-1,3).mean(0))}")
    except Exception as e:
        print(f"      GDI  failed: {e}")
    # WGC (the window's own presented frames)
    try:
        from windows_capture import WindowsCapture
        got = {}; ev = threading.Event()
        cap = WindowsCapture(cursor_capture=False, draw_border=False, window_hwnd=h)
        @cap.event
        def on_frame_arrived(frame, control):
            got["img"] = np.ascontiguousarray(frame.frame_buffer[:, :, :3][:, :, ::-1]); ev.set(); control.stop()
        @cap.event
        def on_closed():
            ev.set()
        cap.start_free_threaded(); ev.wait(5)
        if "img" in got:
            Image.fromarray(got["img"]).save(os.path.join(OUT, f"win{i}_wgc.png"))
            print(f"      WGC  {got['img'].shape[1]}x{got['img'].shape[0]} mean rgb {tuple(int(x) for x in got['img'].reshape(-1,3).mean(0))}")
        else:
            print("      WGC  no frame in 5 s")
    except Exception as e:
        print(f"      WGC  failed: {e}")
