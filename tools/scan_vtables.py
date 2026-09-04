"""Find missed functions by walking pointer tables, not by decoding .text.

The run -> crash -> register -> rebuild loop finds one missed function per
rebuild, and each rebuild is minutes. This finds them in bulk.

ng2recomp's `scan_missed.py` tried the obvious thing - scan .text for anything
that looks like a function prologue - and it does not work: .text is full of
pointer tables, and a pointer beginning 0x82.. decodes as a perfectly plausible
`lwz`. It produced ~1,340 candidates on NG2, mostly false.

This works the other way round. The functions the analyzer misses are the ones
nothing calls directly - they are reached through vtables and dispatch tables,
which are *data*. So:

  1. read every 4-byte-aligned big-endian word in .rdata and .data;
  2. keep the ones that point into .text;
  3. drop the ones the analyzer already treats as a function start;
  4. keep only those in a run of >= MIN_RUN consecutive code pointers, so a
     lone integer that happens to look like an address is ignored;
  5. require the instruction *before* the target to be one control cannot fall
     through - blr, bctr, an unconditional b, or padding.

Step 5 is what makes a false positive cheap. Registering an address that is
really the middle of a straight-line function would cut that function short; if
the preceding instruction cannot fall through, splitting there costs nothing
even when the guess is wrong.

    python tools/scan_vtables.py                 # report
    python tools/scan_vtables.py --check         # verify against known answers
    python tools/scan_vtables.py --write         # (re)generate the TOML block
    python tools/scan_vtables.py --prune         # write, run codegen, repeat
                                                 # until codegen is clean

## Why --prune exists

Splitting a function is not always free. A `b` that used to be an internal jump
- a loop back-edge, or a jump to a shared epilogue - can end up crossing the new
boundary, and the recompiler cannot emit a jump across a C++ function boundary.
Codegen reports those as `Unresolved b target 0xT from 0xS`, and a control run
with only the hand-found overrides produced ZERO of them, so every one is ours.

`--prune` is the fixpoint loop: run codegen, add whatever it complained about to
`config/vtable_exclude.txt`, regenerate the whole block from scratch, repeat.

Regenerating rather than deleting lines matters. Each entry's size is clamped to
the next function boundary, so removing one entry leaves its predecessor sized
to a boundary that no longer exists - a hole the analyzer does not cover, which
shows up as a *new* unresolved call. Only a full regeneration re-clamps
correctly.

## Why --check exists

NG2's `xrefs.py` shipped three bugs that each gave confident wrong answers, so a
new scanner has to be validated against a known answer before it is believed.
`--check` re-derives the functions already registered by hand and reports how
many this scan would have found.
"""

import argparse
import bisect
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import add_function  # noqa: E402
from xex_image import XexImage, pdata_functions  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOML = os.path.join(ROOT, "config", "functions.toml")
EXCLUDE = os.path.join(ROOT, "config", "vtable_exclude.txt")
PARTITION = os.path.join(ROOT, "generated", "default", "codegen.partition.json")
MANIFEST = "fable2_manifest.toml"

BLOCK_HEADER = "# --- BEGIN tools/scan_vtables.py ---"
BLOCK_FOOTER = "# --- END tools/scan_vtables.py ---"

BLR = 0x4E800020
BCTR = 0x4E800420
NOP = 0x60000000
MIN_RUN = 2          # consecutive code pointers before a table is believable
POINTER_SECTIONS = (".rdata", ".data")

UNRESOLVED_B = re.compile(
    r"^Unresolved b target 0x([0-9A-Fa-f]{8}) from 0x([0-9A-Fa-f]{8})", re.M)
UNRESOLVED_CALL = re.compile(
    r"^\s*0x([0-9A-Fa-f]{8}) from 0x([0-9A-Fa-f]{8}):", re.M)
# An address in the import-thunk table at the end of .text. Codegen keeps the
# import and drops our entry, so it registers a name it never emits a body for
# and the LINK fails - after a full compile - with "undefined symbol:
# sub_XXXXXXXX_vtable". Codegen says so first; this reads it.
OUTRANKED = re.compile(
    r"^\[functions\] 0x([0-9A-Fa-f]{8}) outranked by import", re.M)


# --- image analysis ---------------------------------------------------------

def is_terminator(w):
    """True if control cannot fall through this instruction."""
    if w in (BLR, BCTR, NOP, 0):
        return True
    op = w >> 26
    if op == 18 and not (w & 1):          # unconditional b / ba (not bl)
        return True
    if op == 19 and ((w >> 1) & 0x3FF) in (16, 528) and not (w & 1):
        return True                        # blr / bctr forms with LK=0
    return False


def generated_addresses():
    """The addresses currently inside this tool's own block in the TOML."""
    text = open(TOML, encoding="utf-8").read()
    if BLOCK_HEADER not in text:
        return set()
    block = text.split(BLOCK_HEADER, 1)[1].split(BLOCK_FOOTER, 1)[0]
    return {int(m, 16) for m in re.findall(r"^\s*0x([0-9A-Fa-f]+)\s*=", block, re.M)}


