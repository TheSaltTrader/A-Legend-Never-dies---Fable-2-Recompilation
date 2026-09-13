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

    python tools/scan_fnptrs.py                 # report
    python tools/scan_fnptrs.py --check         # verify against known answers
    python tools/scan_fnptrs.py --write         # (re)generate the TOML block
    python tools/scan_fnptrs.py --prune         # write, run codegen, repeat
                                                 # until codegen is clean

## Why --prune exists

Splitting a function is not always free. A `b` that used to be an internal jump
- a loop back-edge, or a jump to a shared epilogue - can end up crossing the new
boundary, and the recompiler cannot emit a jump across a C++ function boundary.
Codegen reports those as `Unresolved b target 0xT from 0xS`, and a control run
with only the hand-found overrides produced ZERO of them, so every one is ours.

`--prune` is the fixpoint loop: run codegen, add whatever it complained about to
`config/fnptr_exclude.txt`, regenerate the whole block from scratch, repeat.

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
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import add_function  # noqa: E402
from xex_image import XexImage, pdata_functions  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOML = os.path.join(ROOT, "config", "functions.toml")
EXCLUDE = os.path.join(ROOT, "config", "fnptr_exclude.txt")
PARTITION = os.path.join(ROOT, "generated", "default", "codegen.partition.json")
MANIFEST = "fable2_manifest.toml"

BLOCK_HEADER = "# --- BEGIN tools/scan_fnptrs.py ---"
BLOCK_FOOTER = "# --- END tools/scan_fnptrs.py ---"

BLR = 0x4E800020
BCTR = 0x4E800420
NOP = 0x60000000
MIN_RUN = 2          # consecutive code pointers before a table is believable
# 24, not 8: the 0x82DE2D48 callback-table builder (2026-09-12) keeps five
# `lis` results live across 16 instructions before the matching `addi`. The
# register-write invalidation below is what keeps a wide window honest.
MATERIALISE_WINDOW = 24  # instructions a lis result is trusted for
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

B_NEXT = 0x48000004      # `b $+4`: an unconditional branch that falls through


def is_terminator(w):
    """True if control cannot fall through this instruction.

    `b $+4` is the exception that cost a build. MSVC emits it as a no-op, and
    it is an unconditional `b`, so the obvious test says "control cannot fall
    through" - while it plainly does. Registering the instruction after one as
    a function start cuts a function in half:

        0x82FFD254  b 0x82FFD258        <- "terminator"
        0x82FFD258  lwz r3, 0x54(r31)   <- new "function" starts here

    r31 is the frame pointer, set up by the parent's prologue
    (`addi r31, r1, -0x80`). Under `non_volatile_as_local` each recompiled
    function owns its own r14-r31, so the split-off half reads 0x54 off a zero
    r31 - and the runtime says exactly that: "Unhandled guest access violation:
    read of guest 0x00000054". Found by bisecting 51 registrations over eight
    rebuilds; it is not something you would spot by reading them.
    """
    if w == B_NEXT:
        return False
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
        # The C++ exception tables in .rdata (a run at 0x8210Bxxx here) point
        # at catch funclets: blocks inside a function that read the frame
        # pointer the unwinder restores, and whose parent branches back into
        # them on the normal path. Registered as functions they cut the parent
        # and the codegen emitted REX_FATAL at eight branch sites (2026-09-12,
        # 0x82CC6B04 and three more). Same test as channel 2.
        if looks_like_continuation(img, target):
            continue
        found.setdefault(target, slot)


def owner(sorted_starts, addr):
    """The analyzer function containing `addr`, or None."""
    i = bisect.bisect_right(sorted_starts, addr) - 1
    return sorted_starts[i] if i >= 0 else None


