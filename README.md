# fable2recomp

Static recompilation of **Fable II (Game of the Year Edition)** from Xbox 360
to native PC, on the **ReXGlue SDK 0.10.0** — the same toolchain used by
re:Blue (Blue Dragon) and by this machine's `ng2recomp` (Ninja Gaiden II).

The user owns the disc. Nothing here is redistributable: the build embeds the
game's own translated code, and `assets/`, `game/` and every disc file are
gitignored.

---

## Status

| | |
|---|---|
| Disc parsed | ✅ XGD2, 451 files, 6.5 GB |
| XEX decrypted and analysed | ✅ |
| `rexglue codegen` | ✅ **zero analysis errors** |
| `setjmp` / `longjmp` located | ✅ `0x83000200` / `0x82CA9260` |
| Native build | ✅ `fable2.exe`, 78 MB, ~3 min |
| Runs | ✅ **title screen → main menu → New Game character select** |
| Intro videos | ✅ Bink decodes correctly, no artifacts |
| Input | ✅ keyboard-to-controller (`--mnk_mode=true`) drives the menus |

Everything above was reached on 2026-09-03, the first day of the project.
The game boots through the Microsoft and Lionhead logo videos, arrives at the
**Fable II title screen**, accepts a button press, opens the **main menu**
(New Game / Downloadable Content / Language / Subtitles) and goes on to the
**New Game character-select screen with the 3D boy and girl models rendering on
their cards** — so this is not just 2D front-end art, it is loading and drawing
game assets. Screenshots in `out/shots/`.

For scale: `ng2recomp` took two days to render its start menu, and its intro
video is still wrong.

Not yet tried: actually entering the world.

## The game

Measured from the disc, not looked up:

| | |
|---|---|
| Title ID | `4D5307F1` |
| Media ID | `716F0A0D` |
| Version | `0.0.0.26` (base build — **not** TU1) |
| Build date | 2009-06-09 18:43:08 UTC |
| Image base | `0x82000000` |
| Entry point | `0x82CBB970` |
| `.text` | 18,101,444 B = **4,525,361 instructions** |
| `.pdata` functions | **46,072** |
| Imports | `xam.xex` (94) + `xboxkrnl.exe` (173) — **no guest DLLs** |
| Instruction mix | 4.8 % float, 1.5 % VMX128 |
| Disc | 1 of 1 |

Sections worth knowing about:

```
.rdata   0x82000600    1,090,376
.pdata   0x8210AA00      368,576
BINKBSS  0x82164A00       10,504
.text    0x82170000   18,101,444
BINK     0x832B3600       62,524
.data    0x832D0000    2,274,796
.lhmem   0x834FB600           12      <- Lionhead
.XBMOVIE 0x834FB800           12
.tls     0x834FBA00           25
.lhtrc   0x834FBC00            8      <- Lionhead trace
BINKDATA 0x834FBE00       15,688
.edata   0x83500000        7,453      <- NOT exports; an XDBF/SPA achievement blob
.idata   0x83510000        1,186
.XBLD    0x83520000          192
.reloc   0x83520200    1,030,968
```

**The `BINK*` sections are the good news.** Fable II plays its 20 videos
through RAD's Bink, statically linked and decoded on the CPU. Ninja Gaiden II
instead linked Microsoft's XMEDIA WMV decoder, whose *GPU* DXVA path lives
inside the GPU plugin and is still producing colour artifacts there. A CPU
decoder is just more PowerPC to translate, so Fable II's video path should come
out of the recompiler working. This has not been tested yet.

`.edata` is not a PE export directory. It starts `XDBF`, then `XACH`
(achievements) and `XCXT` — it is the SPA resource. `rexglue init achievements`
can extract it.

## Relationship to the upstream Fable2Recomp project

