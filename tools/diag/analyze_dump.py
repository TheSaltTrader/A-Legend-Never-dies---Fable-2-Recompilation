"""Read a draw_dump_N.txt written by the GPU plugin (gpu_draw_dump_frames) and
report which (vs, ps) shader pairs come and go across the dumped frames - the
draws that exist only during a transition (the pause-menu dissolve) - with the
state that tells how they are placed on screen (c8 pixel scale, c0..c3
projection, vertex x range, textures)."""
import re
import sys
from collections import defaultdict, OrderedDict

path = sys.argv[1]
line_re = re.compile(r"^f(\d+) d(\d+) vs ([0-9A-F]{16}) ps ([0-9A-F]{16}) prim (\d+) verts (\d+) idx (\d+) z (\d)/(\d) blend ([0-9A-F]{8}) cmask (\w) rt ([0-9A-F]{8}) vp (\d+),(\d+) (\d+)x(\d+) sc (\d+),(\d+) (\d+)x(\d+)(.*)$")

frames = OrderedDict()           # frame -> list of parsed draws
pair_frames = defaultdict(dict)  # (vs,ps) -> {frame: count}
pair_example = {}
with open(path, encoding="utf-8", errors="replace") as f:
    for raw in f:
        raw = raw.rstrip("\n")
        if raw.startswith("#"):
            continue
        m = line_re.match(raw)
        if not m:
            continue
        fr = int(m.group(1))
        d = dict(frame=fr, n=int(m.group(2)), vs=m.group(3), ps=m.group(4), prim=int(m.group(5)),
                 verts=int(m.group(6)), idx=int(m.group(7)), z=m.group(8) + "/" + m.group(9),
                 blend=m.group(10), cmask=m.group(11), rt=m.group(12),
                 vp="%s,%s %sx%s" % (m.group(13), m.group(14), m.group(15), m.group(16)),
                 sc="%s,%s %sx%s" % (m.group(17), m.group(18), m.group(19), m.group(20)),
                 rest=m.group(21).strip())
        frames.setdefault(fr, []).append(d)
        key = (d["vs"], d["ps"])
        pair_frames[key][fr] = pair_frames[key].get(fr, 0) + 1
        if key not in pair_example:
            pair_example[key] = d

nframes = len(frames)
print("frames: %d   draws per frame: %s" % (nframes, " ".join(str(len(v)) for v in frames.values())))
print()
allf = list(frames.keys())

def describe(d):
    rest = d["rest"]
    tags = []
    if " c8 " in " " + rest:
        tags.append("C8")
    if rest.startswith("c0 ") or " c0 " in " " + rest:
        tags.append("PROJ")
    if "gpuw" in rest:
        tags.append("GPUW-TEX")
    if "rbpend" in rest:
        tags.append("RBPEND")
    return " ".join(tags)

# Pairs that are NOT in every frame: candidates for the transition.
print("=== shader pairs present in only some of the frames (first..last frame, frames present, draws/frame max) ===")
rows = []
for key, fm in pair_frames.items():
    if len(fm) == nframes:
        continue
    fs = sorted(fm)
    rows.append((fs[0], fs[-1], len(fm), max(fm.values()), key))
rows.sort()
for f0, f1, cnt, mx, key in rows:
    d = pair_example[key]
    print("vs %s ps %s  frames %d..%d (%d of %d)  max %d/frame  z %s blend %s  verts %d  %s" %
          (key[0], key[1], f0, f1, cnt, nframes, mx, d["z"], d["blend"], d["verts"], describe(d)))
    print("      " + d["rest"][:300])
print()
print("=== per-frame: draws, distinct pairs, c8 draws, c8 draws with x-range >= 1200, projection draws with depth off ===")
for fr, ds in frames.items():
    c8 = [d for d in ds if " c8 " in " " + d["rest"]]
    wide = 0
    for d in c8:
        m = re.search(r" x (-?[\d.]+)\.\.(-?[\d.]+)", d["rest"])
        if m and float(m.group(2)) - float(m.group(1)) >= 1200:
            wide += 1
    proj_z0 = [d for d in ds if ("c0 " in d["rest"]) and d["z"].startswith("0")]
    pairs = len(set((d["vs"], d["ps"]) for d in ds))
    print("frame %3d: %5d draws  %3d pairs  c8 %3d (wide %d)  proj-z0 %d" % (fr, len(ds), pairs, len(c8), wide, len(proj_z0)))
