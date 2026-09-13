import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

# Measured on the same build, save load into Bowerstone Market, first 5 s in
# the world: budget 24 MB -> 52.6 fps, p99 70 ms, 15 hitches; budget off ->
# 54.5 fps, p99 45 ms, 8 hitches; later windows no better either. The pack
# copies were never the hitch: deferring them only spreads two-frame intervals
# over more frames. Off by default; kept as the A/B seam.
edit("src/graphics/d3d12/texture_cache.cpp", [
    ('REXCVAR_DEFINE_INT32(texture_pack_upload_budget_mb, 24, "GPU",\n'
     '                     "Pack texture bytes uploaded per frame before the rest wait for "\n'
     '                     "later frames (0 = no limit)")\n',
     'REXCVAR_DEFINE_INT32(texture_pack_upload_budget_mb, 0, "GPU",\n'
     '                     "Pack texture bytes uploaded per frame before the rest wait for "\n'
     '                     "later frames (0 = no limit). Off: measured a loss at 24 MB - "\n'
     '                     "the copies were never the hitch (Fable II, 2026-09-13)")\n'),
])
print("done")
