"""Interpolator LINKAGE census over the VS/PS pairs the replay actually draws.

usage: linkage_census.py <ngpu_pairs.txt> <ngpu_cache dir> <out report>

For each drawn (VS, PS, variant) triple: disassemble both cached DXILs with dxc -dumpbin; from the VS take
the OUTPUT signature and, per element, whether any store writes a non-constant value (an element whose
every store is the literal 0.0 is "zero-only"); from the PS take the INPUT signature's 'Used' components.
A pair is a MEMBER OF THE CLASS if the PS uses a component of an element the VS lacks or writes only as 0.
Found by 0EF7C66A/5FEF6617 (2026-09-24): the VS put its 8th export in TEXCOORD7, the PS read COLOR0.w.
Pairs whose DXIL is missing are COUNTED as unchecked, never silently skipped.
"""
import os, re, subprocess, sys
from collections import defaultdict

DXC = r"C:\Users\renoi\ClaudeCode\NativeGPU\reference\XenosRecomp\thirdparty\dxc-bin\bin\x64\dxc.exe"
pairs_f, cache, out_f = sys.argv[1], sys.argv[2], sys.argv[3]
_dis = {}

def dis(path):
    if path not in _dis:
        r = subprocess.run([DXC, "-dumpbin", path], capture_output=True, text=True, errors="replace")
        _dis[path] = r.stdout
    return _dis[path]

def sig(text, which):
    """[(name, index, mask, used)] in table order for 'Input' or 'Output'."""
    m = re.search(r"; %s signature:\s*\n;\s*\n; Name.*\n; -+.*\n((?:; .*\n)+?);\s*\n" % which, text)
    rows = []
    if not m: return rows
    for line in m.group(1).splitlines():
        parts = line[1:].split()
        if len(parts) < 6: continue
        name, idx, mask = parts[0], parts[1], parts[2]
        used = parts[6] if len(parts) > 6 and which == "Input" else (parts[-1] if which == "Input" and len(parts) == 7 else "")
        rows.append((name, int(idx), mask, used))
    return rows

def ps_used(text):
    """PS input components actually used, from the 'Used' column (last field when present)."""
    m = re.search(r"; Input signature:\s*\n;\s*\n; Name.*\n; -+.*\n((?:; .*\n)+?);\s*\n", text)
    used = {}
    if not m: return used
    for line in m.group(1).splitlines():
        f = line[1:].split()
        if len(f) < 6 or f[0].startswith("SV_"): continue
        # Name Index Mask Register SysValue Format [Used]
        u = f[6] if len(f) >= 7 else ""
        if u: used[(f[0], int(f[1]))] = u
    return used

def vs_written(text):
    """VS output elements -> set of components written with a NON-constant value."""
    outs = sig(text, "Output")
    real = defaultdict(set); declared = {}
    for i, (name, idx, mask, _) in enumerate(outs):
        declared[i] = (name, idx)
    for m in re.finditer(r"storeOutput\.f32\(i32 5, i32 (\d+), i32 \d+, i8 (\d), float ([^)]+)\)", text):
        sid, comp, val = int(m.group(1)), int(m.group(2)), m.group(3).strip()
        if not re.fullmatch(r"[-+]?[0-9.]+e[+-][0-9]+|0\.0*", val):   # a literal constant is not a real write
            real[sid].add("xyzw"[comp])
    return {declared[k]: real.get(k, set()) for k in declared}

members, checked, unchecked = [], 0, []
for line in open(pairs_f):
    f = line.split()
    if len(f) < 4: continue
    vs, ps, var, n = f[0], f[1], f[2], int(f[3])
    vsp = os.path.join(cache, f"{vs}_{var}.dxil"); psp = os.path.join(cache, f"{ps}_p.dxil")
    if not (os.path.exists(vsp) and os.path.exists(psp)):
        unchecked.append((vs, ps, var, n, "vs dxil missing" if not os.path.exists(vsp) else "ps dxil missing")); continue
    checked += 1
    w = vs_written(dis(vsp)); u = ps_used(dis(psp))
    gaps = []
    for (name, idx), comps in u.items():
        have = w.get((name, idx))
        missing = [c for c in comps if not have or c not in have]
        if missing: gaps.append(f"{name}{idx}.{''.join(missing)}" + ("" if have is not None else " (not declared)"))
    if gaps: members.append((vs, ps, var, n, gaps))

with open(out_f, "w") as o:
    o.write(f"pairs checked {checked}, UNCHECKED {len(unchecked)}, MEMBERS (PS uses what the VS never really writes) {len(members)}\n")
    for vs, ps, var, n, gaps in sorted(members, key=lambda t: -t[3]):
        o.write(f"MEMBER vs {vs} ({var}) ps {ps} draws {n}: {'; '.join(gaps)}\n")
    for u in unchecked: o.write(f"UNCHECKED vs {u[0]} ({u[2]}) ps {u[1]} draws {u[3]}: {u[4]}\n")
print(open(out_f).read())