def known_starts(img):
    """Every address the ANALYZER treats as a function start, on its own.

    Two things are easy to get wrong here and both give confident nonsense.

    First, this has to be the analyzer's list, not `.pdata`. Its Discover and
    GapFill phases find thousands of functions beyond the unwind table -
    60,219 registered against 46,072 `.pdata` entries - so comparing against
    `.pdata` reported 4,323 "missing" functions where there are 121.

    Second, `codegen.partition.json` is written by the last codegen run, which
    included whatever this tool wrote last time. Reading it raw makes the tool
    treat its own previous output as the analyzer's opinion, and the candidate
    list collapses to almost nothing on the second run (121 -> 13, observed).
    Subtracting the generated block fixes it: registering a function only ever
    *splits* an analyzer function, never merges two, so everything else in the
    partition is genuinely the analyzer's.
    """
    if os.path.exists(PARTITION):
        with open(PARTITION, encoding="utf-8") as f:
            data = json.load(f)
        return {int(a, 16) for a in data.get("assignments", {})} - generated_addresses()
    print("warning: no codegen.partition.json - falling back to .pdata, which "
          "will over-report massively", file=sys.stderr)
    return set(pdata_functions(img))


def flush(found, run, starts, img, tlo):
    if len(run) < MIN_RUN:
        return
    for slot, target in run:
        if target in starts or target <= tlo:
            continue
        if not is_terminator(img.word(target - 4)):
            continue                       # would split a fall-through
        found.setdefault(target, slot)


def candidates(img, starts):
    text = next(((va, size) for name, va, size in img.sections
                 if name == ".text"), None)
    if not text:
        sys.exit("no .text section")
    tlo, thi = text[0], text[0] + text[1]

    found = {}
    for name, va, size in img.sections:
        if name not in POINTER_SECTIONS:
            continue
        run = []
        for addr in range(va, va + (size & ~3), 4):
            if not img.contains(addr):
                break
            word = img.word(addr)
            if tlo <= word < thi and (word & 3) == 0:
                run.append((addr, word))
                continue
            flush(found, run, starts, img, tlo)
            run = []
        flush(found, run, starts, img, tlo)
    return found


def sized_rows(img, pdata, found, boundaries):
    """(target, size, owner, slot), with each size clamped to the next start.

    function_extent walks forward to a terminator and will run straight through
    a later entry point; codegen then refuses the whole manifest with
    "Overlapping boundaries: 0x... overlaps 0x...".
    """
    rows = []
    for target in sorted(found):
        try:
            size = add_function.function_extent(img, target)
        except SystemExit:
            continue                       # no terminator in range: not code
        j = bisect.bisect_right(boundaries, target)
        if j < len(boundaries):
            size = min(size, boundaries[j] - target)
        if size <= 0:
            continue
        i = bisect.bisect_right(pdata, target) - 1
        rows.append((target, size, pdata[i] if i >= 0 else 0, found[target]))
    return rows


# --- the generated block ----------------------------------------------------