def looks_like_jump_table(img, addr, tlo, thi, need=3):
    """True if `addr` starts a run of .text addresses rather than instructions.

    This is what channel 2 mostly finds if you let it. A PowerPC switch is

        lis  r11, hi          <- computes the TABLE's address, not a function's
        addi r10, r11, lo
        ...
        mtctr rN
        bctr
        <table of .text addresses>

    so the table base is a `lis`/`addi` result inside .text, immediately after a
    `bctr`, and it satisfies every other test. Worse, a table entry like
    0x8221A4F0 disassembles as a perfectly plausible `lwz r17, ...` - which is
    exactly the trap that made ng2recomp's `scan_missed.py` useless.

    A sample of ten channel-2 candidates before this filter contained nine
    tables and one real function. Reading the words as data instead of as code
    separates them cleanly.
    """
    for k in range(need):
        w = img.word(addr + 4 * k)
        if not (tlo <= w < thi and not w & 3):
            return False
    return True


def branch_targets(words, va):
    """Every address a direct, non-linking branch in .text jumps to.

    `b`/`ba` (op 18) and `bc`/`bca` (op 16) with LK=0. A `bl` target is a call,
    which is a function start by definition and no threat to anything; what
    this set is for is telling a label inside a function from a thunk that
    only ever gets reached through a pointer.
    """
    out = set()
    for i, w in enumerate(words):
        op = w >> 26
        if w & 1:
            continue
        if op == 18:
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            out.add(li if w & 2 else va + i * 4 + li)
        elif op == 16:
            bd = w & 0xFFFC
            if bd & 0x8000:
                bd -= 0x10000
            out.add(bd if w & 2 else va + i * 4 + bd)
    return out


NON_VOLATILE = set(range(14, 32))
MFLR_R12 = 0x7D8802A6


def _reads_writes(w):
    """The GPRs an instruction reads and writes - the subset the continuation
    test needs. Anything not decoded reads nothing and writes nothing, which
    errs toward calling a block a function (the side the old code was on)."""
    op = w >> 26
    rD = (w >> 21) & 0x1F
    rA = (w >> 16) & 0x1F
    rB = (w >> 11) & 0x1F
    reads, writes = set(), set()
    if op == 15 and rA == 0:                       # lis
        writes.add(rD)
    elif op in (7, 8, 12, 13, 14, 15):             # mulli subfic addic addi addis
        reads.add(rA); writes.add(rD)
    elif op in (32, 33, 34, 35, 40, 41, 42, 43, 58):   # integer loads
        reads.add(rA); writes.add(rD)
        if op in (33, 35, 41, 43):
            writes.add(rA)
    elif op in (36, 37, 38, 39, 44, 45, 62):       # integer stores
        reads.add(rA); reads.add(rD)
        if op in (37, 39, 45):
            writes.add(rA)
    elif op in (10, 11):                           # cmpli cmpi
        reads.add(rA)
    elif op in (20, 21, 23, 24, 25, 26, 27, 28, 29):   # rlwimi rlwinm rlwnm ori..andis
        reads.add(rD); writes.add(rA)
        if op == 23:
            reads.add(rB)
    elif op == 31:
        xo = (w >> 1) & 0x3FF
        if xo in (444, 28, 316, 124, 476, 284, 412, 60, 24, 536, 792, 824,
                  26, 58, 922, 954, 986):          # logical, shifts, cntlz, exts
            reads.add(rD); reads.add(rB); writes.add(rA)
        elif xo in (151, 215, 407, 183, 247, 439, 149, 181):   # indexed stores
            reads.add(rD); reads.add(rA); reads.add(rB)
        elif xo in (467, 144):                     # mtspr mtcrf
            reads.add(rD)
        elif xo in (0, 32):                        # cmp cmpl
            reads.add(rA); reads.add(rB)
        elif xo in (339, 19, 83):                  # mfspr mfcr mfmsr
            writes.add(rD)
        else:                                      # loads and arithmetic
            reads.add(rA); reads.add(rB); writes.add(rD)
    reads.discard(0)
    return reads, writes


