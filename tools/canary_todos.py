"""Find the TODOs our fork still carries that Xenia Canary has since resolved.

    python tools/canary_todos.py

Function-count and line-count deltas between the two trees are dominated by
Canary's performance work (GetScissorTmpl, for instance, is the same arithmetic
written in SSE4 with an identical scalar fallback), so they rank refactoring
above fixes and are a poor guide to what is actually missing.

A TODO is a much better signal. Both trees inherit the same comments from the
same upstream author, so a TODO that is still in our copy and GONE from Canary's
marks work that was finished after the SDK forked - a fix we do not have.

This found the extended-range float16 conversion, which our tree still marks
`TODO(Triang3l): Use extended range conversion.` in three places while Canary
implements it: the Xbox 360's float16 has no Inf and no NaN, and exponent 31
holds finite values up to 131008.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OURS = os.path.join(ROOT, "..", "rexglue-src", "src", "graphics")
CANARY = os.path.join(ROOT, "..", "..", "Ninja Gaiden 2 Xbox360",
                      "xenia-canary-src", "src", "xenia", "gpu")

# A TODO and every `//` comment line that continues it. Matching only the first
# line makes the same TODO wrapped at a different column look like two different
# TODOs, which reported several as "resolved by Canary" when they were merely
# re-wrapped - the two trees are formatted to different column limits, so this
# is the common case, not a corner one.
TODO = re.compile(r"//[ \t]*(TODO\([^)]*\):?[^\n]*(?:\n[ \t]*//[^\n]*)*)", re.M)

# Compare a prefix rather than the whole block: the trees also disagree about
# how much of a trailing thought belongs to the TODO.
PREFIX_WORDS = 12


def normalise(s):
    """Comparable text: one line, no author, no punctuation, no case."""
    s = re.sub(r"//", " ", s)
    s = re.sub(r"TODO\([^)]*\):?", "", s)
    s = re.sub(r"[^a-z0-9 ]", " ", s.lower())
    return " ".join(s.split()[:PREFIX_WORDS])


def collect(root, exts):
    """normalised todo -> [(file, line, raw)]"""
    out = {}
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d != ".git"]
        for f in files:
            if not f.endswith(exts):
                continue
            p = os.path.join(base, f)
            try:
                text = open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            rel = os.path.relpath(p, root).replace("\\", "/")
            for m in TODO.finditer(text):
                key = normalise(m.group(1))
                if len(key) < 12:      # "todo fix this" carries no information
                    continue
                line = text.count("\n", 0, m.start()) + 1
                out.setdefault(key, []).append((rel, line, m.group(1).strip()))
    return out


def main():
    if not os.path.isdir(CANARY):
        sys.exit("Canary tree not found: %s" % CANARY)

    ours = collect(OURS, (".cpp", ".h"))
    canary = collect(CANARY, (".cc", ".h"))

    resolved = {k: v for k, v in ours.items() if k not in canary}
    print("TODOs in our tree:      %d" % len(ours))
    print("TODOs in Canary:        %d" % len(canary))
    print("ours that Canary lacks: %d  <- candidates for a port\n" % len(resolved))

    # Rank by how central the file is to drawing a frame.
    def weight(files):
        p = files[0][0]
        for i, frag in enumerate(("shader", "render_target", "draw", "texture",
                                  "command_processor", "primitive")):
            if frag in p:
                return i
        return 99

    for key in sorted(resolved, key=lambda k: (weight(resolved[k]), k)):
        sites = resolved[key]
        print("%s" % sites[0][2])
        for rel, line, _ in sites:
            print("      %s:%d" % (rel, line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
