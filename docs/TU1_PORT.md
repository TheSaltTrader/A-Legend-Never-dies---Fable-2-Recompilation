# Porting to Title Update 1 (branch `tu1`)

The record of moving this port from the disc's executable (version 0.0.0.26)
to the disc's title update (0.0.1.26). Kept as it happens, so the next
person can see what was derived from what.

## Why

Every save made on a console carries the title update's save version and
this build refuses it ("created with a more up-to-date version"). With the
number rewritten the save loads as far as Bowerstone Market and dies in a
recursive object walk on a serialised index this build has no entry for
(CHANGELOG 0.0.16). The update is the version every console ran.

## Facts established before starting (2026-09-12)

| item | value |
|---|---|
| disc executable | `default.xex`, title 4D5307F1, media ID 716F0A0D, version 0x1A (0.0.0.26), GOTY (`data/gold_version.txt`) |
| the update | xboxunity "Fable II v1", tuid 21399, base 0x1A -> 0x11A (0.0.1.26); package 3,166,208 B = `default.xexp` (2,992,128) + `data/tu1_data.bnk` (94,535); kept under `assets/tu1/` (not in git) |
| applies? | yes: the patch's delta descriptor `digest_source` == SHA-1 of the disc executable's RSA signature; the runtime logs `XEX patch applied successfully: base version: 0.0.0.26, new version: 0.0.1.26` |
| how it applies | the runtime's `UserModule` loads a sibling `default.xexp` next to the executable it loads (`game:\default.xexp` at play time; `assets/default.xexp` for the codegen, which loads the executable through the same runtime) |
| how much changes | 5166 of 5656 4 KB pages differ between the patched image and the disc's: the executable is re-linked, every address moves |
| the patched image for tools | `FABLE2_DUMP_IMAGE=<file>` (app seam) writes the loaded image from 0x82000000; `FABLE2_IMAGE=<file>` makes `tools/xex_image.py` (and every tool on it) read that instead of the .xex |
| a wrong update | media ID 04BF96A1's v4 (source version 5) does not apply: different signature |
| Xenia Canary | has a patch file for exactly this: `4D5307F1 - Fable II (GOTY_Platinum Edition, TU1).patch.toml`, hash EE56F849188A6A20, with 60 FPS, 1280x720 and MSAA off; without tick rate, texture morphing, website items |

## What has to be re-derived, and how

