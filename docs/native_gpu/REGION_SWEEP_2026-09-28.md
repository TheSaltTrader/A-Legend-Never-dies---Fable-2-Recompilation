# Region sweep and memory legs, 2026-09-27/28 (1.1.4 gate)

Route: D-pad up -> Quests/Maps -> Regions -> region -> destination (every destination the travel dialog offers:
13 regions, 50 destinations). Driver: job tmp `sweep.sh` / `area.sh` (travel detected by the loading-map camera,
arrival by `scene loading -> world`), pad file with `PAD_ROUTE=wait:1` (a leg's long `wait:` blocks `release`).

## Coverage (RGN3, exe 478796e, arm R113, 23:46 - 00:57)
- 50 / 50 destinations reached; 0 `[coverage] UNHANDLED`; 510 histogram lines; packet types decoded:
  21 22 27 2B 2D 2F 36 3B 3C 3D 45 46 48 54 58 5A 5B 60 61 62 63 64 7F (45, 48, 5A never seen at Fairfax).
- 345,462,935 draws, 0 failed; executor 0 stale read pointers, 0 wait timeouts; 0 registers changed outside the
  forwarded ranges. 27 present-submission waits timed out, all from 00:31 on (VRAM at the card's ceiling).
- Excluded: RGN0 (pre-histogram build, binary not recoverable), RGN1 (detector misread in-region travels), RGN2
  (pad queue blocked by the leg's wait: one destination).

## Defects found
1. In-region fast travel left the scene 'loading' (pillarbox, no ultrawide) - shipped in 1.1.3; a cross-region
   travel or restart clears it (V113X, 01:46/01:49). Fixed 478796e + bba682b (frame-rate independent).
2. Texture-pack memory never released: prebuilt upload buffers + resources parked on g_texpack_uploads, drained only
   on the in-frame path (prebuild/async default ON since 2026-09-27). RGN3: 31.6 GB VRAM, 62 GB private, fps median
   29 after midnight. Fixed 2f8aa68 (drain every submission); a97144e makes replacements count in the cache budget.

## Memory legs (22 destinations incl. a second lap over Market/Rookridge/Oakfield/Wraithmarsh)
| leg | exe | VRAM peak | private peak | fps median / p10 |
|---|---|---|---|---|
| MEM1 | bba682b (budget only) | 21.0 GB at 10 dest., climbing | 28.2 GB | - |
| MEM2 | 2f8aa68 | 8.1 GB, oscillating 5.2-8.1 | 9.8 GB | 60.0 / 51.8 |
| Z114A (release zip) | 1.1.4 | 5.7 GB | 7.0 GB | 60.0 / 58.0 |
Z114F (`FABLE2_NATIVE_GS=0`): plugin path, 89 M draws 0 failed, ~59 fps.

## Per-destination table (RGN3)
| Region | Destination | Stage line | Scene | Arrived | Coverage / failures | Note |
|---|---|---|---|---|---|---|
| Bloodstone | The Waterfront | 'bloodstone' | world-detected | 23:47:40 | unhandled +0, fail-or-stale +0 |  |
| Bloodstone | Wraithmarsh Road | same-region | world-detected | 23:49:51 | unhandled +0, fail-or-stale +0 |  |
| Bloodstone | Bloodstone Mansion | same-region | world-detected | 23:50:55 | unhandled +0, fail-or-stale +0 |  |
| Bloodstone | The Sinkhole | same-region | world-detected | 23:51:59 | unhandled +0, fail-or-stale +0 |  |
| Bloodstone | Reaver's Rear Passage | 'bloodstone_assault' | world-detected | 23:53:06 | unhandled +0, fail-or-stale +0 |  |
| Bower Lake | Brightwood Road | 'bowerlake' | world-detected | 23:54:15 | unhandled +0, fail-or-stale +0 |  |
| Bower Lake | Bowerstone Road | same-region | world-detected | 23:55:17 | unhandled +0, fail-or-stale +0 |  |
| Bower Lake | The Gypsy Camp | same-region | world-detected | 23:56:19 | unhandled +0, fail-or-stale +0 |  |
| Bower Lake | The Tomb of Heroes | same-region | world-detected | 23:57:26 | unhandled +0, fail-or-stale +0 |  |
| Brightwood | Brightwood Tower | 'brightwood' | world-detected | 23:58:40 | unhandled +0, fail-or-stale +0 |  |
| Brightwood | Westcliff Road | same-region | world-detected | 23:59:44 | unhandled +0, fail-or-stale +0 |  |
| Brightwood | Bower Lake Road | same-region | world-detected | 00:00:51 | unhandled +0, fail-or-stale +0 |  |
| Brightwood | Giles's Farm | same-region | world-detected | 00:01:57 | unhandled +0, fail-or-stale +0 |  |
| Brightwood | The Forsaken Fortress | same-region | world-detected | 00:03:05 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Cemetery | The Graveyard | 'bowerstone_cemetery' | world-detected | 00:05:30 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Cemetery | The Graveyard Mansion | same-region | world-detected | 00:06:36 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Cemetery | Old Town Road | same-region | world-detected | 00:07:42 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Cemetery | The Shelley Crypt | 'bloodstone' | world-detected | 00:08:51 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Market | The Town Square | 'bowerstone_market' | world-detected | 00:10:05 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Market | Fairfax Road | same-region | world-detected | 00:11:11 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Market | Old Town Road | same-region | world-detected | 00:12:19 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Market | Bower Lake Road | same-region | world-detected | 00:13:29 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Market | The Gargoyle's Trove | same-region | world-detected | 00:14:39 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Old Town | Cemetery Road | 'bowerstone_slums' | world-detected | 00:15:48 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Old Town | Rookridge Road | same-region | world-detected | 00:16:58 | unhandled +0, fail-or-stale +0 |  |
| Bowerstone Old Town | Market Road | same-region | world-detected | 00:18:08 | unhandled +0, fail-or-stale +0 |  |
| Rookridge | The Rookridge Inn | 'dunecrest' | world-detected | 00:19:23 | unhandled +0, fail-or-stale +0 |  |
| Rookridge | Bowerstone Road | same-region | world-detected | 00:20:32 | unhandled +0, fail-or-stale +0 |  |
| Rookridge | Oakfield Road | 'dunecrest' | world-detected | 00:21:45 | unhandled +0, fail-or-stale +0 |  |
| Rookridge | The Hobbe Cave | 'hobbe_cave' | world-detected | 00:22:57 | unhandled +0, fail-or-stale +0 |  |
| Westcliff | The Westcliff Camp | 'westcliff' | world-detected | 00:24:24 | unhandled +0, fail-or-stale +0 |  |
| Westcliff | Brightwood Road | same-region | world-detected | 00:25:45 | unhandled +0, fail-or-stale +0 |  |
| Westcliff | The Howling Halls | 'dreamworld' | world-detected | 00:26:58 | unhandled +0, fail-or-stale +0 |  |
| Wraithmarsh | Bloodstone Road | 'wraithmarsh' | world-detected | 00:28:11 | unhandled +0, fail-or-stale +0 |  |
| Wraithmarsh | The Drowned Farm | same-region | world-detected | 00:29:30 | unhandled +0, fail-or-stale +0 |  |
| Wraithmarsh | The Well | same-region | world-detected | 00:30:48 | unhandled +0, fail-or-stale +0 |  |
| Wraithmarsh | The Shadow Court | 'shadowcourt' | world-detected | 00:32:02 | unhandled +0, fail-or-stale +0 |  |
| Wraithmarsh | Twinblade's Tomb | 'crucible' | world-detected | 00:33:18 | unhandled +0, fail-or-stale +0 |  |
| Oakfield | The Temple of Light | 'ravenscar' | world-detected | 00:34:48 | unhandled +0, fail-or-stale +0 |  |
| Oakfield | Rookridge Road | same-region | world-detected | 00:36:07 | unhandled +0, fail-or-stale +0 |  |
| Oakfield | The Sandgoose | same-region | WORLD-NOT-DETECTED | 00:38:55 | unhandled +0, fail-or-stale +0 |  |
| Oakfield | The Echo Mine | same-region | world-detected | 00:40:10 | unhandled +0, fail-or-stale +0 | log rotated mid-area: deltas void |
| Oakfield | The Wellspring Cave | 'ritual_cave' | world-detected | 00:41:28 | unhandled +0, fail-or-stale +0 |  |
| Oakfield | Serenity Farm | 'ravenscar' | world-detected | 00:42:48 | unhandled +0, fail-or-stale +0 |  |
| Fairfax Gardens | Fairfax Gardens | 'fairfax' | world-detected | 00:44:40 | unhandled +0, fail-or-stale +0 |  |
| Fairfax Gardens | The Throne Room | same-region | WORLD-NOT-DETECTED | 00:47:30 | unhandled +0, fail-or-stale +0 |  |
| Fairfax Gardens | Lady Grey's Tomb | same-region | world-detected | 00:48:47 | unhandled +0, fail-or-stale +0 |  |
| Guild Cave | The Chamber of Fate | 'guildcave' | world-detected | 00:52:58 | unhandled +0, fail-or-stale +0 |  |
| Bandit Coast | Brightwood Road | same-region | world-detected | 00:54:15 | unhandled +0, fail-or-stale +0 |  |
| Bandit Coast | Westcliff Road | same-region | world-detected | 00:56:27 | unhandled +0, fail-or-stale +0 |  |