def read_exclusions():
    if not os.path.exists(EXCLUDE):
        return set()
    out = set()
    for line in open(EXCLUDE, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(int(line, 16))
    return out


def add_exclusions(new, why):
    with open(EXCLUDE, "a", encoding="utf-8") as f:
        for addr in sorted(new):
            f.write(f"0x{addr:08X}  # {why}\n")


def strip_block(text):
    if BLOCK_HEADER not in text:
        return text
    head, rest = text.split(BLOCK_HEADER, 1)
    if BLOCK_FOOTER in rest:
        rest = rest.split(BLOCK_FOOTER, 1)[1]
        return head.rstrip("\n") + "\n" + rest.lstrip("\n")
    return head.rstrip("\n") + "\n"


def write_block(rows):
    text = strip_block(open(TOML, encoding="utf-8").read()).rstrip("\n")
    lines = [
        "", "", BLOCK_HEADER,
        "# Regenerated by tools/scan_vtables.py - do not hand-edit; put any",
        "# address that must NOT be registered in config/vtable_exclude.txt",
        "# instead, then re-run with --write.",
        "#",
        "# Each of these is pointed to from a vtable or dispatch table in",
        "# .rdata/.data, is not a function start the analyzer already knows,",
        "# and is preceded by an instruction control cannot fall through.",
    ]
    for target, size, owner, slot in rows:
        lines.append(f'0x{target:08X} = {{ name = "sub_{target:08X}_vtable", '
                     f'size = 0x{size:X} }}  # in 0x{owner:08X}, ptr at 0x{slot:08X}')
    lines += [BLOCK_FOOTER, ""]
    open(TOML, "w", encoding="utf-8").write(text + "\n".join(lines))


def regenerate(img, pdata):
    """Recompute the whole block from the image and the exclusion list."""
    excluded = read_exclusions()
    hand = add_function.existing_addresses(strip_block_file())
    starts = known_starts(img) | hand
    found = {a: s for a, s in candidates(img, starts).items()
             if a not in excluded}
    rows = sized_rows(img, pdata, found, sorted(starts | set(found)))
    write_block(rows)
    return rows, excluded


def strip_block_file():
    """The hand-written part of functions.toml, as a temp path for parsing."""
    text = strip_block(open(TOML, encoding="utf-8").read())
    tmp = os.path.join(ROOT, "out", "functions.hand.toml")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    open(tmp, "w", encoding="utf-8").write(text)
    return tmp


# --- prune ------------------------------------------------------------------

def run_codegen(sdk_bin):
    proc = subprocess.run([sdk_bin, "codegen", MANIFEST, "--ignore-stamp"],
                          cwd=ROOT, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def prune(img, pdata, sdk_bin, rounds):
    for round_no in range(1, rounds + 1):
        rows, excluded = regenerate(img, pdata)
        code, log = run_codegen(sdk_bin)
        cross = {(int(t, 16), int(s, 16)) for t, s in UNRESOLVED_B.findall(log)}
        calls = set()
        if "=== ANALYSIS ERRORS ===" in log:
            tail = log.split("=== ANALYSIS ERRORS ===", 1)[1]
            calls = {(int(t, 16), int(s, 16))
                     for t, s in UNRESOLVED_CALL.findall(tail)}

        registered = {addr for addr, _, _, _ in rows}
        outranked = {int(a, 16) for a in OUTRANKED.findall(log)} & registered

        print(f"round {round_no}: {len(rows)} entries "
              f"({len(excluded)} excluded), codegen exit {code}, "
              f"{len(cross)} cross-boundary branch(es), "
              f"{len(calls)} unresolved call(s), "
              f"{len(outranked)} outranked by import")

        if code == 0 and not cross and not outranked:
            print("clean")
            return 0

        if outranked:
            add_exclusions(outranked, "import thunk - codegen drops our entry")
            for addr in sorted(outranked):
                print(f"  excluding 0x{addr:08X} (import thunk)")
            if not cross and not calls:
                continue
        doomed = set()
        for target, source in cross | calls:
            lo, hi = min(target, source), max(target, source)
            # The split that separated the branch from its target is the entry
            # whose start lies between them. Widen by one instruction so an
            # entry sitting exactly on the branch source is caught too.
            doomed |= {a for a in registered if lo - 4 <= a <= hi}
        if not doomed:
            print("  nothing of ours sits between these branches and their "
                  "targets - not caused by this tool:")
            for target, source in sorted(cross | calls):
                print(f"    0x{target:08X} from 0x{source:08X}")
            return 1

        add_exclusions(doomed, "split broke a branch")
        for addr in sorted(doomed):
            print(f"  excluding 0x{addr:08X}")

    print(f"still not clean after {rounds} rounds")
    return 1


# --- entry point ------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xex", default=os.path.join(ROOT, "assets", "default.xex"))
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--prune", action="store_true")
    ap.add_argument("--rounds", type=int, default=10)
    ap.add_argument("--limit", type=int, default=20)
    args = ap.parse_args()

    img = XexImage.load(args.xex)
    pdata = pdata_functions(img)

    if args.prune:
        sdk = os.environ.get("REXSDK")
        if not sdk:
            sys.exit("REXSDK is not set - point it at the ReXGlue SDK root.")
        return prune(img, pdata, os.path.join(sdk, "bin", "rexglue.exe"),
                     args.rounds)

    if args.check:
        manual = sorted(add_function.existing_addresses(strip_block_file()))
        starts = known_starts(img) - set(manual)   # pretend we knew none
        found = candidates(img, starts)
        hit = [a for a in manual if a in found]
        print(f"{len(hit)}/{len(manual)} hand-registered functions are "
              f"reachable by this scan")
        for a in manual:
            why = ("found" if a in found else
                   "MISSED - previous instruction falls through"
                   if not is_terminator(img.word(a - 4)) else
                   "MISSED - no pointer to it in .rdata/.data")
            print(f"  0x{a:08X}  {why}")
        return 0

    if args.write:
        rows, excluded = regenerate(img, pdata)
        print(f"wrote {len(rows)} entries ({len(excluded)} excluded) to {TOML}")
        return 0

    starts = known_starts(img) | add_function.existing_addresses(strip_block_file())
    excluded = read_exclusions()
    found = {a: s for a, s in candidates(img, starts).items() if a not in excluded}
    rows = sized_rows(img, pdata, found, sorted(starts | set(found)))
    print(f"{len(rows)} candidate function(s) pointed to from a table but not "
          f"registered ({len(excluded)} excluded)")
    for target, size, owner, slot in rows[:args.limit]:
        print(f"  0x{target:08X}  size 0x{size:<5X} inside 0x{owner:08X}  "
              f"(pointer at 0x{slot:08X})")
    if len(rows) > args.limit:
        print(f"  ... and {len(rows) - args.limit} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