| what | disc value | tool | TU1 value |
|---|---|---|---|
| registered helper functions (`config/functions.toml`) | 167 entries (kept as `config/functions_disc.toml`) | `tools/resolve_calls.py` to a fixpoint, then `tools/scan_fnptrs.py` | 171 entries: 7 forwarders at 0x82C061F0 + the fixpoint's helpers + the pointer-table scan, MINUS 15 import thunks under 0x832B97A8 (`config/fnptr_exclude.txt`): the codegen drops those and the link wanted `sub_832B...._fnptr` |
| setjmp / longjmp (`fable2_manifest.toml`) | 0x83000200 / 0x82CA9260 | `tools/relocate.py` (find_setjmp.py's density heuristic picks zero-fill loops on this image) | 0x83006C90 (47/48 words; opens `lis r4,0x8332; lwz r0,0x1C4C(r4)` = the setjmp hook global, moved from 0x83321A8C) / 0x82CAFA30 (45/45; `mflr r0; stwu r1,-0x50(r1); mr r6,r4; cmpwi r4,0`) |
| shared vector registers (manifest) | v64,65,77-84,86,87,90-94 | census of the generated source: a register READ before it is written | after the first codegen |
| 60 fps hook | 0x82B9C8E8 `li r11,2` | Canary TU1 + relocate.py + disassembly | TWO sites: 0x82BA3018 `lwz r11, 0x351C(r31)` (Canary's; the divider read from a field) and 0x82BA3058 `li r11, 2` (the disc's 3/2/1 selector, moved +0x6770, 36/36); both hooked, r11 forced to 1 |
| 1280 wide hook | 0x8238DF58 `li r11,0x460` | Canary TU1 + disassembly | 0x823894C0 `li r11, 0x460`, same shape (cmpwi; stw; li 0x460; bne +8; li 0x3C0); relocate.py agrees (36/39) |
| MSAA hook | 0x8238DF3C `li r9,2` | Canary TU1 + disassembly | 0x823894A4 `li r9, 2`; relocate.py agrees (37/39) |
| tick rate hook | 0x8233AEB4 `stfd f0,-0x6af0(r8)` -> 15.0 at 0x83319510 | the 15.0 double in .data (three candidates: 0x83319628, 0x83319660, 0x83319668), then the one `stfd f0,disp(r8)` in .text that targets one of them | 0x8231091C `stfd f0, -0x69A0(r8)` after `lfd f0, 0x68(r1)`, double at 0x83319660; `patch_hooks.cpp` displacement changed to 0x69A0 |
| texture morph hook | 0x8220EF0C `cmplwi cr6,r18,0; beq cr6` | `tools/relocate.py` (36/36) | 0x8220EDD4 `cmplwi cr6, r18, 0; beq cr6, +0x17C` (same branch word) |
| update data | none | `PathConfig::update_data_root` | `game/update/` holding `data/tu1_data.bnk` (the game opens `update:\data\tu1_data.bnk` and `update:\build_version.txt`) |
| save version the game writes (`kGameSaveVersion`, `fable2_saveimport.cpp`) | 805699586 (0x30060002) | the number in a save this build made | 393219 (0x00060003) - the console's number: chosen by `FABLE2_COMPILED_WITH_PATCH` at compile time, so a console save now imports untouched |

## Rule for every tool on this branch

`export FABLE2_IMAGE=<absolute path>/out/guest_image_tu1.bin` before running
`resolve_calls.py`, `add_function.py`, `scan_fnptrs.py`, `xrefs.py` or any
other tool built on `xex_image.py`. Without it they decode `assets/default.xex`
- the disc - and compute sizes and bodies for TU1 addresses out of disc bytes.
That is how the first fixpoint rounds registered helpers 0x1EC and 0x23C long
that are 8-byte thunks: the numbers looked plausible and were nonsense. The
codegen itself is fine either way: it loads the executable through the
runtime, which applies `assets/default.xexp`.

## Shared vector registers: what the census says, and what was chosen

`tools/vector_census.py` reads the PowerPC instruction comments in the
generated code; per function, the first non-spill instruction naming a
v64..v127 register decides whether the function receives it (read first) or
not (written first). Run on BOTH trees it gives the same structure:

| register | disc (generated/default_base) | TU1 |
|---|---|---|
| v64, v77-v84, v86, v87, v90, v91, v93, v94 | one function, sub_82234108 | one function, sub_822340C8 |
| v92 | sub_82DED628 / 908 / D60 (the dot products) + sub_82234108 | sub_82DF4BB8 / 48D8 / 5010 + sub_822340C8 |
| v65 | not flagged | not flagged |
| v96 | 24 functions, sub_82CE6C20 .. | 24 functions, sub_82CEEAF8 .. |

The disc's manifest list is `[64, 65, 77-84, 86, 87, 90-94]` and that build
reaches gameplay. The census would drop 65 and add 96 on both images, i.e. the
difference is in the census, not between the images. Sharing a register that
is not received is what clobbers callers, so the list was left EXACTLY as the
disc's for this pass. **Open item:** if the update shows the flat-blue class of
bug, v96 in that cluster of 24 functions (a SIMD block around 0x82CEE000) is
the first suspect; if something misbehaves around register saves, v65.

## Log

- 2026-09-12 12:00 `kGameSaveVersion` follows `FABLE2_COMPILED_WITH_PATCH`
  (393219 on this build). `FABLE2_HUD=1` added: the on-screen readouts are
  forced on for a process without touching the settings file, and
  `tools/play_probe.py` sets it for every scripted run - the acceptance run
  below carried no numbers because the player's own session had F8 off.
  Version 0.1.0 for this line.
- 2026-09-12 11:45 ACCEPTANCE: the console save (Hero000, version 393219,
  the one the disc build refused and then crashed on when the number was
  rewritten) loads on the update build with no importer help. Scripted run
  (`FABLE2_PAD_SCRIPT` b/a/down/a/right/a every 22 s from 30 s, 150 s):
  frames at 60/90/120 s show the adult hero in Bowerstone Market with the
  quests "The Snowglobe" and "The Crucible Champion"; `[swap]` 51-55 guest
  fps there; no CRASH, ABORT or plugin warning in the log. Boot log:
  `XEX patch applied successfully: base version: 0.0.0.26, new version:
  0.0.1.26`; every hook in `config/hooks/patches.toml` fires; the menu runs
  at 60. Note for the next reader: the log file rotates, so a line anchor
  such as "Crash dumps:" is not always in it - grep the tail of the run.
- 2026-09-12 11:35 builds 2 and 3: the link wanted `sub_832BA0F4_fnptr`,
  ... `sub_832B9EF4_fnptr` (15 symbols, two rounds of 6+9): pointer-table
  entries the scan registered that the codegen drops because they are
  import thunks (all under the parent 0x832B97A8). Listed in
  `config/fnptr_exclude.txt`, rescanned, and the third build linked
  (`out/build_tu1_3.log`).
- 2026-09-12 11:25 `tools/scan_fnptrs.py --write` registered 173 pointer-table
  helpers (186 entries in all); `resolve_calls.py` round 1: codegen succeeded
  with nothing further to register. Census run (table above); manifest list
  kept. Full build started (`out/build_tu1_1.log`).
- 2026-09-12 11:05 rounds 1-2 of the fixpoint had been run without
  `FABLE2_IMAGE`: every registered size came from the disc image. Reset
  `functions.toml` to the seven forwarders and restarted with the variable
  set (the 8-byte thunks at 0x82C11BD8..0x82C11BEC now walk to their `b`).
- 2026-09-12 10:40 `tools/relocate.py` written (window match with branch and
  address fields masked); it reproduces Canary's two verified TU1 sites from
  the disc addresses, which is the check that it works. setjmp/longjmp, the
  tick-rate store (via the 15.0 double), texture morph and the 60 fps selector
  relocated and read back; manifest and `config/hooks/patches.toml` rewritten
  (disc file kept as `patches_disc.toml`); `patch_hooks.cpp` gained the second
  60 fps hook and the new tick-rate displacement. `config/fnptr_exclude.txt`
  emptied (disc list kept). `tools/vector_census.py` written for the
  shared-register list.
- 2026-09-12 10:35 `tools/resolve_calls.py` round 1: 8 unresolved calls, 7
  registered; round 2 refused: `add_function.py` walks to a terminator and
  registered three overlapping entries in a run of 8-byte forwarders
  (`lwz r3,12(r3); b target`) at 0x82C061F0. Registered the whole run as
  7 x 8-byte functions and restarted the fixpoint.
- 2026-09-12 10:15 branch `tu1` from v0.0.17 (d0e7099). `generated/default` of the
  disc build kept as `generated/default_base`; the disc executable as
  `out/fable2_base_0.0.15.exe`. `assets/default.xexp` placed for the codegen.
  `config/functions.toml` emptied (disc list kept). Three Canary TU1 hook
  addresses verified against the patched image (table above).
