"""Binding-map differential: XenosRecomp (unverified) vs the SDK analysis (verified faithful).

For every shader the live census saw, pair XenosRecomp's HLSL/layout (named by
the JIT's block hash) with the SDK's bindings (named by the ucode hash) through
hashmap.txt, and compare:
  pixel:  the set of (fetch-constant slot, dimension) the HLSL body samples via
          g_Sampler<N>_Texture<2D|3D|Cube>DescriptorIndex  vs  the SDK's texture_bindings
  vertex: the set of (fetch-constant slot, stride words) in the .layout's vfetch lines
          vs the SDK's vertex_bindings
A slot one translator uses and the other does not is a translation divergence -
the class the striped branches would be in. Reports MATCH / MISMATCH per shader
and totals; never asserts which side is right (the SDK side has 406/406 behind it).
"""
import os, re, sys, glob

BD = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\wt-fable2-nativegpu\out\build\win-amd64-Release"
CENSUS = os.path.join(BD, "ngpu_shader_census")
JIT = os.path.join(BD, "ngpu_jit")
CACHE = os.path.join(BD, "ngpu_cache")

DIM = {"2D": 1, "3D": 2, "Cube": 3}   # xenos FetchOpDimension: k1D=0, k2D=1, k3D=2, kCube=3

def load_hashmap():
    m = {}
    p = os.path.join(CENSUS, "hashmap.txt")
    if not os.path.exists(p):
        sys.exit("no hashmap.txt - run a leg with the bindings dump first")
    for line in open(p):
        f = line.split()
        if len(f) < 5 or f[1] != "jit" or f[3] != "ucode":
            continue
        kind, jit, ucode = f[0], f[2], f[4]
        m[(kind, ucode)] = jit
    return m

def sdk_bindings(path):
    tex, vtx = set(), set()
    flags = {}
    for line in open(path):
        f = line.split()
        if not f:
            continue
        if f[0] == "tex":
            tex.add((int(f[1]), int(f[3])))
        elif f[0] == "vtx":
            vtx.add((int(f[1]), int(f[3])))
        elif f[0] == "memexport":
            flags["memexport"] = int(f[1]); flags["dynaddr"] = int(f[3])
    return tex, vtx, flags

# XenosRecomp names each sampler by the shader's constant-table NAME and places
# its descriptor index in a cbuffer with a packoffset that encodes the fetch
# slot: `uint <name>_Texture2DDescriptorIndex : packoffset(cK.c)` -> slot K*4+c
# in the 2D block (c0-c7), 3D block at c8-c15, cube block at c16-c23. The body
# then calls tfetch2D(<name>_Texture2DDescriptorIndex, ...). So the slot map is
# declaration -> slot, and the used set is every <name>_Texture<dim> token that
# appears outside the declarations.
DECL = re.compile(r"uint\s+(\w+)_Texture(2D|3D|Cube)DescriptorIndex\s*:\s*packoffset\(c(\d+)\.([xyzw])\)")
USE = re.compile(r"\b(\w+)_Texture(2D|3D|Cube)DescriptorIndex\b")
COMP = {"x": 0, "y": 1, "z": 2, "w": 3}
BLOCK_BASE = {"2D": 0, "3D": 8, "Cube": 16}

def xr_pixel(hlsl_path):
    slot_of = {}
    body_uses = set()
    for line in open(hlsl_path, errors="replace"):
        s = line.strip()
        m = DECL.search(s)
        if m:
            name, dim, creg, comp = m.group(1), m.group(2), int(m.group(3)), m.group(4)
            slot_of[(name, dim)] = (creg - BLOCK_BASE[dim]) * 4 + COMP[comp]
            continue
        if s.startswith("#define") or s.startswith("uint ") or s.startswith("cbuffer"):
            continue
        for m in USE.finditer(s):
            name, dim = m.group(1), m.group(2)
            if (name, dim) in slot_of:
                body_uses.add((slot_of[(name, dim)], DIM[dim]))
    return body_uses

def xr_vertex(layout_path):
    # The SDK numbers vertex fetch constants 0..95 from the top of the fetch
    # constant file (D3D9 stream s lives at fetch constant 95 - s); XenosRecomp's
    # layout carries the stream number. Both are converted to the SDK numbering.
    vtx = set()
    for line in open(layout_path, errors="replace"):
        if not line.startswith("vfetch "):
            continue
        f = line.split()
        # vfetch SEM idx stream off stride fmt num comp sgn swz mini type [x]
        try:
            stream, stride = int(f[3]), int(f[5])
        except (IndexError, ValueError):
            continue
        vtx.add((95 - stream, stride // 4))
    return vtx

XR_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xr_hlsl")   # xr_translate.py's output, named by ucode hash

def main():
    stats = {"pixel": [0, 0, 0], "vertex": [0, 0, 0]}   # compared, match, no-xr-file
    mism = []
    for bpath in sorted(glob.glob(os.path.join(CENSUS, "*.bindings.txt"))):
        name = os.path.basename(bpath)
        ucode, kind_c = name[:16], name[17]
        kind = "pixel" if kind_c == "p" else "vertex"
        tex, vtx, flags = sdk_bindings(bpath)
        if kind == "pixel":
            xr_path = os.path.join(XR_OUT, f"{ucode}_p.hlsl")
            if not os.path.exists(xr_path):
                stats[kind][2] += 1
                continue
            xr = xr_pixel(xr_path)
            stats[kind][0] += 1
            if xr == tex:
                stats[kind][1] += 1
            else:
                mism.append((kind, ucode, f"SDK tex {sorted(tex)} vs XenosRecomp {sorted(xr)}"))
        else:
            xr_path = os.path.join(XR_OUT, f"{ucode}_v.hlsl.layout")
            if not os.path.exists(xr_path):
                stats[kind][2] += 1
                continue
            xr = xr_vertex(xr_path)
            stats[kind][0] += 1
            if xr == vtx:
                stats[kind][1] += 1
            else:
                mism.append((kind, ucode, f"SDK vtx {sorted(vtx)} vs XenosRecomp {sorted(xr)}"))
    for k in ("pixel", "vertex"):
        c, m, nofile = stats[k]
        print(f"{k}: compared {c}, MATCH {m}, MISMATCH {c - m}, no XenosRecomp file for {nofile} (unmeasured)")
    for kind, ucode, d in mism:
        print(f"MISMATCH {kind} {ucode}: {d}")

if __name__ == "__main__":
    main()
