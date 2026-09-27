"""Census: translated shaders where an `if (!pN)` block sits INSIDE an enclosing `if (pN)` block (dead code by
construction - the XenosRecomp control-flow mis-nesting found in 338B8AB7). Walks braces; counts per file."""
import glob, os, re, sys
D = sys.argv[1] if len(sys.argv) > 1 else r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\wt-fable2-nativegpu\out\build\win-amd64-Release\ngpu_jit"
hits = []
for f in sorted(glob.glob(os.path.join(D, "*.hlsl"))):
    lines = open(f, encoding="utf-8", errors="replace").read().splitlines()
    stack = []   # predicate condition of each open block (or None)
    dead = 0; pending = None
    for ln in lines:
        s = ln.strip()
        m = re.match(r"if \((!?)(p\d)\)$", s)
        if m: pending = (m.group(1) == "!", m.group(2)); continue
        if s == "{":
            if pending and any(c and c[1] == pending[1] and c[0] != pending[0] for c in stack): dead += 1
            stack.append(pending); pending = None; continue
        if s.startswith("}"):
            if stack: stack.pop()
            continue
        pending = None
    if dead: hits.append((dead, os.path.basename(f)))
hits.sort(reverse=True)
total = len(glob.glob(os.path.join(D, "*.hlsl")))
print(f"shaders with a contradictory nested predicate block: {len(hits)} of {total} translated")
for n, f in hits[:25]: print(f"  {n:4d} dead blocks  {f}")
