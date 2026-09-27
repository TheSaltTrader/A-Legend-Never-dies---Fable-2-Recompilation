"""reach.py DIR: static reachability of predicated blocks in XenosRecomp pc/switch shaders.
Cases are parsed in order; control falls through case k -> k+1 unless an unconditional `pc = X; continue;`; a
conditional jump `if (pN) { pc = X; continue; }` sends the (pN true) states to X and lets (pN false) fall through.
p0 state per path = subset of {True, False}; an assignment `p0 = ...` makes it {T, F}. A block `if (p0)` / `if (!p0)`
is REACHABLE if it is entered with a state set containing that value. Reports, per shader, predicated blocks that
are never reachable (should be 0 after the fix), and the count of reachable ones."""
import re, sys, glob, os
D = sys.argv[1]
for f in sorted(glob.glob(os.path.join(D, "*.hlsl"))):
    text = open(f, encoding="utf-8", errors="replace").read()
    if "switch (pc)" not in text: continue
    body = text[text.index("switch (pc)"):]
    parts = re.split(r"\n\t*case (\d+):", body)
    cases = []   # (label, code)
    for i in range(1, len(parts) - 1, 2): cases.append((int(parts[i]), parts[i + 1]))
    idx = {lab: k for k, (lab, _) in enumerate(cases)}
    entry = [set() for _ in cases]; entry[0] = {True, False} if False else {None}
    # state: None = p0 not yet assigned (treated as both)
    changed = True
    blocks_reach = {}   # (case, blockno) -> bool
    while changed:
        changed = False
        for k, (lab, code) in enumerate(cases):
            st = set(entry[k])
            if not st: continue
            st = {True, False} if None in st else st
            lines = code.split("\n"); j = 0; bno = 0; fall = True
            while j < len(lines):
                s = lines[j].strip()
                if re.match(r"p0 = ", s): st = {True, False}
                m = re.match(r"if \((!?)p0\)$", s)
                if m:
                    want = (m.group(1) == "")
                    # block content until matching brace
                    depth = 0; blk = []; j += 1
                    while j < len(lines):
                        t = lines[j].strip(); blk.append(t)
                        if t == "{": depth += 1
                        elif t == "}": depth -= 1
                        if depth == 0 and t == "}": break
                        j += 1
                    reach = want in st
                    key = (k, bno); bno += 1
                    if blocks_reach.get(key) != reach and reach: blocks_reach[key] = True; changed = True
                    blocks_reach.setdefault(key, reach)
                    jm = re.search(r"pc = (\d+);", "\n".join(blk))
                    if jm and "continue;" in blk:
                        tgt = idx.get(int(jm.group(1)))
                        if tgt is not None and reach:
                            ns = {want}
                            if not ns <= entry[tgt]: entry[tgt] |= ns; changed = True
                        st = st - {want}   # only the other value falls through
                        if not st: fall = False; break
                j += 1
            if fall and k + 1 < len(cases) and "return;" not in code.split("\n")[-3:]:
                if not st <= entry[k + 1]: entry[k + 1] |= st; changed = True
    dead = sum(1 for v in blocks_reach.values() if not v); live = sum(1 for v in blocks_reach.values() if v)
    print(f"{os.path.basename(f):28s} predicated blocks: reachable {live}, NEVER reachable {dead}")