def looks_like_continuation(img, target, limit=24):
    """True if the code at `target` is the MIDDLE of a function: it uses a
    non-volatile register, or the caller's stack frame, before writing it.

    A function starts with nothing in r14-r31 it may rely on, and with r1 as
    the caller's frame (arguments at positive offsets excepted, which are
    rare for the callbacks this tool is after and are counted as frame use
    here on purpose - the other error is the one that crashes). Three of the
    twenty sampled candidates at 0x82FFBEDC, 0x82CB700C and 0x830F4E54
    (2026-09-12) were blocks like that, each five instructions after the
    `lis/addi` that named them: a continuation the parent stores, not a
    function anyone calls.
    """
    written = set()
    for k in range(0, limit * 4, 4):
        w = img.word(target + k)
        op = w >> 26
        if w == MFLR_R12 or (op == 37 and ((w >> 16) & 0x1F) == 1):
            return False                           # a prologue: a real function
        reads, writes = _reads_writes(w)
        for r in reads:
            if r in NON_VOLATILE and r not in written:
                return True
        # r1 with a NON-NEGATIVE displacement before any stwu is the caller's
        # frame, i.e. the middle of a function. A negative one is the red
        # zone a leaf function may scribble in without a frame of its own -
        # 0x8300DF78 does exactly that (VMX128 code, `stw r10, -0x10(r1)`)
        # and is a real function pointed to from a table.
        if 1 in reads and op not in (31,) and ((w >> 16) & 0x1F) == 1:
            if not (w & 0x8000):
                return True
        written |= writes
        if is_terminator(w) or (op == 18 and not (w & 1)):
            return False
    return False


def site_feeds_bctr(words, i, limit=10):
    """True if a `bctr` follows the materialising instruction at index `i`
    before any `bl` or `blr`: the address is jumped to, not handed over."""
    for k in range(i + 1, min(i + 1 + limit, len(words))):
        w = words[k]
        if w == BCTR:
            return True
        if w == BLR or ((w >> 26) in (16, 18) and (w & 1)):   # blr, bl, bcl
            return False
    return False


def looks_like_inline_cases(img, target):
    """`li rD, 0; b L; li rD, 1; b L; li rD, 2; b L ...` - a switch whose
    cases the compiler laid out at a fixed stride and jumps into by
    arithmetic on the materialised base (0x83063ABC, 2026-09-12). Not a
    function; registering it cuts the switch off from its exit."""
    first_b = None
    for n in range(3):
        li = img.word(target + n * 8)
        b = img.word(target + n * 8 + 4)
        if (li >> 26) != 14 or ((li >> 16) & 0x1F) != 0:   # li rD, imm
            return False
        if (b >> 26) != 18 or (b & 3):                      # b (relative, no link)
            return False
        dest = target + n * 8 + 4 + ((b & 0x03FFFFFC) - (0x04000000 if b & 0x02000000 else 0))
        if first_b is None:
            first_b = dest
        elif dest != first_b:
            return False
    return True


