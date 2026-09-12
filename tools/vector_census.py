"""Which non-volatile vector registers carry values INTO functions.

With `non_volatile_as_local`, the recompiler makes v64..v127 zero-initialised
locals (`PPCVRegister v92{};`) and emits nothing for the __savevmx/__restvmx
helpers. A function that RECEIVES a value in one of those registers then
reads zero - on this port that was the flat-blue scene (a dot product on v92
came out 0, 1/sqrt(0) = Inf, 0 x Inf = NaN). Such registers must be listed in
the manifest's `shared_vector_registers`, and ONLY those: a shared register
that a function uses as scratch is never restored, and clobbers its caller.

The generated code carries every PowerPC instruction as a comment
(`// vor128 v126,v1,v1`), which is what this reads. Per function, per local
vector register, the first instruction that names it decides:

  stvx / stvlx / stvrx / stvewx / stvx128 ...   a spill to memory: skip it
                                                (that is the prologue save,
                                                and the value saved is the
                                                CALLER's - not a use)
  the register is the first operand              a write: the function does
                                                not receive it
  anywhere else                                  a read before any write:
                                                the value ARRIVES - share it

    python tools/vector_census.py                 # report on generated/default
    python tools/vector_census.py --dir generated/default_base

Prints the registers, a few functions that receive each, and the manifest
line. Registers already shared (`ctx.vNN`) are listed too so a register that
nothing receives any more can be localised again.
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]{8})\)", re.M)
DECL_RE = re.compile(r"^\s*PPCVRegister v(\d+)\{\};", re.M)
ASM_RE = re.compile(r"^\s*//\s*([a-z][a-z0-9_.]*)\s+(.*)$", re.M)
STORE_MNEMONICS = ("stv",)  # stvx, stvx128, stvlx, stvrx, stvewx, stvlx128 ...


VREG_RE = re.compile(r"(?<![\w])v(\d+)(?![\d])")


def received_registers(body, lo, hi):
    """Registers in lo..hi whose first non-spill instruction in `body` reads
    them: the value arrives from the caller."""
    decided = {}
    for m in ASM_RE.finditer(body):
        mnemonic, operands = m.group(1), m.group(2)
        if mnemonic.startswith(STORE_MNEMONICS):
            continue  # a spill; the value belongs to the caller
        parts = [x.strip() for x in operands.split(",")]
        for k, part in enumerate(parts):
            for vm in VREG_RE.finditer(part):
                reg = int(vm.group(1))
                if reg < lo or reg > hi or reg in decided:
                    continue
                decided[reg] = "write" if k == 0 else "read"
        if len(decided) == hi - lo + 1:
            break
    return [r for r, v in decided.items() if v == "read"]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=os.path.join(ROOT, "generated", "default"))
    ap.add_argument("--min", type=int, default=64)
    ap.add_argument("--max", type=int, default=127)
    args = ap.parse_args()

    received = {}
    shared = {}
    functions = 0
    for path in sorted(glob.glob(os.path.join(args.dir, "fable2_recomp.*.cpp"))):
        text = open(path, encoding="utf-8", errors="replace").read()
        funcs = list(FUNC_RE.finditer(text))
        for i, fm in enumerate(funcs):
            functions += 1
            body = text[fm.end(): funcs[i + 1].start() if i + 1 < len(funcs) else len(text)]
            name = fm.group(1)
            for reg in received_registers(body, args.min, args.max):
                received.setdefault(reg, []).append(name)
            for sm in re.finditer(r"ctx\.v(\d+)\b", body):
                reg = int(sm.group(1))
                if args.min <= reg <= args.max:
                    shared.setdefault(reg, set()).add(name)

    print("%d functions scanned in %s" % (functions, args.dir))
    print("\nregisters received with a value (read before any write, local or shared):")
    for reg in sorted(received):
        fns = received[reg]
        print("  v%-3d %4d function(s): %s%s" % (reg, len(fns), ", ".join(fns[:5]),
                                                  " ..." if len(fns) > 5 else ""))
    print("\nregisters currently shared (ctx.vNN), functions touching them:")
    for reg in sorted(shared):
        print("  v%-3d %d" % (reg, len(shared[reg])))
    print("\nmanifest line from the census (what THIS code needs):")
    print("shared_vector_registers = [%s]" % ", ".join(str(r) for r in sorted(received)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
