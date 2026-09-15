"""Build a labeled contact sheet of frames whose ms is in a given range."""
import os, sys, re, glob
from PIL import Image, ImageDraw

OUT = sys.argv[1]
lo = int(sys.argv[2]); hi = int(sys.argv[3])
dst = sys.argv[4]
cols = int(sys.argv[5]) if len(sys.argv) > 5 else 4

files = []
for p in glob.glob(os.path.join(OUT, "f_*.jpg")):
    m = re.search(r"f_(\d+)_(\d+)ms_fl(-?\d+)_(\w+)_", os.path.basename(p))
    if not m: continue
    idx, ms, fl, cls = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
    if lo <= ms <= hi:
        files.append((ms, fl, cls, idx, p))
files.sort()
if not files:
    print("no frames in range"); sys.exit(0)

thumb_w = 340
im0 = Image.open(files[0][4]); tw, th = thumb_w, int(im0.height*thumb_w/im0.width)
rows = (len(files)+cols-1)//cols
pad = 18
sheet = Image.new("RGB", (cols*tw, rows*(th+pad)), (40,40,40))
d = ImageDraw.Draw(sheet)
for i,(ms,fl,cls,idx,p) in enumerate(files):
    im = Image.open(p).resize((tw,th))
    r,c = divmod(i, cols)
    x,y = c*tw, r*(th+pad)
    sheet.paste(im, (x, y+pad))
    d.rectangle([x,y,x+tw,y+pad], fill=(0,0,0))
    d.text((x+3,y+3), "%dms fl=%d %s #%d" % (ms,fl,cls,idx), fill=(255,255,0))
sheet.save(dst)
print("montage:", dst, "frames:", len(files), "grid %dx%d" % (cols,rows))
for (ms,fl,cls,idx,p) in files:
    print("  %5dms fl=%d %-7s #%d" % (ms,fl,cls,idx))
