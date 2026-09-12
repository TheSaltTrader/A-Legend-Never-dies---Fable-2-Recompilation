"""RowStart() outside a table takes the process down.

RowStart calls ImGui::TableNextRow, which dereferences the current table
WITHOUT a null check. Called with no BeginTable/EndTable pair open it reads
through a null pointer - on the NG2 port that was an access violation in
rexruntime the moment the Textures section drew, with nothing in the log.
Adding a control to a settings page is exactly when this happens, because the
tables end long before the section does.

This walks src/fable2_menu.cpp tracking BeginTable/EndTable depth per function
and reports every RowStart at depth 0. Exit 1 if any.

Run:  python tools/sweep_rowstart.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MENU = os.path.join(ROOT, "src", "fable2_menu.cpp")


def main():
    text = open(MENU, encoding="utf-8", errors="replace").read()
    depth = 0
    problems = []
    func = "?"
    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        m = re.match(r"^(?:bool|void)\s+(?:[A-Za-z_]+::)?([A-Za-z_]\w*)\(", line)
        if m:
            # A new function: whatever was open cannot carry over.
            func = m.group(1)
            depth = 0
        if "ImGui::BeginTable(" in stripped:
            depth += 1
            # A BeginTable whose result is ignored is the run-time half of the
            # same crash: it returns false when its window is collapsed or not
            # visible this frame, and the rows drawn after it then hit a null
            # table. The lexical check above cannot see that; this can.
            if not re.search(r"\b(if|while)\s*\(\s*!?\s*ImGui::BeginTable\(|=\s*ImGui::BeginTable\(",
                             stripped):
                problems.append((lineno, func, "BeginTable result ignored: " + stripped[:50]))
        if "ImGui::EndTable(" in stripped:
            depth = max(0, depth - 1)
        if re.search(r"\bRowStart\(", stripped) and not stripped.startswith("void RowStart"):
            if depth == 0:
                problems.append((lineno, func, stripped[:70]))
    if problems:
        print("RowStart outside any table (would crash in ImGui::TableNextRow):")
        for lineno, func, snippet in problems:
            print("  line %d in %s: %s" % (lineno, func, snippet))
        return 1
    print("sweep_rowstart: every RowStart in %s is inside a table" % os.path.relpath(MENU, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
