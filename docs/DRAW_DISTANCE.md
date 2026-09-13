# Draw distance (level-of-detail pop-in) - investigation, 2026-09-13

The user asked to increase draw distance with a slider, alongside the
field-of-view slider that shipped in 0.1.6. FOV was a single global constant
and was straightforward; draw distance is not, and this records why and what
the safe path is, so the next session can decide with the user.

## What the pop-in is

Objects and their sharp textures appear close to the player because of the
game's own level-of-detail and texture streaming, tuned for a 512 MB console.
It is not the far clip plane: the camera's projection far plane is ~4993 units
(read from the live projection matrices), far beyond where things pop in. So a
far-plane change would not help.

## Where the values live

The per-object draw distances are fields in `game/data/globals/globals.gdb`,
a binary database. Its field names are hashed to 32-bit ids with **FNV-1**
(offset basis 0x811c9dc5, prime 0x01000193, multiply-then-xor), confirmed
against the id bytes stored after each name in the symbol table:

| field | FNV-1 id | where |
|---|---|---|
| MaxDrawDistance | 0x171e1806 | id-sorted class tables, 12 occurrences |
| MaxDrawDistanceOverride | 0xa9689dee | 6 occurrences |
| LodFadeDistance | 0xe2687215 | 4 occurrences |
| FadeInDistance | 0x74d853d3 | 2 occurrences |
| RadiosityFadeDistance | 0x197e929c | 2 occurrences |

Each class block holds an ascending (binary-searchable) array of these field
ids; the per-object *values* sit in a parallel array indexed by the id's rank.
The header at 0x00 has section offsets (0x118a7, 0x154a7c, 0x6b870, ...).

## Why it is not done yet

Rewriting `globals.gdb` in place to multiply every draw-distance value means
decoding the value-array layout exactly. A wrong offset corrupts the database
and every object misbehaves, and this file is shared with the game the user is
actively playing. That is not a change to make blind and unattended overnight.

## The safe path (for the user's decision)

1. Decode the value array fully against a couple of known objects, verified by
   reading a value the game clearly uses (e.g. a 206.7 near MaxDrawDistance).
2. A tool that writes a PATCHED COPY of globals.gdb (never the original),
   multiplying MaxDrawDistance / LodFadeDistance / FadeInDistance by a factor,
   with the original kept and a one-line revert.
3. A "Draw distance" slider that selects the factor and points the game at the
   patched copy (a game-folder override), rebuilt when the slider changes.

Alternatively, a runtime hook on the LOD-distance comparison (a global bias)
would avoid touching the file, but needs the comparison site found first - the
same live-value method that found FOV, once a LOD value is known to hunt for.

## Tools from this session (in the session scratchpad)

- `memscan.py` - read-only scan of the running game's guest memory (big-endian).
- `projscan.py` - finds projection matrices, reads the FOV and near/far planes.
- `pokeone.py` / `fovhunt.py` - write a value and screenshot-diff to confirm a
  constant controls the picture (how the FOV constant was found).
