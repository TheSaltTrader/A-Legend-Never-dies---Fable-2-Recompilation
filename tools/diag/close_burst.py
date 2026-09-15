"""Full-res burst around a menu close: open the menu, then press start and grab
full-res frames as fast as possible for ~1.6 s. Save the frames that look like
'world in a 16:9 box' (bright centre, dark left/right) plus a gameplay
reference, so pillarbox-correct (hero true proportions, narrower view) can be
told from squished (same view crammed thin)."""
import ctypes as C, ctypes.wintypes as W, subprocess, time, os, sys
import numpy as np
from PIL import Image
OUT=sys.argv[1]; os.makedirs(OUT,exist_ok=True)
for f in os.listdir(OUT):
    try: os.remove(os.path.join(OUT,f))
    except OSError: pass
EXE_DIR=r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\fable2recomp\out\build\win-amd64-Release"
PAD=os.path.join(EXE_DIR,"pad_script.txt")
k=C.WinDLL('kernel32');k.OpenProcess.restype=C.c_void_p;k.ReadProcessMemory.restype=C.c_bool
pid=int(subprocess.check_output(['tasklist','/fi','imagename eq fable2.exe','/fo','csv','/nh'],text=True).split(',')[1].strip('"'))
hp=k.OpenProcess(0x0410,False,pid); b=(C.c_char*1)(); gg=C.c_size_t(0)
def flag():
    return b.raw[0] if (k.ReadProcessMemory(hp,C.c_void_p(0x100000000+0x834B2467),b,1,C.byref(gg)) and gg.value) else -1
u=C.windll.user32; g=C.windll.gdi32; u.SetProcessDPIAware()
SW=u.GetSystemMetrics(0); SH=u.GetSystemMetrics(1); CW=1200; CH=int(SH*CW/SW)
hs=u.GetDC(0); hm=g.CreateCompatibleDC(hs); hb=g.CreateCompatibleBitmap(hs,CW,CH); g.SelectObject(hm,hb); g.SetStretchBltMode(hm,3)
class BI(C.Structure):_fields_=[('s',W.DWORD),('w',C.c_long),('h',C.c_long),('p',W.WORD),('bc',W.WORD),('c',W.DWORD),('si',W.DWORD),('xp',C.c_long),('yp',C.c_long),('cu',W.DWORD),('ci',W.DWORD)]
bi=BI();bi.s=C.sizeof(BI);bi.w=CW;bi.h=-CH;bi.p=1;bi.bc=32;bi.c=0;buf=(C.c_char*(CW*CH*4))()
def grab():
    g.StretchBlt(hm,0,0,CW,CH,hs,0,0,SW,SH,0xCC0020); g.GetDIBits(hm,hb,0,CH,buf,C.byref(bi),0)
    return np.frombuffer(buf,dtype=np.uint8).reshape(CH,CW,4)[:,:,:3].copy()

# ensure in gameplay, save a reference gameplay frame
if flag()!=0:
    open(PAD,"w").write("start:0.35\n"); time.sleep(1.2)
ref=grab(); Image.fromarray(ref[:,:,::-1]).save(os.path.join(OUT,"REF_gameplay.jpg"),quality=88)
# open the menu
open(PAD,"w").write("start:0.35\n"); time.sleep(1.3)
print("opened, flag=",flag())
# press start to close, and burst-capture full res
open(PAD,"w").write("start:0.35\n")
frames=[]; flags=[]; t0=time.perf_counter()
while time.perf_counter()-t0<1.6:
    flags.append(flag()); frames.append(grab())
print("burst %d frames %d fps"%(len(frames),round(len(frames)/1.6)))
# save frames that are 'world in 16:9 box': centre bright, both edges dark, not full menu
saved=0
for i,(fr,fl) in enumerate(zip(frames,flags)):
    a=fr.mean(axis=2); l=a[:,:int(CW*0.12)].mean(); r=a[:,int(CW*0.88):].mean(); c=a[:,int(CW*0.4):int(CW*0.6)].mean()
    if c>55 and l<26 and r<26:  # 16:9 box with bright content
        Image.fromarray(fr[:,:,::-1]).save(os.path.join(OUT,"box_%02d_fl%d_c%03d.jpg"%(i,fl,c)),quality=88); saved+=1
    elif l>50 and r>40 and c>50:  # edge-to-edge world
        Image.fromarray(fr[:,:,::-1]).save(os.path.join(OUT,"edge_%02d_fl%d.jpg"%(i,fl)),quality=88)
print("saved 16:9-box frames:",saved)
open(PAD,"w").write("release\n")
k.CloseHandle(hp)