`../reference-Fable2Recomp` is a clone of
[github.com/Fable2Recomp/Fable2Recomp](https://github.com/Fable2Recomp/Fable2Recomp),
kept for reference. It is the same idea on the same SDK, and its
`Fable2_config.toml` carries **923 hand-found function overrides**.

**None of those addresses apply here, and this was checked rather than
assumed.** That repo targets *GOTY TU1*; this disc is the base GOTY build
`0.0.0.26`. Against our image:

- their `setjmp_address = 0x83006C90` lands mid-instruction inside a VMX128
  block, and `longjmp_address = 0x82CAFA30` decodes as `beq +0x14`;
- only 1 of their 8 `parent =` addresses is a `.pdata` function start here;
- **164 of their 923 overrides land in our `BINK` section**, i.e. past the end
  of our `.text` entirely — their build's code segment is simply longer;
- of 56 sampled 8-byte overrides (which should all be `addi rX,rX,N; b target`
  adjustor thunks), exactly 1 has that shape here.

So the two projects share an approach, not a coordinate system. Adopting their
config would have silently mis-registered hundreds of function boundaries.

It also turns out not to matter much: under SDK 0.10.0 this XEX needed **12**
function overrides to make codegen validate, and 118 in total once the runtime
had its say - not 923 (see below).

## Prerequisites

- The ReXGlue SDK. `tools/build.cmd` looks for `%REXSDK%`, falling back to
  `<project>/../RexBlue/win-amd64` (where this project keeps its own copy).
- LLVM/clang 20+, Ninja, CMake 3.25+, MSVC Build Tools (for the Windows SDK
  headers and libs that `clang-cl`-targeted clang++ needs — `vcvars64.bat`).
- Python 3.12 with `capstone` for the analysis tools.
- No vcpkg. SDL3, fmt, spdlog, utf8cpp and DXC all ship inside the SDK.

## Layout

```
fable2recomp/
  assets/default.xex          the XEX alone, what codegen reads
  game/                       the whole extracted disc, what the build runs against
  config/functions.toml       function-boundary overrides (hand-found, then a generated block)
  config/vtable_exclude.txt   addresses scan_vtables.py must never register
  generated/default/          codegen output (500+ files, ~290 MB)
  src/                        the host application
  tools/                      analysis and build scripts
  fable2_manifest.toml        the codegen manifest
  out/build/win-amd64-*/      build output
```

## Building and running

```
tools\build.cmd Release            # or RelWithDebInfo when chasing a crash
tools\run.cmd
```

`build.cmd` runs `rexglue codegen` as its own step **before** CMake. This is
not tidiness: codegen rewrites `generated/default/fable2_pch.h`, and inside a
single ninja run the precompiled header can be built from the old copy before
codegen replaces it, after which every translation unit fails with *"file has
been modified since the precompiled header was built"*.

`Release` strips debug info from the ~500 recompiled TUs. Any other
configuration keeps line tables, and codegen emits **one source line per guest
instruction**, so a fault resolves to the exact PowerPC instruction that caused
it. That is the entire triage method — build `RelWithDebInfo` before debugging.

A bare `--flag` does **not** set a boolean cvar in this runtime; it is accepted
and silently ignored. Write `--fullscreen=true`.

## Tools

| tool | what it does |
|---|---|
| `extract_disc.py` | GDF (XGD1/2/3) extractor: `--list`, `--only`, or everything |
| `xex_image.py` | retail XEX2 decrypt + decompress to a flat guest image; caches `out/image.bin` |
| `xex_imports.py` | which kernel/xam ordinals the game imports, and who calls them |
| `resolve_calls.py` | **run codegen, register every unresolved call, repeat to a fixpoint** |
| `add_function.py` | register one missed function, sizing it by walking to its terminator |
| `find_setjmp.py` | locate `_setjmp` / `longjmp` by shape, since the XEX is stripped |
| `scan_vtables.py` | **find missed functions in bulk by walking vtables**, instead of one crash per rebuild; `--check`, `--write`, `--prune` |
| `boot.py` | run the build for N seconds, stop it *by PID*, summarise the log |
| `boot_loop.py` | run → crash → register → rebuild, automated |
| `whereis.py` | what is at a guest address |
| `xrefs.py` | who reads/writes a guest range |
| `gstrings.py` | strings with their guest addresses |
| `scan_missed.py` | diagnostic only — see the warning below |
| `build.cmd`, `run.cmd` | build and launch |

`scan_missed.py` is kept only for diagnosis. **Scanning `.text` for things that
look like function prologues does not work**: `.text` contains pointer tables,
and a pointer beginning `0x82…` decodes as a plausible `lwz`. On Ninja Gaiden
II it produced ~1,340 candidates that were mostly false.

`scan_vtables.py` inverts that and is the tool to use — see below.

## Findings

### Codegen needed 12 function overrides to validate at all, found by iteration

The analyzer takes function bounds from `.pdata`, then fills the gaps. MSVC
emits no `.pdata` for small helpers with no prologue — `this`-adjusting virtual
thunks, one-line getters, forwarders — so a helper sitting in the padding after
a real function is absorbed into that function's body, and a `b` into it fails
validation with *"target not in any function"*.

The first codegen reported 8. Registering those exposed 3 more, and those
exposed 2 more: a registered forwarder's own `b` target becomes a call *from* a
real function, so this has to be iterated to a fixpoint rather than done once.
`tools/resolve_calls.py` does that automatically and stopped at **12**, with
codegen reporting zero errors. Three more came from actually running the game,
and 103 more from walking vtables (next section), for **118 in total**.

All 12 are the expected shape, e.g.

```
0x82C000F8  lwz r3, 0xc(r3)      # a forwarder to a member function
0x82C000FC  b   0x82c106a8
```

### Finding missed functions in bulk, safely

The functions the analyzer misses are exactly the ones nothing calls directly:
they are reached through **vtables and dispatch tables**, which is why static
analysis never sees a call to them and why they only surface at runtime, one
`[FATAL] Call to invalid or unregistered function` at a time. At roughly three
minutes per rebuild, finding a hundred of them that way costs hours.

But vtables are *data*. `tools/scan_vtables.py` reads every 4-byte-aligned word
in `.rdata` and `.data`, keeps the ones pointing into `.text`, drops the ones
already registered, and requires two things of what is left:

- the pointer must sit in a **run of at least two** consecutive code pointers,
  so a lone integer that happens to look like an address is ignored;
- the instruction **before** the target must be one control cannot fall through
  — `blr`, `bctr`, an unconditional `b`, or padding.

That second rule is what makes it safe. A false positive here is not harmless:
registering an address that is really the middle of a straight-line function
would cut that function short. If the preceding instruction cannot fall
through, splitting there costs nothing even when the guess is wrong.

**Splitting is not free, though, and the first attempt broke things in four
distinct ways.** Each one produced a plausible-looking result, which is why the
tool now has to prove itself before it is believed.

1. **Sizes must be clamped to the next function start.** `function_extent`
   walks forward to a terminator and will run straight through a later entry
   point. Codegen then refuses the whole manifest —
   *"Overlapping boundaries: 0x830FD7B8+0x7C overlaps 0x830FD7F4+0x40"* — and
   because that failure happens in codegen, the *previous* executable is still
   sitting in the build directory, so the next run looks like it succeeded.
   Always check that the build actually rebuilt.
2. **A split can break a branch.** A `b` that used to be an internal jump — a
   loop back-edge, or a jump to a shared epilogue — can end up crossing the new
   boundary, and the recompiler cannot emit a jump across a C++ function
   boundary. Codegen reports *"Unresolved b target 0xT from 0xS"*. A control
   run with only the 15 hand-found overrides produced **zero** of these, so
   every one was ours. `--prune` is the fixpoint loop that finds them: run
   codegen, add whatever it complained about to `config/vtable_exclude.txt`,
   **regenerate the whole block**, repeat. Regeneration rather than deleting
   lines matters — remove one entry and its predecessor is still sized to a
   boundary that no longer exists, leaving a hole that surfaces as a *new*
   unresolved call.
3. **Some targets are import thunks, and codegen keeps the import.** The last
   few KB of `.text` is the import thunk table. Registering an entry there
   makes codegen log `[functions] 0x832B2A0C outranked by import`, register a
   name, and then never emit a body for it — so the failure lands at *link*
   time, after a full compile, as `undefined symbol: sub_832B2A0C_vtable`.
   Codegen said so several minutes earlier; `--prune` now reads that line.
4. **The tool must not read its own output as evidence.** Candidates are
   compared against the analyzer's function list, which lives in
   `generated/default/codegen.partition.json` — but that file is written by the
   last codegen run, which included whatever this tool wrote last time. Reading
   it raw made the tool treat its own previous output as the analyzer's
   opinion, and the candidate list silently collapsed from 121 to 13 on the
   second run. It now subtracts its own generated block, which is sound because
   registering a function only ever *splits* an analyzer function, never merges
   two.

And the analyzer's list is the right comparison in the first place: its
Discover and GapFill phases find thousands of functions beyond the unwind table
— 60,219 registered against 46,072 `.pdata` entries — so comparing against
`.pdata` reported 4,323 "missing" functions where there are 121.

**`--check` is the guard.** NG2's `xrefs.py` shipped three bugs that each gave
confident wrong answers, so this scan is required to say how it does against
the functions already registered by hand. It finds 4 of 15 — and that number is
honest rather than disappointing: the other 11 are reached by a direct `b` from
code, which is `resolve_calls.py`'s job at codegen time. Of the functions found
the expensive way, **by crashing at runtime, it finds all of them.**

### `setjmp` / `longjmp` — found before running, on purpose

On Ninja Gaiden II this pair was the single biggest fix, and it was found the
hard way: unset, the resource parsers' error path *returned* instead of
unwinding, fell into a copy loop whose bounds check was meant to be fatal, and
overran a stack buffer — crashing minutes later on a garbage pointer nowhere
near the cause. **A crash on a garbage pointer far from any obvious cause is a
plausible signature of missing `setjmp`/`longjmp`**, because an un-modelled
`longjmp` turns "abort this operation" into "carry on with invalid state".

So `tools/find_setjmp.py` finds them by shape rather than waiting for the
symptom. Both are confirmed exact mirrors of each other:

```
0x83000200  _setjmp(buf)
    lis r4, 0x8332 / lwz r0, 0x1a8c(r4) / mtctr / bnectr   <- the setjmp hook
    mflr r0 / mfcr r4
    stfd f14..f31 -> buf+0x00 ; std r13..r31 -> buf+0x98
    stvx128 v20.. -> buf+0x140
    stw LR -> +0x134 ; stw CR -> +0x130 ; std r1 -> +0x90 ; stw 0 -> +0x138
    li r3, 0 ; blr                                          (ends 0x830004C8)

0x82CA9260  longjmp(buf, value)
    lwz r0, 0x138(r3)         <- the same flag setjmp cleared
    mr r7, r3                 <- buf moves to r7
    lwz r3, 0x134(r7)         <- saved LR
    lwz r4, 0x90(r7)          <- saved SP
    lfd f14..f31 / ld r13..r31 / lvx128 back out of r7
```

`_setjmp` has **no `.pdata` entry of its own** — it sits inside the range
starting `0x82FFFF68`, alongside the `__savevmx`/`__restvmx` helpers — which is
exactly why it has to be named in the manifest by hand. Same as on NG2.

### Codegen flags

The manifest sets `non_volatile_as_local`, `skip_lr`, `ctr/xer/cr/reserved_as_local`
— what re:Blue ships, and what NG2 needed. The defaults keep every guest
register in one shared `PPCContext`, which makes correctness depend on each
callee's prologue and epilogue saving and restoring non-volatiles through guest
stack memory; on NG2 that was a real crash (`r31` went null across a call).
With the flag, r14–r31 become function locals and the `__savegprlr`/`__restgprlr`
helper calls are elided.

`skip_lr` was verified safe here the same way: **zero `bl $+4` PC-capture
idioms** in 4.5 M instructions.

### The 2.4 MB "function" warning is not a bug

Codegen warns:

```
Function 0x82242ED0 is 2433590 bytes, exceeds max_file_size_bytes (1048576)
```

That number is the size of the *emitted C++*, not of the guest function.
`0x82242ED0` is a genuine `.pdata` function start whose next entry is
`0x8225DC40` — 0x1AD70 = 109,424 guest bytes, about 27,000 instructions, which
at ~89 bytes of C++ each is exactly 2.4 MB. It compiles; it is just a big
translation unit.

## Known gaps

- Nothing has been run yet. Reaching a build is not reaching a frame.
- The 20 Bink videos are untested (see above for why they are expected to work).
- No DLC. The GOTY disc carries the two expansions on-disc, so unlike NG2 there
  is probably no STFS licence work to do — unverified.
- `$SystemUpdate/` on the disc is the *dashboard* update, not a game title
  update, and is not used.
- No settings UI. `ng2recomp` grew a pre-boot setup screen and an F10 overlay
  that are worth porting once the game runs; the SDK's own cvar browser is on
  F4 in the meantime.
