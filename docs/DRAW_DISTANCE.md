# Draw distance and field of view - how they work, 2026-09-13

Both sliders exist now (0.1.9 field of view, 0.1.10 draw distance). This file
records what was found, what was tried and failed, and how each is done, so
the next person does not repeat the dead ends.

## Field of view (live)

The gameplay camera's projection is built EVERY FRAME by one function,
`sub_821B4B48`, with the camera object in r3:

    lfs f0,648(r31) / lfs f13,520(r31)   previous and target horizontal angle
    lfs f11,652(r31) / lfs f10,524(r31)  previous and target vertical angle
    fmadds f8,f12,f1,f0                  f8  = lerp(prev, target, f1)  horizontal
    fmadds f30,f9,f1,f11                 f30 = lerp(prev, target, f1)  vertical
    fmul f7,f8,f31  (x 0.5)              <- hook fable2PatchFieldOfView HERE
    bl 0x82294118                        tan(half)
    ...
    fdivs f1,f31,f9 / fdivs f6,f31,f6    m00 = 1/tan, m11 = 1/tan
    bl 0x8219d690                        4x4 constructor -> camera + 272

The angles are radians: 1.0444 vertical (59.84 degrees, not 60) and 1.593
horizontal (91.3 degrees), i.e. the 16:9 aspect is baked into the pair, no
division by an aspect anywhere. The hook scales the vertical angle by the
setting over 60 and re-derives the horizontal one from the same tangent
ratio, so the aspect is preserved exactly. It runs for every camera (the
zoomed dialogue shots included) and scales rather than replaces, so their
framing relative to gameplay is kept.

How it was found: `projlist.py` (session scratchpad) lists every perspective
matrix in guest memory; the heap copy with zn = 0.1 in m32 is the camera's.
A cdb hardware write-breakpoint on its m11 hit in `sub_8219D690`, the matrix
constructor; its call sites were scanned for the tan routine, and exactly one
uses it. The constant at 0x82101034 that 0.1.6 wrote is NOT on this path
(the A/B in 0.1.8 showed identical frames); it is some other 60.

Lesson, paid for with two crashed sessions: cdb's `ba` breakpoints stay armed
in the debug registers after `qd`, even after `bc *`; the next write raises
a single-step exception (0x80000004) inside the game with no debugger to
take it. Do not use data breakpoints on this game again unless the crash
filter learns to clear DR7 and continue.

## Draw distance (restart-bound)

### Where the values live

`game/data/globals/globals.gdb` (3,016,809 bytes, big-endian):

| offset | meaning |
|---|---|
| 0x00 | `GDB\0` |
| 0x04 | size of the record section (0x118a7 - not used by the port) |
| 0x08 | offset of the descriptor section, from 0x18 (0x154a7c) |
| 0x0C | size of the descriptor section (0x6b870) |
| 0x10 | count (9993), 0x14 zero |
| 0x18 | 16 bytes the game rewrites at load |
| 0x28 | records, up to 0x18 + word 0x08 |
| 0x154a94 | descriptors (7,388) |
| ~0x230000 | symbol table: `id, name\0` pairs |

Field ids are FNV-1 of the field name (basis 0x811c9dc5, prime 0x01000193,
multiply then xor). A descriptor is `[count << 8 | flag][count ids,
ascending, repeats allowed][count type words: type << 24 | C++ member
index]`; types seen: 0 bool, 1 ?, 3 float, 4 string id, 5 enum, 6 reference
(`parent`), 7 ?. A record is `[descriptor offset within the section][one
32-bit value per field IN THE DESCRIPTOR'S ID ORDER]` - not by the member
index, which is where the value goes in the game's C++ object. 71,845
records. The class defaults (record 0x42d68) say MaxDrawDistance 64,
BillboardDistance 70, LodFadeDistance 30; instance records override with
80, 150, 400, 1024 and so on. 150 draw-distance floats in all.

### What does and does not work

- The whole file is loaded into the guest heap (base varies between runs);
  the header and the descriptor references are relocated in place, the
  values are byte-identical to the file.
- Scaling all 150 values x0.1 in the resident copy during play: no change.
- Scaling them on the title screen, before the save loads: no change. The
  numbers are copied into the object definitions while the game starts.
- Scaling them in the FILE (backup, swap, sha256-verified restore): the
  buildings and the far wall behind the Crucible arch are gone at x0.1.
  That is the lever.

### How the port does it

`src/fable2_gdb.cpp`. `WriteScaledGlobals` walks the file with the layout
above, refuses to write if either walk does not reach the end it expects,
and scales MaxDrawDistance, MaxDrawDistanceOverride, BillboardDistance and
LodFadeDistance (floats in 0.5..20000; sentinels and references are left
alone). `InstallDrawDistance`, from the app's OnPostSetup (the runtime has
mounted the game, the guest has not opened a file yet), mirrors
`data\globals` into `<exe>/shadow/globals/`: the scaled globals.gdb plus
hard links to the other six files (copies when the game folder is on another
volume), mounts the mirror as a read-only HostPathDevice at
`\Device\Fable2Shadow`, and registers a symbolic link from
`\Device\Harddisk0\Partition1\data\globals` to it. The FOLDER, not the
file: the runtime's OpenFile resolves a path's directory (where symbolic
links apply) and then takes the child by name, so a link on the one file is
never consulted - the first version did that, logged "served", and the game
read its own copy. The runtime's resolver follows links until none matches,
so `game:\...` and `d:\...` both arrive at the mirror; the device is
mounted outside `\Device\Harddisk0` because the runtime's null device
claims everything under it that the partition does not. At 100% nothing is
mounted and the mirror is removed (removing hard links leaves the originals
alone). The game folder is never written.

### Tools (session scratchpad)

`gdb_records.py` / `gdb_dd.py` decode the file; `gdb_poke.py` and
`gdb_patch.py` scale the resident copy (the negative results above);
`gdb_file_patch.py` + `gdb_file_test.sh` are the file experiment with the
verified restore; `memscan.py`, `projlist.py`, `hostmap.py` (host RIP ->
guest function through the exe's PPCFuncMappings table) served the FOV work.