def materialised(img, starts, tlo, thi, found):
    """Channel 2: function pointers BUILT IN CODE, never stored as data.

    A callback handed to another subsystem does not have to live in a table.
    MSVC materialises its address inline:

        lis  r11, 0x8221
        addi r10, r11, 0x42D0        -> 0x822142D0

    and that address may appear nowhere else in the image - not as a pointer,
    not at any alignment. `0x822142D0` was exactly this: a three-instruction
    getter that the vtable channel could not see, that reached the runtime as
    `[FATAL] Call to invalid or unregistered function` only once the player got
    past character select and the world started loading.

    Two details matter. The `addi` destination is usually a DIFFERENT register
    from the `lis` destination, so matching `addi rD, rD, lo` alone misses most
    of them. And `lis` results are short-lived, so a register is only trusted
    until something else writes to it.

    A third one cost a crash in play (2026-09-12, 0x82DE2BA8): the site and
    the target may sit in the SAME analyzer function. A builder at
    0x82DE2D48 fills a table with ten thunks at 0x82DE2B38..0x82DE2CF8, and
    the analyzer had absorbed builder and thunks alike into 0x82DE2A70 - so
    the old same-owner rule, meant to keep out labels, threw the whole family
    away. A label is now recognised by what it is: the destination of a
    direct branch somewhere in .text. A materialised address that no branch
    ever jumps to is a function pointer whoever owns it.
    """
    va, size = next((v, s) for n, v, s in img.sections if n == ".text")
    n = size & ~3
    words = struct.unpack_from(f">{n // 4}I", img.data, img.offset(va))
    sorted_starts = sorted(starts)
    labels = branch_targets(words, va)

    hi_of = {}                      # register -> (immediate, index it was set)
    for i, w in enumerate(words):
        op = w >> 26
        rD = (w >> 21) & 0x1F

        if op == 15 and ((w >> 16) & 0x1F) == 0:        # lis rD, imm
            hi_of[rD] = ((w & 0xFFFF) << 16, i)
            continue

        rA = (w >> 16) & 0x1F
        base = hi_of.get(rA)
        if base is not None and i - base[1] <= MATERIALISE_WINDOW:
            lo = w & 0xFFFF
            target = None
            if op == 14:                                 # addi rD, rA, lo
                target = base[0] + (lo - 0x10000 if lo & 0x8000 else lo)
            elif op == 24:                               # ori rD, rA, lo
                target = base[0] | lo
            site = va + i * 4
            if (target is not None and tlo <= target < thi and not target & 3
                    and not site_feeds_bctr(words, i)
                    and target not in starts
                    and is_terminator(img.word(target - 4))
                    and not looks_like_jump_table(img, target, tlo, thi)
                    and (owner(sorted_starts, site) != owner(sorted_starts, target)
                         or target not in labels)
                    and not looks_like_inline_cases(img, target)
                    and not looks_like_continuation(img, target)):
                found.setdefault(target, site)

        # Anything that writes a register invalidates the lis we recorded for
        # it. Being conservative here costs a few candidates; being sloppy
        # invents addresses out of unrelated instruction pairs.
        if op in (14, 15, 24, 25, 26, 27, 28, 29, 32, 33, 34, 35, 40, 41,
                  46, 56, 58) or op == 31:
            hi_of.pop(rD, None)
    return found


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

    materialised(img, starts, tlo, thi, found)
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
        "# Regenerated by tools/scan_fnptrs.py - do not hand-edit; put any",
        "# address that must NOT be registered in config/fnptr_exclude.txt",
        "# instead, then re-run with --write.",
        "#",
        "# Each of these is pointed to from a vtable or dispatch table in",
        "# .rdata/.data, is not a function start the analyzer already knows,",
        "# and is preceded by an instruction control cannot fall through.",
    ]
    for target, size, owner, slot in rows:
        lines.append(f'0x{target:08X} = {{ name = "sub_{target:08X}_fnptr", '
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
    ap.add_argument("--sample", type=int, metavar="N",
                    help="disassemble N random candidates for eyeballing")
    ap.add_argument("--seed", type=int, default=1)
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

    if args.sample:
        # The guard that actually works. Every automated check passed while
        # channel 2 was returning nine switch jump tables for every real
        # function; ten disassembled candidates made it obvious in seconds.
        # Look for repeated `lwz rN, off(rN)` runs - that is a table being
        # read as code, not a function.
        import random
        import struct as _struct
        from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN
        md = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
        starts = known_starts(img) | add_function.existing_addresses(strip_block_file())
        excluded = read_exclusions()
        found = {a: s for a, s in candidates(img, starts).items()
                 if a not in excluded}
        random.seed(args.seed)
        picks = random.sample(sorted(found), min(args.sample, len(found)))
        print(f"{len(found)} candidates; showing {len(picks)}")
        for target in picks:
            try:
                size = add_function.function_extent(img, target)
            except SystemExit:
                size = None
            site = found[target]
            where = ".text (built by lis/addi)" if img.section_of(site) == ".text" \
                else f"{img.section_of(site)} (pointer table)"
            print(f"\n0x{target:08X}  size {size and hex(size)}  "
                  f"site 0x{site:08X} in {where}")
            for k in range(0, min(size or 24, 24), 4):
                w = img.word(target + k)
                d = list(md.disasm(_struct.pack(">I", w), target + k))
                print("     " + (f"{d[0].mnemonic} {d[0].op_str}".strip()
                                 if d else f"<{w:08X}>"))
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
