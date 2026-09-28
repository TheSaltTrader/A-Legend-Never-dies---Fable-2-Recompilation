#!/usr/bin/env python3
"""P2 census: inject the EXIT hook into the generated code of every hooked Direct3D entry point.

The entry hooks (config/hooks/native_gpu_trace.toml) were emitted by the codegen at each function's first
instruction; the census also needs the exit, and functions have no `blr` (they leave by tail calls), so
the reliable exit point is every `return;` of the recompiled body. This script inserts `fable2_p2_exit(<hook index>);`
before each `return;` inside `DEFINE_REX_FUNC(sub_XXXXXXXX)` for the 304 addresses of src/native_gpu_trace.cpp
(the index is that table's order, the one fable2::p2::Enter receives), and one `extern "C" void fable2_p2_exit(int);`
per touched file. Idempotent; --revert removes both again. The generated tree is untracked (pinned from the main
repo), so this is a build-local change: run it before building a census exe, --revert before a release build.

Usage: p2_inject_exits.py [--revert] [--root C:/users/renoi/claudecode/Fable 2 Recompile Xbox/wt-fable2-nativegpu]"""
import glob
import os
import re
import sys

root = r"C:/users/renoi/claudecode/Fable 2 Recompile Xbox/wt-fable2-nativegpu"
if "--root" in sys.argv:
    root = sys.argv[sys.argv.index("--root") + 1]
revert = "--revert" in sys.argv
DECL = 'extern "C" void fable2_p2_exit(int);   // P2 census exit hook (tools/native_gpu/p2_inject_exits.py)\n'

trace = open(os.path.join(root, "src", "native_gpu_trace.cpp"), encoding="utf-8").read()
addrs = [int(m.group(1), 16) for m in re.finditer(r"\{0x([0-9A-F]{8}), \"", trace)]
index = {a: i for i, a in enumerate(addrs)}
assert len(addrs) == 304, len(addrs)

if "--check" in sys.argv:
    # After a build: every hooked function must still carry its exit (a codegen run rewrites the generated files and
    # drops them - Fable 2026-09-27, a census exe with 0 exits).
    have = 0
    for path in sorted(glob.glob(os.path.join(root, "generated", "default", "fable2_recomp.*.cpp"))):
        t = open(path, encoding="utf-8").read()
        for a in addrs:
            i = t.find("DEFINE_REX_FUNC(sub_%08X) {" % a)
            body = t[i:t.find("\n}\n", i)] if i >= 0 else ""
            # Every return must carry its exit, not just one (nested returns, 2026-09-27).
            returns = len(re.findall(r"\n\t+return;", body)) + len(re.findall(r"\n\t+if \(.*\) (?:\{ fable2_p2_exit\(\d+\); )?return;", body))
            if i >= 0 and body.count("fable2_p2_exit(") == returns:
                have += 1
    print("check: %d of %d hooked functions carry the exit" % (have, len(addrs)))
    sys.exit(0 if have == len(addrs) else 2)

touched_files = 0
inserted = 0
removed = 0
found = set()
for path in sorted(glob.glob(os.path.join(root, "generated", "default", "fable2_recomp.*.cpp"))):
    t = open(path, encoding="utf-8", errors="strict").read()
    orig = t
    if revert:
        t2 = re.sub(r"\t+fable2_p2_exit\(\d+\);   // P2\n", "", t)
        t2 = re.sub(r"\{ fable2_p2_exit\(\d+\); return; \}   // P2c\n", "return;\n", t2)
        removed += t.count("fable2_p2_exit(") - t2.count("fable2_p2_exit(")
        t2 = t2.replace(DECL, "")
        t = t2
    else:
        for a in addrs:
            key = "DEFINE_REX_FUNC(sub_%08X) {" % a
            i = t.find(key)
            if i < 0:
                continue
            found.add(a)
            j = t.find("\n}\n", i) + 1   # keep the newline that ends the body's last statement
            body = t[i:j]
            if "fable2_p2_exit(" in body:
                continue
            # Any indentation: 11 of Fable's hooked functions also return from inside a nested block ("\t\treturn;"),
            # and one missed exit in a hot function leaks a stack entry per call (census P2_F2: 188 M overflows).
            new_body = re.sub(r"\n(\t+)return;\n",
                              lambda m: "\n%sfable2_p2_exit(%d);   // P2\n%sreturn;\n" % (m.group(1), index[a], m.group(1)),
                              body)
            # One-line conditional returns, "if (cr6.eq) return;" - the codegen's early-out form (P2_F3: hook 66
            # entered 10.2 M times, exited 43,662: every early-out leaked a stack entry).
            new_body = re.sub(r"\n(\t+)if \((.*)\) return;\n",
                              lambda m: "\n%sif (%s) { fable2_p2_exit(%d); return; }   // P2c\n" % (m.group(1), m.group(2), index[a]),
                              new_body)
            inserted += new_body.count("fable2_p2_exit(")
            t = t[:i] + new_body + t[j:]
        if "fable2_p2_exit(" in t and DECL not in t:
            # after the file's include line
            k = t.find("\n", t.find("#include")) + 1
            t = t[:k] + DECL + t[k:]
    if t != orig:
        open(path, "w", encoding="utf-8", newline="\n").write(t)
        touched_files += 1

if revert:
    print("reverted: %d exit calls removed from %d files" % (removed, touched_files))
else:
    missing = [hex(a) for a in addrs if a not in found]
    print("inserted %d exit calls into %d files; functions found %d of %d%s" %
          (inserted, touched_files, len(found), len(addrs), ("; MISSING " + ", ".join(missing)) if missing else ""))
    if missing:
        sys.exit(2)
