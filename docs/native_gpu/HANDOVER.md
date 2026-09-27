# Native-GPU HANDOVER (fable2 worktree) — START HERE

The Xenos->native conversion for Fable II, census + porting phase. Everything is
LOCAL and UNPUSHED on branch `native-gpu`; nothing ships until RELEASE_GATE passes.

## NEWEST FIRST: `NIGHT_2026-09-22_PORTING.md` (claudecode-4c, the night of 09-22/23)
The porting night: the SDK shader translator compiled INTO the app and proven a
faithful lift (DXBC differential 406/406 vs the plugin's own dump); the untile
differentials (synthetic 672/0, real 44 PASS / 2 unmeasured); the blend-decoder
lift; the census corrected (the ring parse is not a SEEN source - it walks inline
draw data; registers now from the device shadow; shaders from the SetShader
object); the XDK device shadow decoded by its values (59 register slots + 85
XDK-private fields); the first native-vs-plugin picture (geometry right, shading
wrong, branches as red/green stripes); `ngpu_verdicts.txt`. Its top section names
the three things that decide tomorrow, in order. Read it before the older items
below - several of their claims are retracted there (0x2010-0x202D, the 767-
register episode's numbers, "48 unnamed registers").

## Read this first (the census phase, 2026-09-22 day)
**`HANDOVER_CENSUS_2026-09-22.md`** (next to this file) — the full pickup: where the
next session starts, what the census names first-to-port, the validation path and its
first VERIFIED result, what is parked and why, and the two live counterexamples.

## The other docs here
- `CENSUS_FOUNDATION_2026-09-22.md` — the census itself (denominator, surfaces, boundary).
- `PERF_BASELINE_2026-09-22.md` — the town-is-CPU-bound baseline that motivates going native.
- `TEXTURE_CONVERSION_LIFT_PLAN.md` — the first handler (the untile lift).
- `AUTONOMOUS_SESSION_SUMMARY_2026-09-22.md` — the shipping line (v1.0.0) + earlier state.

## One-line state (updated 2026-09-24; HEAD a7994d4)
The census and the untile harness stand as below (552,960 comparisons, 0 mismatches). Since
then (36 commits after 36aba28): the SDK's DXBC translator is compiled in and matches the
plugin's own dump 406/406 (742c646); the M5 coherence census is done - reads are covered by
the translator's emitted vfetch bounds mask, the memexport WRITE side is open (1 shader in
Bowerstone, population unmeasured; 62da478); the striped red/green branches are NOT
provenance and NOT binding (5aa3d51); the morning fixes landed (target never cleared,
resolves never ran, stale ring record, the VRAM leak, mapped heaps write-only -> 60.0 fps in
the Bowerstone stand, 58e0d3b); and the parity stand has run: TOWN = NO MEASUREMENT
OBTAINED (the invariant moved; 8299c0b), LAKE = at parity on every row it can measure,
invariant held to 0.38% (2c4e2fa), gate rule separating within-arm movement from a
between-arm offset, p99 a watch item, third scene pre-registered (a7994d4).

**Next: M5** - render the same draws through the SDK's DXBC and see whether the striped
branches go. The remaining suspects text comparison cannot settle: fetch-constant /
sampler interpretation (format 49 sign/exp/swizzle, clamp/mip), XenosRecomp's body
translation, and the VS texcoord output.

**Before trusting the parity gate again:** state its margin - what would have to move for
it to miss a real difference. On 2026-09-24 the video-seam gate false-passed a broken arm
because its window shifted 0.7 rows with the content box (it had passed by half a row all
along). If the parity gate is ever voided, re-walk the rows it declared AT PARITY; the rows
it separated stand.

Local/unpushed commits: 6b42d1b .. a7994d4 (`git log 6b42d1b^..a7994d4`; the original
seven end at 36aba28).

## 2026-09-24 SIGN1 (Bower Lake) - scope correction, and one census row
- The striped red/green branches were photographed at the BOWERSTONE CEMETERY gate
  (2026-09-22 23:04). SIGN1 ran at BOWER LAKE, and its native frame
  (`shots/2026-09-24_1014_bowerlake_native_SIGN1.png`) shows NO striped branches
  (orange canopy, dark-red ferns; ground black, sky/water patches white, reflections
  wrong). So SIGN1's "no signed data read unsigned" is a BOWER LAKE result about a
  population WITHOUT the subject - it does not exclude the sign hypothesis for the
  stripes. Re-run the per-format sign census where the stripes are.
- The saves changed on 2026-09-23: card 1 = Bower Lake, card 2 = Bowerstone Market.
  The cemetery save the stripes were seen on is no longer card 1; which card (if any)
  still starts there is unknown - ask the user rather than launching to find out.
- CENSUS ROW (new, a real gap): texture fetch-constant GAMMA (sign field 3) is not
  honoured by the native upload - 1 DXT1 texture at Bower Lake (signs 3330).
  Brightness, not channels. Replaces: the plugin's gamma handling in the texture load.

## 2026-09-24 Bower Lake BLACK NEAR GROUND (user's choice "1") - its own row
Separate rows, not one "lake bug": (a) BLACK near ground (bottom ~quarter of the spawn
view), (b) WHITE sky/water patches, (c) broken reflections. Only (a) is worked here.
- PROBE1 (probe 0.20,0.90 = black ground): the only draws the CPU probe finds covering it
  are PS 9A193FEBB26723F4 with VS 36F4DD575DB99917 / 2A5207A8748C72CB (ndc z .984/.995).
- DROP1 (those draws dropped) and WHITE1 (its slots whitened): the probe point stays
  (0,0,0) in BOTH. WHITE1's frame (shots/2026-09-24_1025_...whitened.png) shows 9A193FEB is
  the lake's main world material (arches, cliffs, rocks, hero, mid bank all turn white) -
  and the near band stays black. So the near ground is NOT a wrong colour from 9A193FEB:
  nothing that runs paints it.
- TESSFLAT1 (ngpu_tess_flat, applied): 0.0% magenta in rows 560-720, 71% black, and no
  patch/tessellation counter fires at the lake at all - the ground here is not coming
  through the patch path.
- COVERAGE (the native path's own line, every leg): the game issues 803 draws/frame (348
  indexed, 384 non-indexed, 71 user-pointer); the native path draws 317 (39%) and "turns
  away no primitive". ~486 draws/frame vanish before the primitive check with no counter
  naming why. The near ground is presumably among them - NOT YET SHOWN.
- NEXT: a drop-reason census at the native draw hook (every early return counted by reason
  and by indexed / non-indexed / user-pointer), so the 486 are named before anything is
  changed. Healthy-case check with it: the 317 drawn must include 9A193FEB's draws.
- DROP-SITE CENSUS (every bare return in ShadowDrawVertices + ShadowDrawIndexedImpl tagged by
  source line; entries counted - nothing leaves uncounted). DROPS2 (defaults): 0 non-indexed
  draws enter the draw path - all ~386/frame leave at `ngpu_draw_vertices` (default OFF,
  "they cover the scene with dark quads so far"); 348 indexed all take the DrawTranslated
  success exit, yet the frame counts ~317 drawn (~30/frame lost INSIDE the translated path,
  unnamed - a row of its own).
- Legs, near band rows 560-720 % black (SIGN1/TESSFLAT1 baseline 71.2%):
    DRAWV1  ngpu_draw_vertices on (native 572/803 = 71%)        73.8%  - not the ground
            (~129/frame still leave at the QUADLIST line = point lists, prim 1)
    DEPTHALL1  every draw depth-ALWAYS                         67.9%  - no running draw covers it
    UPON1   draw_up + depth-ALWAYS                              0.0%  - VOID: a full-screen
            user-pointer pass paints everything when depth is off (frame = sky/water only)
    UPON2   draw_up on, normal depth                           30.2%  - the black turns SKY/FOG
            coloured (shots/2026-09-24_1043_...updraws_on.png): the backdrop shows through
- VERDICT for row (a): the near ground is NOT a colour defect. NO INTERCEPTED DRAW PUTS
  GEOMETRY IN THE NEAR BAND (measured by where fragments land - NOT 'the terrain is absent from
  every intercepted draw', which was the original wording and is stronger than the evidence:
  a displaced or degenerate terrain draw would read the same) (indexed, non-indexed, points, user-pointer,
  depth-forced). The ground reads black only because the backdrop (user-pointer draws,
  default off) is off too. SUSPECT, NOT SHOWN: the library draw emitters - "fired 50 times
  this frame, none of them through the three wrappers we hook" (every leg) - i.e. draws that
  never enter the 803/frame count at all. Next: hook the emitter (or census its draws by
  surface/shader) and see whether the near terrain is among them.
- CORRECTIONS to the Bower Lake legs above (claudecode-76's review, accepted):
  1. NO NOISE FLOOR was measured for the near-band metric (one baseline pair, no repeats).
     DRAWV1 (73.8%, +2.6) and DEPTHALL1 (67.9%, -3.3) are NOT SEPARATED from the baseline -
     they carry no direction, and non-indexed draws are NOT retired as a candidate by them.
     The verdict rests on UPON2 (-41.0 points, backdrop visible through the hole) and the
     drop-site census, not on those two arms.
  2. THE DENOMINATOR IS A LOWER BOUND. "The game issues 803 draws/frame" counts only the
     three hooked wrappers; the library draw emitters (~50/frame, "none through the three
     wrappers we hook") are outside it. Every "317 of 803 (39%)" / "572 of 803 (71%)" in this
     file is over an INCOMPLETE population; quote it as "of >= 853" or fold the emitters in.
  3. SCOPE: "terrain geometry absent" explains row (a), the black near ground, ONLY. It says
     nothing yet about (b) white sky/water patches, (c) broken reflections, (d) the canopy /
     fern colours - those stay open rows, not carried by association.
- EMITTER CENSUS (EMIT1, observation only - raw pointers, no translation triggered). All
  FIVE DRAW_INDX_2 emitters in native_gpu_trace.cpp now report (82B99018, 82BA7B28, 82BA83C0
  were hooked but silent). Per frame: e0 (82B9EEE0) 25, e1 (82B9F038) 25, e2/e3/e4 ZERO at the
  lake (hooked, never called here). DENOMINATOR at the lake: 805 wrapper draws + 50 emitter
  fires = ~855/frame, every known emitter enumerated; the zero-fire three are counted, not
  absent. All 50 fires use ONE shader pair (VS obj 4C931D90 / PS obj 42CC0850, a few with
  none bound) over ~5 r5 objects, into surfaces 04020118 (40/frame) and 14000500 (10/frame) -
  NOT the scene surface 14010500 where PROBE1 put the ground. So the near terrain is NOT an
  emitter draw (by surface). Lead, not verdict: one material, inline microcode, non-scene
  surfaces = housekeeping (the rect-over-EDRAM-alias depth clear noted at
  ngpu_depth_alias_clear is a candidate) - bears on nothing in rows (b)-(d) yet.
- NEXT SUSPECT for row (a): the ~30 indexed draws/frame that take DrawTranslated's SUCCESS
  exit yet are not counted drawn (348 entered vs ~317 drawn). Name them by shader and by the
  return inside DrawTranslated that ends them.
- DrawTranslated's exits tagged too (DROPS3): the "~30 lost" are 31/frame at the
  ngpu_skip_impostor return - ORTHOGRAPHIC skinned draws (impostor renders), skipped by
  design. So the lake's indexed draws are fully accounted: 317 drawn + 31 impostor skips =
  348. With the emitter census, EVERY draw class that reaches the native path is named:
  317 indexed drawn, 31 impostors skipped, ~255 non-indexed (off by default), ~129 point
  lists, 71 user-pointer (off by default), 50 emitter fires into non-scene surfaces.
- ROW (a) NOW NARROWS TO: no intercepted draw puts geometry in the near band, even with
  depth ALWAYS. Remaining explanation not yet excluded: the terrain IS drawn but its
  vertices land elsewhere - Fable's terrain takes height from a heightmap sampled in the VS
  (the 769x769 map, ngpu_vs_textures / ngpu_tex_scale_addr history). Next: find the draws
  that bind the heightmap and check where their vertices land (probe at a near-band point
  with those draws isolated, or the edge dump on that VS).
- BY IDENTITY, NOT BY OUTCOME (claudecode-76's method point): the terrain VS is
  2D96AFB187B6B7F6 (night record: "the only tessellated VS drawing to the scene", PS
  B950F34C5012D6F7). IDENT1 (the full-size-target VS line now prints the translated hash
  beside the code hash): it IS intercepted and drawn at the lake - 6 draws/frame, 19,383
  indices, into the full-size target. So row (a) is NOT a submission problem.
  (Hash-join caveat: of the 17 full-size VS only 2 have census binding files this run -
  the other 15 are UNCHECKED for vertex-texture fetch, not "none".)
- ONLYVS1 (ngpu_only_vs = that VS, every draw depth ALWAYS): the terrain's draws cover
  0.2% of the frame (a small patch left, a sliver centre; shots/2026-09-24_1102_...png).
  PRESENT BUT DEGENERATE. Leading explanation (not shown): a TESSELLATED VS drawn as plain
  triangles - at the lake no tessellation/patch counter fires (TESSFLAT1), so the patch
  emulation that paints part of Bowerstone's ground is never engaged here and the VS reads
  patch inputs it is not given. Next: log the primitive type and HOS_CNTL / VGT_HOS
  registers AT those 6 draws, and why the patch path does not take them.
- WHY THE PATCH PATH NEVER RAN AT THE LAKE (positive control found, then the cause):
  * The counters are NOT silent instruments: aliasfix (2026-09-22 06:35, fable2recomp/out)
    logged "bridge: 4792 draws arrived from the plugin's parser" and "TESSELLATION ... quad
    74220 ... issued 74100 (continuous 55740)" - they fire when fed.
  * At the lake, BRIDGE1 (ngpu_bridge/_log/_draws on): "bridge: 0 draws arrived from the
    plugin's parser". The exe looks up RexNgpuSetDrawCallback; NO plugin in RexBlue\bin
    (v1.0.1, v1.0.0, every dll_backup) exports it - it exists only in rexglue MAIN builds.
    The native worktree has been pinned to the v1.0.0 (hotfix-branch) pair all day, so the
    bridge - and with it the tessellation emulation - could NEVER engage in any of today's
    native legs. Every "no patch counter fires" today is NO MEASUREMENT OBTAINED for patches.
  * Heightmap-collapse hypothesis (flat sheet at base height) DIED on ONLYVS1: the terrain
    VS alone gave scattered scraps, not a sheet.
- PRE-REGISTERED for BRIDGE2 (clean main pair: plugin b57537ba177c exports the callback,
  runtime d190e2093473; bridge log+draws on; NOT a parity leg - two things differ, plugin
  and bridge): (1) "draws arrived" > 0; (2) a TESSELLATION line fires with quad patches;
  (3) near band black falls well below 71%; healthy case: arches/rocks/hero still drawn.
  If (1) holds and (3) does not, tessellation is not the mechanism for row (a).
- BRIDGE2 RESULT: ABANDONED - the native exe with the clean MAIN pair (plugin b57537ba177c,
  exports all three RexNgpuSet* callbacks; runtime d190e2093473) CRASHES ~2 s after launch,
  before the title menu: "exception 0xc0000005 at 0x0" (a call through a null pointer;
  minidump in out/). The bridge DID receive draws during boot (26 arrived), so the feed is
  wired; the crash is something else. Predictions (2)/(3) are UNTESTED, not failed. The
  near-band figure printed by that leg came from a STALE shot - void.
  Which plugin the working 2026-09-22 bridge runs (aliasfix, 4,792 arrivals/frame) used is
  NOT recorded - their artifact files do not exist. Next: symbolize the minidump, or
  identify that plugin; until then no native leg can exercise the tessellation path.
  The native build dir is back on the v1.0.0 pair (2e4ec3f16be3).
- BLAST RADIUS of "the bridge never engaged today" (claudecode-76, accepted):
  SURVIVES (bridge-independent - all come from guest-side trace hooks or the hooked draw
  path; the bridge received 0 draws in every one of those legs, so nothing came through the
  callback): the five DRAW_INDX_2 emitters (50/frame, surfaces, e2-e4 hooked-never-called);
  the ~855/frame denominator; the 31/frame impostor skips; terrain VS 2D96AFB1 intercepted,
  6 draws/frame, 19,383 indices, full-size target.
  DOES NOT SURVIVE (anything about what the frame LOOKS LIKE): "terrain degenerate, 0.2%
  coverage" (exactly what a tessellated VS with the emulation disabled produces - explained
  by a disabled subsystem, not evidence of a porting defect); the 71.2% baseline and all
  near-band arms; rows (b) white sky/water, (c) reflections, (d) canopy/fern colours - all
  measured with the emulation structurally unable to engage. None is evidence yet.
  v1.0.1 is untouched: it was measured on the shipping pair through the PLUGIN path.
- BINARY PROVENANCE: today's legs DO record their binaries (ab_untile_leg.sh writes
  out/<tag>.artifact.txt with exe/plugin/runtime sha256 - BRIDGE2 shows plugin b57537ba...).
  The 09-22 aliasfix runs came from a different runner with no artifact file, which is why
  their plugin is unidentifiable. The lock note now carries the plugin hash too.
- THE BOOT CRASH, DEBUGGED (minidump crashdumps/fable2-20260924-110704.dmp, cdb): BridgeLogReplay
  -> DrawTranslated -> CachedStream+0x2f5 -> call 0x0. CachedStream calls g_enable_fn, which is
  resolved ONLY inside BindWatch(); the hooked path calls BindWatch() before DrawTranslated
  (line ~13470), the replay did not. At boot the replay draws first (26 arrivals, 0 hooked
  draws), so the pointer was still null. The 09-22 runs presumably survived by ORDER (a
  hooked draw bound it first). FIX: the replay's issue_draw calls BindWatch() first;
  CachedStream bypasses (counted as cache_unmapped) instead of calling a null pointer.
- BRIDGE3 (fix + clean main pair b57537ba/d190e209, bridge on) - scored against BRIDGE2's
  pre-registration: (1) MET - 1,991 draws/frame arrive from the plugin's parser, 187,688 of
  303,488 recorded draws issued; (2) MET - TESSELLATION fires: 87,240 quad patch draws
  issued per window (continuous 53,040 / adaptive 34,200); (3) VOID - the near band reads
  0.0% black, but the presented frame is FLAT BLOCKS (shots/..._BRIDGE3_replay_frame.png),
  not a scene: with the replay on, the picture fails wholesale, so it says nothing about the
  terrain. No crash. The native build dir is restored to the v1.0.0 pair.
- STATE OF ROW (a): the patch emulation can now run at the lake; whether it produces the
  near ground is BLOCKED on the replay's presented frame (tiling / bins / rect lists - the
  replay's own open items), not on the terrain.
- THE BRIDGE WORKS - the first POSITIVE native-path result today (BRIDGE3: fed, 1,991
  arrivals/frame; tessellation engaged, 87,240 quad patch draws issued/window). Everything
  measured before it today was taken with the bridge structurally disabled.
- NEW ROW (e): THE NATIVE PATH WITH TESSELLATION ENGAGED PRESENTS FLAT BLOCKS. The replay is
  NOT a diagnostic harness: ngpu_bridge_draws is described as "REPLAY the recorded bridge
  draws ... This is the draw path the whole migration is for", and the tessellation emulation
  exists ONLY inside it (~lines 9546-9818) - the hooked path cannot tessellate. So the flat
  blocks belong to the native path itself, not to an instrument. Open caveat: the shot may
  read a different surface under replay (the swap surface is learned from the packet) - the
  CAPTURE is flat blocks; that the PRESENTED frame is has not been checked separately.
  Row (a) cannot be answered until (e) is.
- POPULATIONS, not a comparison: aliasfix (2026-09-22 06:35, region NOT recorded - its log has no region line,
  plugin unidentified) 4,792 arrivals/frame, 74,220 quad patches/window; BRIDGE3
  (2026-09-24, bowerlake, plugin b57537ba) 1,991 arrivals/frame, 87,240 quad patches/window.
  Different scene, window and plugin - aliasfix's only job was to show the counters fire.
- ROW (e) CONFIRMED BY AN INDEPENDENT INSTRUMENT (BRIDGE4, same config as BRIDGE3): the
  PRESENTED native window ("Fable II - native shadow (Plume D3D12)") captured by WGC and by a
  GDI screen copy of its rectangle - neither shares code with ngpu_shot - shows NO scene: two
  dark-red halves (left/right, tile/bin-shaped) and a few huge flat polygons (brown, grey)
  (shots/2026-09-24_1120_..._presented_window_WGC.png; WGC mean rgb 78,46,44 vs GDI 80,49,46).
  ngpu_shot and the window AGREE the frame is not a scene, so row (e) is REAL, not a capture
  artifact. (eye_look was not used: it silently falls back to a screen copy on the Plume
  window - WGC and GDI were called directly instead.)
- BLAST RADIUS, STRONGER FORM: every pre-bridge native leg lacked the tessellation emulation
  BY ARCHITECTURE, not only by configuration - the hooked path cannot tessellate at all; the
  emulation exists only inside the replay.
- REJECTED INSTRUMENT: eye_look / eye_raise on the native window. It silently falls back to
  a screen copy on the Plume shadow window, so it would give a confident reading of the WRONG
  surface - the very defect a cross-check exists to rule out. Use WGC on the window handle
  plus a GDI copy of its rect (D:\fable2_flash\gameplay\grab_windows.py) instead.
- PRE-REGISTERED before reading BRIDGE3/4's replay lines (claudecode-76's prediction): if the
  replay's bin/rect setup is wrong, EXACTLY TWO bins or rects cover the target (the left/right
  halves), and the emulated patches carry degenerate or default tessellation factors (the
  huge flat polygons).
- REPLAY CHARACTERISED (BRIDGE4 log, scored against the pre-registration above):
  * "exactly two bins": REFUTED AS WRITTEN - SIX bin_selects (00000002, 00000008, 0000000C,
    80000001, 80000003, FFFFFFFF) - but they map to exactly TWO PA_SC_WINDOW_OFFSETs
    (0 and 7E000000), consistent with the two left/right halves. Tile scissor is OFF
    (ngpu_replay_tile_scissor default false): 0 draws clipped to their tile.
  * "degenerate/default tess factors": REFUTED - adaptive levels 1..15 -> T16, min T2;
    factors T2:34,200 / T16:53,040 draws, 768,120 patches/window. The huge flat polygons are
    NOT explained by default factors.
  * The replay issues 189,360 of 306,000 recorded draws over 120 frames (62%): 116,520 (~970/
    frame) turned away as "not an indexed draw" (ngpu_replay_autoindex default OFF); RECT_LIST
    ~3,600/window not expandable ("no source vertices").
  * LEADS for row (e), not verdicts: (1) two window offsets with no tile clipping - both
    halves painting over one target; (2) the ~970/frame dropped auto-index draws, which may
    include full-screen passes the frame depends on.
- UNITS for the line above: "189,360 issued of 306,000 recorded, 116,520 not indexed" is ONE
  120-frame window = 1,578 issued / 2,550 recorded / 971 non-indexed PER FRAME (62%). The
  RECT_LIST "~3,600 unexpandable" is a DIFFERENT report window and is not a component of the
  62%. Recorded 2,550/frame vs the hooked path's ~855/frame: presumably the six-bin predicated
  tiling (the parser sees each bin's copy of a draw, the hook sees one call) - an explanation
  to check, not a fact.
- PRE-REGISTERED for AUTOIDX1 (BRIDGE4 config + ngpu_replay_autoindex=true, one switch):
  issued/recorded rises toward 100% minus the rect lists; if the two halves persist unchanged,
  the two window offsets are the real subject of row (e).
- AUTOIDX1 RESULT (ngpu_replay_autoindex on, else BRIDGE4; no crash; window by WGC/GDI,
  mean 93,64,66 / 95,66,68): issued 193,301 of 290,833 recorded (66%, from 62%); 68,220 still
  turned away as "not an indexed draw" (from 116,520 - the switch covers only part of them).
  Prediction 1 ("rises toward 100%") REFUTED. The presented window still shows the two
  dark-red halves, now with large flat triangles fanning from the screen centre and a
  vertical column (shots/2026-09-24_1124_...png) - NOT a scene. Prediction 2's condition
  ("halves persist") is MET: the two window offsets (0 / 7E000000, six bins, tile scissor
  off) are the leading subject of row (e). The dropped auto-index draws are not what is missing.
- AUTOIDX1 IS A VALID MEASUREMENT (claudecode-76 asked whether the switch engaged): the 68,220
  still "not an indexed draw" are EXACTLY 64,745 point lists + 3,475 rect lists - auto-index is
  allowed only for prim 4/6 by design. It DID engage: the non-indexed bucket fell ~47k and
  auto-index draws issued rose 58,440 -> 69,446. The SECOND refusal reason, new with the
  switch: "the draw path refused" 29,312 (0 in BRIDGE4) - converted draws rejected by
  DrawTranslated. That, not the switch, is the auto-index subject now.
- PRE-REGISTERED for TILESC1 (ngpu_replay_tile_scissor) and TILEBAND1 (ngpu_replay_tile_band),
  each ONE switch against BRIDGE4's config: if the two halves come from unclipped overlapping
  tile passes, clipping changes the halves' content toward a coherent picture; if the window
  is unchanged, tiling is not row (e)'s mechanism either.
- TILE LEGS (each one switch vs BRIDGE4; no crash; saves restored):
  * TILEBAND1 (ngpu_replay_tile_band): 6,376,160 draws confined to their tile's band. The
    presented window (WGC, shots/2026-09-24_1129_...png) still shows the two dark-red halves,
    one large flat polygon and the column, plus a few small scene-like fragments at the bottom.
    NOT a scene. Pre-registered verdict: clipping by band does NOT make the frame coherent -
    tiling-by-band is not row (e)'s mechanism.
  * TILESC1 (ngpu_replay_tile_scissor): 185,862 draws clipped to their tile's scissor. Its
    window capture was OVERWRITTEN by TILEBAND1's (a shell-quoting slip wrote both legs to one
    folder); only its means survive, WGC 73,38,40 / GDI 75,41,42 vs BRIDGE4 78,46,44. A mean
    cannot say whether the picture is a scene: TILESC1's visual verdict is NO MEASUREMENT
    OBTAINED - re-run with its own folder if it matters.
  * What remains for (e): the huge flat polygons themselves (present in every bridge leg, with
    real tess factors) and the dark-red fill of each half; the 29,312 draw-path refusals under
    auto-index.
- PRE-REGISTERED for TESSOFF1 (BRIDGE4 config + ngpu_tess=false, one switch): if the huge flat
  polygons are the emulated terrain patches (placed wrong), they VANISH with the emulation off;
  if they stay, they come from other draws.
- TESSOFF1 (BRIDGE4 + ngpu_tess=false): the huge flat polygons and the column PERSIST
  (shots/2026-09-24_bowerlake_TESSOFF1_...png). Pre-registered verdict: they are NOT the
  emulated terrain patches - they come from other draws.
- NEW ROW (f): the 29,312 draw-path refusals under auto-index are ALL "draw: range" (DrawTranslated
  line ~7629, xs_skip_range) and ALL ONE key: VS DFB44E4B, stream 0, surface 05000140, mean
  overshoot ~90.6 KB past the fetch constant's declared size (55k draws per print). One shader
  whose auto-index range is computed past its buffer - likely the index offset or count applied
  wrong for auto-index. Not yet investigated.
- TILESC2 (re-run of TILESC1 through tools/native_gpu/bridge_leg.sh, which builds the capture
  folder from the tag in python and ASSERTS it exists): 188,268 draws clipped to their tile
  scissor; the presented window still shows the two halves and huge flat polygons, a few
  fragments near centre-bottom (shots/..._TILESC2_...png). Pre-registered verdict: tile SCISSOR
  clipping does not restore a scene either. TILESC1's "no measurement" is superseded by this.
  (TILESC1's means vs BRIDGE4 were n=1 with no floor and are NOT cited as a difference.)
- Row (e) board now: the huge flat polygons (not tessellation, not tiling), the dark-red fill of
  each half, and row (f).
- ROW (f) FACTORED (AUTOIDX1's own "range refusal sample" lines; VS DFB44E4B = the RECT_LIST
  shader, layout TEXCOORD0 fmt6 @0 + TEXCOORD1 fmt16 @12, stride 20; fetch 1B0E1C40):
    start 0  count 48 -> need   960 B = 48 x 20; declared 96 B -> overshoot   864 B
    start 48 count 48 -> need 1,920 B;           declared 96 B -> overshoot 1,824 B
    start 96 count 48 -> need 2,880 B;           declared 96 B -> overshoot 2,784 B
  * The DECLARED size is GUEST-SUPPLIED (fetch constant dword 1 bits 2..25 via FetchWord from the
    guest register state), not natively computed.
  * 96 B = exactly 48 x 2 B - the draw's own count times two; need/declared = 10 = stride(20)/2.
    The stream holds TWO bytes per element where our layout reads TWENTY: the native layout for
    this stream (or what "count" means for these auto-index rect draws) is the suspect, not the
    guest. The overshoot also GROWS with start = VGT_INDX_OFFSET (0,48,96 ...): the replay adds
    the running index offset to a stream whose declared size never grows - the ~90.6 KB mean is
    that accumulation (mean start ~4,600). Two suspects, both ours; not yet separated.
  * (e) vs (f): DFB44E4B draws into surface 05000140, NOT the scene surface, and every one of its
    draws is refused (never issued) - it cannot paint the scene's polygons. Whether OTHER draws on
    fetch 1B0E1C40 issue with garbage is NOT checked. The rows stay separate.
- PRE-REGISTERED (claudecode-76's hypothesis for row (f)): the replay binds the INDEX BUFFER as
  vertex stream 0; PREDICTION: these draws also bind a SECOND fetch constant sized ~960 B at
  stride 20 (the real vertex stream). If 1B0E1C40 (96 B) is the only one, the hypothesis dies.
  Note against it before looking: the samples read ib_phys 00000000 - these are AUTO-INDEX draws
  with NO index buffer.
- FCDUMP1 (bridge + auto-index; at each DFB44E4B range refusal, EVERY vertex fetch constant bound):
    st0 1B0E1C40 96 B | st1 1B0DF9C0 192 B | st2 1F3F7000/1F407000/1F436000 64,000 B |
    st3 744-2,160 B (varies)   - all endian 2, all type 3 (vertex)
  VERDICT on the pre-registered hypothesis: REFUTED AS WRITTEN - no fetch constant near 960 B, and
  these are auto-index draws with no index buffer. BUT a buffer that divides EXACTLY by the
  layout's stride IS bound: st2 = 64,000 B = 3,200 x 20, while the replay reads st0 (96 B, divides
  by 2). New leading suspect for row (f): STREAM-SLOT SELECTION - which fetch constant the
  shader's "stream 0" maps to (the SDK numbers vertex fetch constants 95 downward; D3D9 stream s
  = constant 95-s). The binding diff that "matched 52/52" was run on BOWERSTONE's shaders -
  DFB44E4B is a lake shader outside that population, so that match does not cover it.
  Next: compare DFB44E4B's vfetch constant indices (SDK analysis / XenosRecomp HLSL) with the slot
  the replay reads.
- ROW (f), TWO OPEN QUESTIONS BEFORE ANY SLOT FIX (claudecode-76; its index-buffer hypothesis is
  REFUTED as written - the specific ~960 B prediction was wrong; st2 = 3,200 x 20 is a separate find):
  1. MAX start / reset per frame: NOT MEASURED - the refusal sampler caps at 3 samples per print
     and every sampled group reads start 0/48/96 (hints a per-frame reset, proves nothing about the
     max). ARITHMETIC ONLY: mean overshoot ~90.6 KB => mean start ~4,600 elements; if starts run
     from 0 each frame the max is ~2x that, PAST st2's 3,200 elements - so the slot fix alone would
     likely still be refused, and the running VGT_INDX_OFFSET is probably a SECOND defect (own row
     once measured). Instrument needed: log the max start per frame for DFB44E4B.
  2. IS st2 THE SLOT THE LAYOUT DECLARES: NOT DERIVED - FCDUMP1 used the replay's own FetchWord
     indexing; mapping the layout's "stream 0" through the 95-s convention needs FetchWord's
     indexing and DFB44E4B's actual vfetch constant (SDK analysis / XenosRecomp HLSL). Until then
     st2 is "the right size", not "the right stream".
  - SCOPE, kept explicit: the 52/52 vertex-binding match (2026-09-23) covers BOWERSTONE's shaders
    only; DFB44E4B (a lake shader) is OUTSIDE it and that match is no reassurance here.
- ROW (f), FURTHER (source + logs, no leg):
  * SLOT: in replay, FetchWord(s,k) = regs[0x4800 + ngpu_vfetch_base(190) - 2s + k] => stream s =
    vertex fetch constant 95-s. DFB44E4B's layout (ngpu_cache .layout, XenosRecomp HLSL) reads
    TEXCOORD0 fmt57 (32_32_32_FLOAT) @0 and TEXCOORD1 fmt37 (32_32_FLOAT) @12, stride 20, both on
    input stream 0 (the second fetch's index is COMPUTED). The same shader's RECT_LIST draws
    (3 vertices) fit st0's 96 B; only its 48-count triangle-list draws overshoot.
  * The replay sets auto_index = (src != 0), lumping kImmediate (1, indices inline in the packet)
    with kAutoIndex (2) - and the plugin sets inline_addr = 0 for kImmediate, so inline indices
    are never recorded. BUT the SDK plugin itself does not support kImmediate (it logs "Using
    immediate vertex indices, which are not supported yet" and fails the draw), and that error
    appears ZERO times in the shipping-path logs (r1_lake_U, r1_town_U, smoke_v101_town) and in
    FCDUMP1. So these draws are almost certainly TRUE auto-index (src 2), and the immediate-index
    lumping, while wrong in principle, is probably not what fires here. (The refused draws' src
    was not logged - strong inference, not measurement.)
  * VISIBLE CONSEQUENCE, probably none: for auto-index, the plugin's DXBC vfetch bounds-check
    returns ZERO past the declared 96 B, so on the shipping path these draws produce degenerate
    geometry; the native range guard refuses them - the same nothing. Row (f) is LOW PRIORITY
    unless the src log says otherwise. The (src != 0) lumping is a latent correctness bug worth a
    one-line fix later (auto_index = src == 2; refuse src 1 as the plugin does).
- ROW (e) - PROBEB1 (bridge on, probe at 0.25,0.60 = inside the big grey polygon in every bridge
  capture): the draws covering it are ORDINARY WORLD GEOMETRY - VS 36F4DD57 / 2A5207A8 + PS
  9A193FEB (the lake's main material), VS 1CF397A6 + PS D6F02D0B, VS E4D8ABB1 + PS C3620F8C, all
  into the scene surface 14010500 at ndc z 0.984-0.995 - each covering the point with only 1-3 of
  its 1,995-4,016 triangles on the CPU transform (small, sane triangles). The GPU shows ONE HUGE
  FLAT POLYGON there. So for the SAME replayed draws, the CPU transform (c0..c3 + the draw's own
  vertices/indices) and the GPU transform DISAGREE; and the hooked path draws these same shaders
  correctly (bridge off: arches, rocks, hero fine). LEAD: what the REPLAY uploads to the GPU for
  these draws differs from what the probe reads - most plausibly constants beyond c0..c3 (a
  world matrix or similar) stale/zero in the replay's constant upload. NOT shown. Next: for one
  replayed draw of VS 36F4DD57, dump the constant buffer the GPU receives against the register
  file's c0..c15 at that draw.
- CORRECTION to the PROBEB1 lead: under replay the GPU's VS constants are a straight copy of
  RingRegsShadow()[0x4000..0x43FF] (c0..c255, DrawTranslated ~line 7730) - the SAME source the
  probe's c0..c3 come from; and the probe reads the same cached vertex/index data the draw binds.
  So "constants beyond c0..c3 stale in the replay upload" is WEAKENED. The likelier reading: the
  huge polygon is a DIFFERENT draw the probe cannot see - the probe skips draws without a POSITION
  element or without an index buffer (auto-index / computed-position draws) - painting over the
  world draws it did name (ndc z ~0.98, far). Next: bisect by bin (ngpu_replay_bin replays one
  bin_select) to find the pass that draws the polygon, then by draw sequence within it.
- PRE-REGISTERED for the BIN legs (bridge + ngpu_replay_bin = each of the six bin_selects, one leg
  each): the huge polygon appears in only SOME bins, and those bins name the pass that draws it.
- BIN LEGS (bridge + ngpu_replay_bin, one bin_select each; montage shots/2026-09-24_bowerlake_BIN_legs_native_windows.png):
  * 0000000C, 80000003: recognisable SCENE PIECES - a wooden pier/boat structure, cyan/white sky,
    black ground - but NOT the spawn view (the plugin window, same moment, shows the arches over
    the lake from the shore).
  * 80000001: the "Loading Bower Lake" banner and the loading bar, nothing of the world.
  * FFFFFFFF: the wooden structure, lake water, AND the loading UI together.
  * 00000002, 00000008: NO MEASUREMENT - world loaded, no crash, but the replay never printed its
    report and no native window was visible to capture (win0 was the PLUGIN window; the
    enumeration order is not stable - the montage now picks the native window by its 1282 px width).
  * Pre-registered verdict ("the polygon appears in only some bins"): superseded by a bigger
    observation - single bins show COHERENT content from the WRONG TIME: the loading screen and a
    view that is not the spawn view. NEW LEAD for row (e): the replay presents draws RECORDED
    DURING THE REGION LOAD (a stale recording), and the all-bins frame is those passes painted
    over one another. Not shown - next: timestamp/sequence the recording the replay presents
    against the live frame counter.
- CORRECTION to the bin-leg lead ("the replay presents a recording from the region load"):
  * The recording is swapped and RESET at every bridge frame boundary (OnBridgeSwap -> handoff ->
    BridgeLogReset), capped at 32,768 draws/frame.
  * BRIDGE4 (all bins): "2,527 draws ... handed over for the last bridge frame; 0 draws dropped at
    the cap", 27 times in the run - the normal replay presents a FRESH per-frame recording. The
    stale-recording lead is REFUTED for the all-bins picture.
  * The BIN legs print that handover line ZERO times: under ngpu_replay_bin the per-frame handoff
    apparently never happens, so those replays likely present an old, never-reset recording -
    which explains the loading banner. The bin legs' "coherent stale content" is therefore most
    likely an ARTIFACT OF THE SINGLE-BIN MODE and does NOT transfer to row (e). Their content is
    not evidence about the all-bins frame; the bin mode itself needs fixing before it can bisect.
- CORRECTION TO THE CORRECTION (the previous bullet is WRONG in its second half):
  * OnBridgeSwap hands off on EVERY swap regardless of the bin filter; the "handed over" line is
    printed from EndFrame at frames%120 only when g_bridge_ready is set at that instant, so its
    absence proves nothing about handoffs.
  * Measured: the bin legs PRESENT LIVE FRAMES - 4,800 native frames each, last frame 432-694
    native draws, bin filter matched 13,800 (0C) / 29,520 (80000001) / 45,240 (80000003) /
    38,160 (FFFFFFFF) draws per 120 bridge frames. The bin legs' content is LIVE, not stale.
    (BIN_00000002 printed neither line - no data.)
  * So the "Loading Bower Lake" banner and loading bar are DRAWN EVERY FRAME in gameplay. Lead,
    not verdict: a hidden UI element (the loading widget kept alive but invisible in-game) that
    the native path draws visibly - alpha / blend / visibility state not honoured. That would also
    be a candidate for part of row (b)'s white patches. The single-bin mode IS usable for
    bisecting after all.
- CONFOUND IN EVERY BRIDGE LEG TODAY: ngpu_hooked_draws defaults TRUE and no bridge leg overrode it
  (BRIDGE4's override echo lists only census/shadow/native_draws/dump/untile/bridge*/shot). So the
  shadow window showed the HOOKED frame AND the REPLAY painted over each other. The cvar's own help:
  turning it off "is the only way to tell a correct replay from a replay that draws nothing on top
  of a correct hooked frame". Every row-(e) picture (BRIDGE3/4, AUTOIDX1, TILESC2, TILEBAND1,
  TESSOFF1, PROBEB1, the BIN legs) is a TWO-RENDERER composite - their visual verdicts are
  suspect until re-taken with the replay alone. (The counters - arrivals, tessellation, issued -
  are unaffected.)
- PRE-REGISTERED for REPLAYONLY1 (BRIDGE4 config + ngpu_hooked_draws=false): coherent frame =>
  row (e) was the double painting; still broken => the replay itself is broken, now shown clean.
- REPLAYONLY1 (bridge on, ngpu_hooked_draws=false - override echoed; 187,680 issued of 303,480
  recorded, same as BRIDGE4): the REPLAY ALONE presents two flat dark-red halves (top, split at
  the centre, left darker) and ONE HUGE FLAT LIGHT-GREY PLANE (bottom, horizon at mid-height) with
  a thin red/black diagonal line (shots/..._REPLAYONLY1_replay_alone_WGC.png). Pre-registered
  verdict: STILL BROKEN => row (e) is the REPLAY'S OWN DEFECT, now established without the
  hooked-path overlay. What the replay's ~1,560 issued draws/frame put on screen is two flat
  tile fills and a single giant plane - consistent with nearly every replayed draw landing off
  screen, degenerate, or depth-rejected, and one plane-like draw surviving.
  Next: with hooked draws off, name the draw that makes the plane (drop-by-shader bisection over
  the replay, or the per-draw dump if the plane is on the scene target), and check a replayed
  draw's viewport / window-offset transform - the two tile offsets (0, 7E000000) with no
  per-tile viewport correction would put most geometry off screen.
- THE MISSING CLEAR (the USER's observation, 2026-09-24: "Aren't you missing a clear state, I see
  many textures trailing previous animations ... I don't think the invisible menu holds"). The
  "hidden UI element" lead is WITHDRAWN - the banner is left over, not drawn invisibly. Found:
  1. RB_COPY_CONTROL (0x2318), which carries a resolve's clear-after-copy bits, is NOT in
     kBridgeLogRanges, and ResolveNative read it from g_ring.regs (the retired ring parser, not
     running): all six samples in BRIDGE4 and REPLAYONLY1 read 00000000 - no replayed resolve
     ever cleared.
  2. Even a captured request was discarded: the clear applied only if the target was re-bound in
     the SAME frame, and the next frame's first bind reset the flags without clearing - so every
     END-OF-FRAME "clear after copy" was lost.
  FIX (uncommitted until measured): the resolve marker fires synchronously from the plugin's copy
  path, and every bridge draw hands over the plugin's LIVE register file - so OnBridgeResolve reads
  regs[0x2318] then and stores it on the record; the replay passes it to ResolveNative; and a
  pending clear applies at the next bind even across the frame boundary when replaying.
  (The counter "targets cleared because a resolve asked" could not have shown this: reset every
  frame, printed only at frames%300 - it is absent in the hooked legs too.)
- PRE-REGISTERED for CLEAR1 (fix, bridge on, hooked draws OFF): the copy-control samples show clear
  bits on at least some resolves, and the accumulation - loading banner, trails - is GONE from the
  replay-alone frame.
- CLEAR1 RESULT (fix, bridge on, hooked draws OFF; no crash): copy-control samples now read
  00000004 x1, 00100000 x3, 00100340 x2 (clear colour 1 depth 1) - clears are requested and
  captured. The replay-alone frame is now a RECOGNISABLE SCENE (shots/..._CLEAR1_...png): lake,
  arches, island and shrine, far hills, trees, sky and clouds. The loading banner and the trails
  are GONE. Both pre-registered predictions MET. The user, watching live: "Looking much better,
  just missing the ground, sky texture, characters and water textures and animation".
- OPEN on the replay path now (each its own row): near ground still black; sky/water/character
  textures missing or flat; hero dark; blocky impostor patches in the distant trees; animation.
  Row (e) as "the replay presents no scene" is CLOSED by the clear fix.
- ROW (a) RESUMED ON THE WORKING REPLAY. PRE-REGISTERED for TESSFLAT2 (bridge, hooked draws OFF,
  ngpu_tess_flat=true): magenta covers the near band => the patches are there and the black is their
  material/depth; no magenta there => the patch geometry never reaches the near band.
- TESSFLAT2 RESULT: near band (bottom 22%) magenta 0.1%, black 0.0% - by the pre-registered
  metric, branch 2 (patch footprint does not cover the near band). BUT the band is no longer black:
  it shows the lake water and a sunlit lake bed (orange caustics), a pale flat plane at left, and
  magenta only around the fern clumps and the far bank (shots/..._TESSFLAT2_...png). The ONLY
  difference from CLEAR1 is how patches draw - so in CLEAR1 the black band was painted BY the
  patch draws. Not yet reconciled with the small flat-mode footprint.
- PRE-REGISTERED for TESSOFF2 (bridge, hooked OFF, ngpu_tess=false): band shows water/lake bed =>
  the CLEAR1 black is painted by the terrain patches (present, shading black); band black again =>
  something else paints it.
- TESSOFF2 (tess off, replay alone): near band 0.0% black - the WATER SURFACE fills the terrain's
  place (the USER, watching: "water animation is over the terrain area, not the actual lake"). So
  in CLEAR1 the terrain patches ARE drawn in front of the water and come out BLACK; the flat
  diagnostic pipeline does not place them the same way (TESSFLAT2's small footprint).
- Terrain PS under replay ("TESSELLATION shaders", CLEAR1): 4DF4ADDB:4800 52FE118F:29400
  5FEF6617:22920 81B999B2:30120. 4DF4ADDB is Bowerstone's "near pieces that paint black" (2026-09-22)
  - the same defect, now reproduced on the working replay.
- PRE-REGISTERED for WHITE2 (replay alone, ngpu_tex_slots=0 only for PS 4DF4ADDB): near ground turns
  white/bright => its colour comes from textures that arrive black; stays black => constants/lighting.
- WHITE2 RESULT: override echoed; 3,221,680 texture slots served WHITE on PS 4DF4ADDB's draws (the
  instrument matched) - yet the near band stayed 99.3% black and the frame is visually unchanged
  (shots on disk). So either 4DF4ADDB's output does not depend on its textures, or 4DF4ADDB is not
  what paints the near band. The USER, watching: "far away terrain geometry is starting to look
  correct, terrain close to the character is still black".
- PRE-REGISTERED for DROP2 (replay alone, ngpu_drop_ps = 4DF4ADDB): band turns to water =>
  4DF4ADDB paints it (black from constants/lighting); stays black => another shader paints it.
- DROP2 RESULT: dropping PS 4DF4ADDB leaves the near band 99.3% black - 4DF4ADDB does NOT paint it.
  The USER: "it just looks like missing geometry and texture for terrain, no amount of light will fix
  that" - the lighting reading is dropped. Since TESSOFF2 (tess off) shows water there, one of the
  OTHER tessellated PS paints the black: 52FE118F / 5FEF6617 / 81B999B2. Candidate story: the
  terrain's depth-only PRE-PASS writing (black) colour, or the textured colour pass never landing.
- PRE-REGISTERED for DROP3a/b/c (replay alone, drop each of the three in turn): the PS whose drop
  turns the band to water is the one painting the black.
- DROP3 RESULTS (replay alone, near band % black; CLEAR1 99.3%):
    drop 52FE118F 99.3% | drop 5FEF6617 99.3% | drop 81B999B2 69.0%
  81B999B2 is the TERRAIN COLOUR PASS: the USER saw the far terrain texture disappear when it was
  dropped, and it paints part of the near band BLACK. Under it (shots/2026-09-24_bowerlake_DROP3c.png)
  water shows through, and BLACK STAIR-STEPPED CHUNK SHAPES remain - other terrain draws, also black
  near the camera. So several terrain passes are black NEAR and correct FAR: a near-only input.
- PRE-REGISTERED for WHITE3 (replay alone, whiten 81B999B2's slots): its near black turns white =>
  the black comes from a texture (declined GPU-written texture / empty resolve); stays black => not
  its textures.
- WHITE3 RESULT: VOID AS A TEXTURE TEST - 20,249,740 slots whitened on 81B999B2, and its draws
  VANISHED (frame identical to DROP3c, band 69.0%) instead of turning white: the instrument changed
  the draw's behaviour. Informative anyway: 81B999B2 presumably alpha-tests on a texture that the
  placeholder fails - consistent with the 2026-09-22 Bowerstone finding that the base terrain is
  ALPHA-TESTED AWAY WITHIN ~48 m BY DESIGN (stipple alpha) and the near ground is painted by the
  NEAR PIECES instead.
- ROW (a) NOW POINTS AT THE NEAR PIECES: the black stair-stepped chunk shapes left when 81B999B2 is
  dropped (DROP3c) are the near terrain pieces, and they render black. Next: name the draw(s) behind
  those chunks (drop the remaining tessellated VS/PS pairs with 81B999B2 also dropped, or the
  per-draw dump), then look at what they sample - the Bowerstone record says the near pieces'
  colour comes from MULTIPLY layers (blend src x DST_COLOR) over a base, so a black base or a
  black/zero multiply layer would give exactly this.
- PRE-REGISTERED for DROP4a/b/c (replay alone, drop 81B999B2 AND one of 4DF4ADDB / 52FE118F /
  5FEF6617 via ngpu_drop_ps2): the pair whose drop removes the black chunks (band well below 69%)
  names the near pieces.
- DROP4 RESULTS (band % black): +4DF4ADDB 69.0 | +52FE118F 69.0 | +5FEF6617 0.0. So PS 5FEF6617 draws
  the BLACK NEAR-TERRAIN CHUNKS (the near pieces). Pairing by exact draw counts (TESSELLATION
  shaders line): VS 0EF7C66A:22,920 <-> PS 5FEF6617:22,920; VS 6A56B867:30,120 <-> PS 81B999B2:30,120
  (the colour pass); VS FFDBABD9:34,200 <-> PS 52FE118F:29,400 + 4DF4ADDB:4,800.
- SLOTS1 (ngpu_slot_report_ps = 5FEF6617, replay alone): every slot binds a REAL texture - no
  placeholder, no decline, none flagged as a resolve destination:
    0 DXT1 1024^2 (or 512^2) | 1 DXT1 1024^2 | 2 DXN 1024^2 (normal) | 3 DXT1 768^2 (splat?) |
    4 fmt29 64x1 | 5 fmt6 256x1 (LUTs) | 6 fmt23 (24_8_FLOAT) 1024^2 @12C03000 |
    7 fmt23 1024^2 @13003000 | 8 fmt2 (8-bit) 1280x720 @12B1D000 | 9,11 fmt6 1x1 | 10 fmt6 32x32
  Slots 6/7 look like SHADOW MAPS (depth format, 1024^2) and slot 8 a screen-space shadow/light mask
  (1280x720, 8-bit) - GPU-written targets, uploaded from GUEST MEMORY because they are not in the
  resolve registry ("WHERE THE DEPTH RESOLVES GO: NONE - no depth resolve landed anywhere this run").
  LEAD: those read as zeros => fully shadowed => BLACK near ground; shadow maps cover only the area
  near the camera, so the far terrain (unshadowed) looks right - matching the user's reading that the
  geometry is fine and the ground is "missing texture".
- PRE-REGISTERED for SHADOWWHITE1 (replay alone, 5FEF6617 only, slots 6/7/8 WHITE, material slots
  untouched): the near ground turns textured/lit => the black is the unresolved shadow/light inputs.
- SHADOWWHITE1 RESULT: 2,311,821 slots whitened (matched), band still 99.3% black - BUT CONFOUNDED:
  81B999B2 paints the TOP layer over the band (DROP3c), so lighting 5FEF6617 underneath cannot show.
  Not yet a refutation.
- PRE-REGISTERED for SHADOWWHITE2 (replay alone, drop 81B999B2, whiten 5FEF6617 slots 6/7/8): the
  black chunks (69% of the band in DROP3c) turn lit => the black is slots 6/7/8; stay 69% black =>
  the shadow/light-input lead is DEAD for 5FEF6617.
- SHADOWWHITE2 RESULT: 2,306,655 slots whitened, chunks still 69.0% black - the shadow/light-input
  lead is DEAD for 5FEF6617.
- NEW HYPOTHESIS (from the 2026-09-22 Bowerstone record: the near terrain's detail draws are
  MULTIPLY layers, blend src x DST_COLOR, over a base; the far base is alpha-tested away near the
  camera by design): 5FEF6617 multiplies onto a target whose near BASE never drew -> black x anything
  = black. PREDICTION for WHITEALL5F (replay alone, drop 81B999B2, whiten EVERY slot of 5FEF6617):
  chunks STAY black (multiply over black); if they turn white, the multiply hypothesis is dead.
- WHITEALL5F RESULT: VOID AS A COLOUR TEST - with every slot of 5FEF6617 white (81B999B2 dropped) the
  chunks VANISH (band 0.0% black, water visible), exactly as 81B999B2 vanished in WHITE3. Whitening
  slots 6/7/8 alone does NOT make them vanish (SHADOWWHITE2). So a slot among 0-5 / 9-11 GATES
  SURVIVAL (a texture-driven discard / alpha test). Whitening therefore cannot read these shaders'
  colour; the multiply-over-missing-base hypothesis is neither confirmed nor refuted by it.
- NEXT for row (a): find the gating slot (whiten one slot at a time), then read what the shader does
  with it; separately test the multiply hypothesis from STATE, not colour - log 5FEF6617's blend
  factors / colour mask / alpha test at its replayed draws.
- PRE-REGISTERED batch (claudecode-76's two gaps first):
  * DROP5F (drop 5FEF6617 alone): ~30% black => the two layers are INDEPENDENT contributors;
    ~99% => the chunks are UNDERNEATH 81B999B2 and matter only once it is gone.
  * WHITECTL (whiten EVERY slot of the healthy PS 9A193FEB - arches/rocks/hero): its draws SURVIVE
    (turn white) => the vanishing is a real texture gate specific to the terrain shaders; they VANISH
    => the whitening path itself eats draws and WHITE3/WHITEALL5F's "gate" is NO MEASUREMENT.
  * STATE5F / STATE81 (slot report + blend/alpha/mask/depth state for 5FEF6617 / 81B999B2): read the
    blend factors - a src x DST_COLOR multiply on 5FEF6617 supports "multiply over a missing base".
- BATCH RESULTS (pre-registered above):
  * DROP5F (drop 5FEF6617 alone): 99.3% - the layers are STACKED, not independent: 81B999B2 lies
    over the near pieces, which only show once it is gone.
  * WHITECTL (whiten every slot of the healthy PS 9A193FEB): its draws turn WHITE (arches, rocks, much
    of the lake surface) - they SURVIVE. The whitening instrument does not eat draws; the vanishing of
    81B999B2 / 5FEF6617 under whitening is real and specific to them.
  * STATE (replayed registers at those draws):
      81B999B2  RB_BLENDCONTROL0 00060006 = src x SRC_ALPHA + dst x ZERO  (REPLACES dst, alpha-weighted)
      5FEF6617  RB_BLENDCONTROL0 01060106 = src x SRC_ALPHA + dst x ONE   (ADDITIVE)
      both: RB_COLORCONTROL 00000004 (alpha func GREATER, TEST OFF), ref 0.502, depth GEQUAL + write,
      colour mask F. Blend IS applied (ngpu_state_mask default 31).
  * The multiply hypothesis is WRONG (5FEF6617 is additive, not multiply).
- READING (consistent with every number today, not yet shown): the near pieces are an ADDITIVE layer;
  drawn onto a target cleared to black with no base under them they add ~nothing and write depth, so
  the water drawn later fails the depth test - black. The colour pass replaces dst with colour x alpha
  and its alpha is ~0 near the camera (the base terrain fades out near by design) - black too. What is
  missing is the near ground's BASE colour layer (the USER: "missing texture").
- NEXT: find the near base on the PLUGIN path (the oracle, which renders the ground correctly): which
  draw writes the near ground's colour before the additive pieces, and whether the replay drops it
  (the ~68k refused point/rect lists, the 29k range refusals, the emitter draws) or draws it wrong.
- PRE-REGISTERED for BASE1 (replay alone + ngpu_replay_autoindex=true; every leg since the clear fix
  ran with it OFF, so ~400 auto-index tri-list/strip draws/frame were never drawn): band black drops
  and ground colour appears => the near base is an auto-index draw; stays ~99% => it is elsewhere
  (point/rect lists, emitter draws, range refusals).
- BASE1 RESULT: 99.3% - the near base is NOT an auto-index draw.
- REVISED READING (not shown): the terrain CROSS-FADES by distance. The far colour pass (81B999B2,
  src x SRC_ALPHA replacing dst) fades OUT near the camera (alpha ~0 -> black, as measured); the near
  pieces (5FEF6617, src x SRC_ALPHA + dst, additive) should fade IN there (alpha ~1), adding the ground
  colour onto black. Natively they add nothing - and with EVERY texture white they vanish rather than
  turn white, so their alpha does not come from a texture. Likeliest source: the VERTEX shader (VS
  0EF7C66A, run through the tessellation EMULATION) computing a distance fade from the camera position
  or the emulated domain coordinates, which comes out 0 here.
- NEXT: read VS 0EF7C66A's output that feeds alpha (its translated HLSL / the emulated variant) and the
  constants it uses; test by forcing that output to 1 for 5FEF6617's draws.
- CORRECTION: the earlier slot report printed at most 12 lines per 600 frames and slots 0-11 used all
  12 - so it HID slots 16-18 (my instrument's cap). The raw report (VSSLOTS1) shows the VS sampler
  slots ARE bound textures at 5FEF6617's draws:
    16 fc1 1B597058 -> fmt 24 (k_16) 769x769  = the terrain HEIGHTMAP
    17 fc1 1B6D1071 -> fmt 49 (DXN)  769x769  = its normals
    18 fc1 1B82907A -> fmt 58 (DXT3A, alpha-only) - the VS reads its .w (tq variant line ~162) as a mask
  DXT3A has special native handling (ngpu_dxt3a swizzles every channel to R); if the alpha does not end
  up where .w reads it, the mask is 0 and the additive near pieces fade to nothing.
- PRE-REGISTERED for MASK1 (replay alone, 5FEF6617 only, slot 18 WHITE): near ground gets colour =>
  the DXT3A mask decode is the culprit; no change => it is not.
- MASK1 RESULT: 769,652 slots whitened, band 99.3% - CONFOUNDED by the 81B999B2 top layer again (the
  same flaw as SHADOWWHITE1; my setup error). MASK2 re-runs it with 81B999B2 dropped; baseline DROP3c
  69.0% black (the near chunks).
- MASK2 RESULT (81B999B2 dropped, 5FEF6617 slot 18 WHITE): band 99.3% black vs DROP3c's 69.0% - the
  near pieces now cover MORE of the screen, and are still black (shots/..._MASK2.png; the right-edge
  shore now shows terrain texture). So the DXT3A mask (slot 18 .w) controls WHERE the near pieces
  exist (coverage/extent), NOT their brightness: the mask-as-fade hypothesis is REFUTED.
- STATE OF ROW (a): drawn in full, the near pieces write depth and add ~no colour (the water drawn later
  fails the depth test -> black). The fault is PS 5FEF6617's OUTPUT: colour x alpha ~0 with real
  textures, and with every texture white it DISCARDS (a texture-driven clip in the shader). Next:
  translate 5FEF6617 offline (tools/native_gpu/xr_translate.py on its container) and read what it
  outputs and what it clips on.
- CORRECTION (my error, twice): g_ring.regs is NOT a dead parser shadow during replay - RingRegsShadow()
  RETURNS g_ring.regs, and the replay writes its recorded register deltas INTO it (RingSetReg). So:
  * the clear fix (2a4ead2) is still right, but its stated cause was wrong: RB_COPY_CONTROL read 0
    because 0x2318 is NEVER RECORDED by the bridge (not in kBridgeLogRanges), not because the replay
    read a dead shadow. Capturing it at the resolve marker remains the fix.
  * the PS constant upload (g_ring.regs[0x4400+i]) reads the same live replay state as the VS upload.
- PS 5FEF6617 DISASSEMBLED (dxc -dumpbin on its cached DXIL): colour out = c20.w x (textured colour);
  alpha out = (1 - saturate(c31.x * v + c31.y)) x an input alpha; discard only if the emulated alpha
  test flag (shared constants reg 33 .y & 2) is set and alpha < ref. So its colour is scaled by ONE
  PS constant, c20.w - black whatever the textures if c20.w == 0 (consistent with every result today).
  Next: measure c20 and c31 at its replayed draws.
- PSCONST1 (c20/c31 at 5FEF6617's replayed draws): c20 = (0.063, 0.034, 0.019, 7.3..16) - c20.w is a
  healthy HDR scale, NOT zero; c31 = (0.000558, -1.286) - the fade term is ~1 near the camera. The
  "c20.w == 0" hypothesis is REFUTED. So 5FEF6617 multiplies its TEXTURED colour by ~7-16 and still
  outputs black: the textured colour its code computes is ~0 although every input texture is real
  (SLOTS1/VSSLOTS1). Its code comes from XenosRecomp (the unverified translator; the plugin, which
  renders the ground correctly, uses the SDK's DXBC).
- ROW (a) LANDS ON M5: the discriminator is rendering 5FEF6617's draws through the SDK's DXBC (verified
  406/406 against the plugin). If the ground appears, the defect is XenosRecomp's translation of this
  shader; if not, it is in what the replay feeds it.
  Dead today (all with pre-registered verdicts): shadow/light slots, the DXT3A mask as fade (it gates
  coverage), c20.w == 0, the multiply-over-base story (5FEF6617 is additive), auto-index draws,
  4DF4ADDB / 52FE118F as painters.
- ROW (f) LABEL: "the plugin's DXBC vfetch bounds-check returns zero past the declared 96 B" is a CODE
  READING (dxbc_translator_fetch.cpp, vendored 0cb9040 - as the 2026-09-22 night record already says:
  "verified by reading the vendored source, not by running it"), NOT a measurement. The parity argument
  (both paths show nothing for these draws) rests on that reading; the LOW PRIORITY stands.
- BLAST RADIUS OF THE MISSING CLEAR (claudecode-76's question - answer (ii), MEASURED by CLEAR1): the
  picture CHANGED completely once targets cleared (flat blocks -> a recognisable scene). So every
  PICTURE verdict taken before 2a4ead2 was read off an ACCUMULATING target and is VOID as a picture
  verdict: BRIDGE3/4, AUTOIDX1, TILEBAND1, TILESC1/2, TESSOFF1, PROBEB1's frame, the BIN legs
  (including the "Loading Bower Lake" banner) and REPLAYONLY1. "The replay presents no scene" was
  itself an accumulation artifact, and is closed by the fix (recorded at CLEAR1).
  UNAFFECTED - they rest on COUNTS, not pictures: the emitter census and ~855 denominator, the drop-site
  census, auto-index and range-refusal counts, the fetch-constant dumps, row (f), bridge arrivals and
  tessellation counters.
  ROW (a)'s post-fix work (CLEAR1 onward: TESSFLAT2, TESSOFF2, WHITE2/3, DROP2/3/4, SLOTS1, VSSLOTS1,
  MASK1/2, SHADOWWHITE1/2, WHITEALL5F, WHITECTL, BASE1, PSCONST1) was ALL taken on the clearing build,
  and the drop ladder went to 0.0% black with both painters dropped - an all-off-goes-blank check in
  that band. Those stand.
- ROWS (b) white sky/water patches, (c) reflections, (d) canopy/fern colours are STRUCK: read off
  SIGN1's frame, which was both pre-bridge (no tessellation, by architecture) and pre-clear (an
  accumulation). They described a machine that no longer exists.
- FRESH DEFECT LIST on the current build (CLEAR1: native replay alone vs the PLUGIN window at the SAME
  moment - shots/2026-09-24_bowerlake_CLEAR1_native_vs_plugin.png). CORRECT natively: the arches, the
  island and shrine, the far hills, the lake's shape. WRONG:
    1. near ground BLACK (plugin: grass, dirt path, ferns)                      - row (a)
    2. the HERO a dark silhouette (plugin: lit red coat)
    3. near FERNS dark red/black (plugin: lit orange-brown)
    4. distant trees as blocky grey/white squares (impostor cards) (plugin: canopies)
    5. WATER dull opaque teal (plugin: bright blue, reflective, specular)
    6. overall lighting flatter/darker, no haze/bloom
    7. no HUD (probably expected at this stage)
  Items 1-3 share a pattern - EVERYTHING NEAR THE CAMERA IS DARK, not just the terrain - which suggests a
  COMMON near-camera cause (near lighting/shadow inputs, or a shared lighting shader path) alongside or
  instead of one shader's translation. M5 tests 5FEF6617's translation; it may not be the whole answer.
- DISTANCE-TERM HYPOTHESIS (claudecode-76: an inverted fog/haze factor darkens NEAR and leaves FAR unhazed)
  TESTED on CLEAR1's native frame, no leg: near ground = EXACTLY (0,0,0), max channel 0, at every sampled
  point; hero mean (33,10,6) with parts up to 183 (dim, partly lit); ferns (12,0,0) dark red - NOT drawn
  toward one shared colour; a vertical profile is a HARD STEP (0 from the bottom of the screen to the
  ground's far edge, then water), not a gradient. REFUTED. The ground is a ZERO output (consistent with
  5FEF6617's textured colour ~0); the hero and ferns are dark but a different symptom, possibly related.
- LIST LABELS: item 4 (blocky distant trees) is most likely the DELIBERATE ngpu_skip_impostor skip
  (31/frame ortho impostor RENDERS not drawn, so the cards sample an impostor atlas never painted) - a
  known intentional gap, not a new defect (unconfirmed: re-take with ngpu_skip_impostor=false). Item 7
  (no HUD) is a subsystem NOT YET PORTED, not a rendering fault.
- ROWS, as of 2026-09-24 evening (each its own mechanism until shown otherwise):
    (a) near ground: EXACTLY ZERO output (not dimmed) - PS 5FEF6617's textured colour ~0 from
        XenosRecomp's code; direct test = M5 (render it through the SDK DXBC).
    (g) HERO: dim but partly lit - mean (33,10,6), lit parts to 183 - a partial lighting/shadow-term
        symptom, different from a zero write.
    (h) near FERNS: (12,0,0), nearly zero and red-biased - different again.
    (4) distant trees: likely the deliberate ngpu_skip_impostor gap (UNCONFIRMED).
    (5) water dull, (6) flat lighting / no haze, (7) no HUD (not yet ported).
  DEAD hypotheses for (a), all tested: flat-sheet heightmap collapse, index-buffer-as-stream, stale
  load-time recording, hidden UI, shadow/light slots, DXT3A mask as fade, c20.w == 0, multiply over a
  missing base, auto-index base, and the distance/fog term (the near/far split was composition, not
  mechanism).
- BRANCH CHECK (the user's go-ahead): 5FEF6617's colour branch tests a COMPUTED float (%144 > 0), NOT a
  boolean constant - the boolean hypothesis does not apply to it. Its base colour = (diffuse sample)^2
  (gamma->linear) x a lighting sum (c11.x * term + term), the diffuse sampled at interpolant row 1.
- CORRECTION to WHITE3 / WHITEALL5F: "whitening every slot" included the VS's HEIGHTMAP (slot 16) - a
  white heightmap displaces every vertex to maximum height, moving the geometry out of view. The
  "vanish" is DISPLACEMENT, not a texture gate. Both remain void as colour tests, for this reason.
- PRE-REGISTERED for PSWHITE1 (replay alone, 81B999B2 dropped, 5FEF6617's PIXEL slots 0-11 white, VS
  slots 16-18 REAL): chunks turn bright => the shader's maths works and the real texture CONTENTS are
  what is zero (present but empty/stale - the user's streaming question); stay black => the lighting
  term or the translated maths zeroes the colour.
- PSWHITE1 RESULT: 13,054,810 PIXEL slots white (VS slots real; override echoed), chunks STAY 69.0% black,
  geometry unchanged. With diffuse = white, (diffuse)^2 = 1, so output = lighting sum x c20.w (7-16): the
  LIGHTING SUM IS ZERO. Textures and their contents are RULED OUT (so not stale/streamed texture data).
  The zero comes from the lighting terms - built from VS interpolants and PS constants c11/c19/c21/c22/c67.
- LIGHTC1: every lighting constant is sane - c11 1,1,1,1 | c19 (162,136,31,1) | c21 (1.05,0.79,0.42,1.3)
  | c22 (0.07,0.07,0,0) | c67 (17,1900,1,0.0005). Constants RULED OUT.
- THE MECHANISM (row (a)), found by reading signatures, not yet tested by a fix:
  * PS 5FEF6617's input signature reads TEXCOORD0-6 and COLOR0.w (register 17) = %4, the ALPHA
    multiplier: alpha out = (1 - fog) x COLOR0.w; blend src x SRC_ALPHA + dst.
  * VS 0EF7C66A (both _v and the tessellated _q XenosRecomp HLSL) DECLARES oColor0/oColor1, sets them to
    0.0 at the top, and NEVER WRITES THEM AGAIN - while it DOES write oTexCoord7 (all four components),
    which the PS never reads.
  => an INTERPOLATOR LINKAGE MISMATCH in XenosRecomp's translation: the VS's 8th export lands in
     TEXCOORD7 while the PS expects it as COLOR0. Alpha arrives as 0 -> the additive near pieces add
     NOTHING while writing depth -> the ground is EXACTLY black, geometry right, white textures change
     nothing. Exactly what M5 (the SDK translator's consistent linkage) would fix; the hero and ferns may
     be the same class.
- NEXT: direct test - wire COLOR0 = TEXCOORD7 in 0EF7C66A's variants and see whether the ground appears.
- LINKFIX TEST, pre-registered: in the tessellated VS variant 0EF7C66A..._q ONLY (its on-disk HLSL
  recompiles to EXACTLY the cached DXIL 9448106ad568 - provenance verified; the _v variant does NOT
  reproduce, XenosRecomp non-determinism, so it is left alone), add one line `oColor0 = oTexCoord7;`,
  recompile with the JIT's exact dxc flags, install into ngpu_cache (original backed up). PREDICTION:
  the near ground gets colour (band black falls substantially) => the linkage mismatch is the cause.
- LINKFIX1 RESULT - ROW (a)'s CAUSE CONFIRMED: with `oColor0 = oTexCoord7;` added to the tessellated VS
  0EF7C66A..._q (patched DXIL 6af084f50785, replay alone, one change), the near band goes 99.3% -> 43.0%
  black and the DIRT PATH appears - brown, following the plugin's path shape - plus a green patch by the
  shore (shots/2026-09-24_bowerlake_LINKFIX1_path_appears.png; patched HLSL kept as
  docs/native_gpu/LINKFIX_0EF7C66A_q.hlsl). The pre-registered prediction is MET: XenosRecomp's
  interpolator LINKAGE MISMATCH (the VS's 8th export in TEXCOORD7, the PS reading it as COLOR0) is what
  zeroed the near pieces' alpha. The cache was RESTORED to 9448106ad568 after the leg - this was a test,
  not a fix.
- The remaining black (mostly grass areas) is presumably other near shaders with the same class of
  mismatch. THE FIX IS THE CLASS, not this shader: link VS exports to PS inputs by export index in the
  XenosRecomp path (FixHlsl / the translation step), or move the native path onto the SDK's DXBC (M5),
  whose linkage is consistent. Next: census every VS/PS pair the replay draws for a PS input the VS
  never writes (COLOR0/1 initialised-only) - that names all members of the class at once.
- PER-SHADER OR PER-COMPILE? (claudecode-76's question, answered before any census): XenosRecomp run 6x on
  0EF7C66A's _q container with the JIT's exact arguments -> 6/6 byte-identical HLSL (966a45f04ef1); in
  every run oColor0 is written ONLY by its 0.0 initialiser and the 8th export goes to oTexCoord7. The
  mismatch is a stable property of this shader's TRANSLATION, present in XenosRecomp's RAW output (before
  FixHlsl). Scope: one shader, six runs - _v's non-reproduction shows other shaders CAN vary, so every
  census member gets the same N-run check before it counts.
- LINKAGE CENSUS (tools/native_gpu/linkage_census.py over the 77 VS/PS pairs PAIRS1 drew; full list
  docs/native_gpu/LINKAGE_CENSUS_2026-09-24.txt, pairs PAIRS_bowerlake_2026-09-24.txt): 77 checked,
  0 unchecked, 30 MEMBERS (the PS uses a component its VS never really writes).
  * VALIDATION: the known subject 0EF7C66A/5FEF6617 IS flagged (COLOR0.w - the LINKFIX1 case); the
    healthy control BCC6E2DE/9A193FEB (arches, rocks - render correctly) is NOT flagged.
  * Largest: 6A56B867/81B999B2 (the terrain COLOUR PASS, 950k draws) - COLOR0.w, the same defect: why the
    colour pass goes black near the camera. Many world pairs missing TEXCOORD0.xy / TEXCOORD1.xyz, several
    missing COLOR0.xyzw (vertex colour - foliage/characters/effects candidates).
  * CAUTION: "never really written" = every store is a literal constant; a VS that legitimately outputs a
    constant would be a FALSE POSITIVE. Members need spot-checks, and the N-run determinism check (the
    near-piece VS was 6/6 stable; _v variants can differ).
- The fix belongs in the LINKAGE: map VS exports to PS inputs by the Xenos interpolator register, not by
  XenosRecomp's per-shader semantic assignment - or move to the SDK DXBC (M5), which links consistently.
- LINKFIX1's 43% RESIDUAL IS ACCOUNTED FOR, not presumed: the terrain colour pass 6A56B867/81B999B2 is a
  census member with the SAME COLOR0.w defect (its alpha also arrives as 0 - why it goes black near).
- The USER CHOSE M5 ("2", after the census): move the native draw path onto the SDK's DXBC, whose VS->PS
  linkage is consistent by construction - the class fix. Open before M5 lands, as a pre-registered
  expectation: whether the HERO's and the FERNS' pairs are census members (needs the probe to name their
  shaders). If they are, M5 is expected to repair items 1-3 of the fresh defect list together.
- Cheap false-positive resolution for the census (claudecode-76): compare each member VS against the SDK's
  DXBC translation of the same microcode - a real varying there = genuine member; a constant in both =
  false positive. M5 makes this moot for the draw path but it keeps the census honest.
- M5 NON-REGRESSION LIST (claudecode-76; PRE-REGISTERED before any M5 code). M5 swaps the translator, so it
  changes many things at once and a one-thing control cannot protect it. What was ALREADY CORRECT natively
  in CLEAR1 (shots/2026-09-24_bowerlake_CLEAR1_native_vs_plugin.png) must STILL be correct after M5, at
  the same stand and moment, judged against the plugin window:
    a. the arches            b. the island and shrine       c. the far hills       d. the lake's shape
    e. PS 9A193FEB's draws specifically (the healthy control, NOT a census member): still present and
       lit as in CLEAR1 (WHITECTL showed which pixels are its: arches, rocks).
  Any of a-e regressing = M5 is NOT a success even if items 1-3 repair; record it as partial, name the item.
- M5 SIZE WILL BE A NUMBER, not an estimate: each contract element (root signature, system constants,
  float/bool/loop/fetch constants, vfetch via shared memory, texture/sampler model, interpolators,
  memexport, tessellation, RT model, per-draw register state) scored MATCHES / DIFFERS / ABSENT against
  the native path, and the DIFFERS+ABSENT count reported to the user BEFORE the hours are spent. Guard
  against a HALF-MIGRATION: no draw may use SDK DXBC while any of its bindings still follow the old
  XenosRecomp contract - per pair, all-SDK or all-old, and which one is logged.
- M5 CONTRACT MAP, SCORED (SDK side mapped from branch_src; native side from native_gpu_present.cpp).
  16 elements: MATCHES 1 (RT model: RTVs, blend/format in the PSO). DIFFERS 9: root signature
  (b0-b4 space0 + shared-memory table + unbounded views in spaces 1-3, vs native b0-b2 space4 + 5 sets);
  float constants (packed by each shader's used-bitmap); bool/loop (b2 raw 40 dwords); fetch constants
  (b3 raw); index handling (SV_VertexID + in-shader endian swap); texture model (2D as Texture2DArray,
  signed+unsigned SRV pair, guest swizzle in the SRV mapping); samplers (keyed per fetch constant);
  interpolators (VS+PS translated as a PAIR with a shared mask - native caches per shader);
  tessellation (SDK = real HS/DS + built-in VS/HS; Plume has no HS/DS stage -> raw D3D12 PSOs).
  ABSENT 6: system constants b0 (the whole xe_* struct from registers); descriptor indices b4;
  vertex fetch from a 512 MB shared-memory mirror of guest physical memory (native uses IA layouts);
  memexport UAV; built-in GSs for point/rect/quad lists; per-draw modification bits.
  => 15 of 16 elements need work. M5 is DAYS, not "several hours". Reported to the user before spending it.
- WHILE SCORING, THE CLASS CAUSE WAS FOUND IN XENOSRECOMP ITSELF: it links VS->PS by the USAGE each
  container DECLARES (VS: export reg -> o<usage><index> from its table; PS: r<reg> = i<usage><index>).
  Fable II's tables are per-shader compiler allocations (VS BCC6E2DE: reg6=COLOR0; 1AFFC557: reg3=COLOR0;
  6B210267: reg7=COLOR0; 0EF7C66A: identity TEXCOORD0-15; PS 3B242060: reg1=COLOR0; A9C388A8: reg8=COLOR0),
  so the names do not agree across a pair. The Xenos hardware - and the SDK translator - link BY REGISTER.
  LINKFIX1 (`oColor0 = oTexCoord7`) WAS register linkage for one pair.
  FIX (XenosRecomp working tree, local): both sides name interpolator register N TEXCOORD<N>; the VS
  export map is keyed by the register (was the table position - equal in every container seen, so not a
  second bug); each container's declared table kept as `// link:` comments. Offline: 6 of 24 on-disk PS
  containers change linkage; the drawn-pair check has only 1 pair with both containers on disk (76 not),
  so OFFLINE CANNOT SETTLE IT - the leg decides.
- LINKREG1, PRE-REGISTERED (bridge on, hooked draws off, Bower Lake stand; ngpu_cache and ngpu_jit MOVED
  aside to *_prelink_20260924 so every shader re-translates with the fixed XenosRecomp; one warm-up leg
  LINKREG0 first so the JIT finishes, discarded):
    FIX CRITERION: near band black falls well below CLEAR1's 99.3%, at least to LINKFIX1's 43%, and the
      linkage census on the new cache reports far fewer than 30 members (0 expected for the declared-name
      class; survivors = a different class, listed).
    NON-REGRESSION (a-e above): arches, island/shrine, far hills, lake shape, PS 9A193FEB's draws.
      9A193FEB is the sharpest test: its VS BCC6E2DE declares COLOR0 at reg 6, so if register linkage
      were WRONG this is the pair it would break.
    Anything in a-e regressing = register linkage is not the rule; revert (cache restored from the move).
- LINKREG1 RESULT - ROW (a) FIXED BY REGISTER LINKAGE (XenosRecomp 95a268c, local). Same binary, same stand,
  bridge on, hooked draws off; the ONLY difference between the arms is the shader cache:
    LINKREG1  (cache translated by the fixed XenosRecomp): near band black 0.2%
    LINKREG1C (control: the pre-fix cache restored byte-identical, ngpu_jit=false): 99.3% (= CLEAR1)
  Picture (shots/2026-09-24_bowerlake_LINKREG1_native_vs_plugin.png, ..._LINKREG1C_control_top_fix_bottom.png):
  the near ground is GRASS AND DIRT PATHS in the plugin's shapes. NON-REGRESSION a-e ALL HOLD: arches,
  island/shrine, far hills, lake shape, and 9A193FEB's arches/rocks - present and lit as in CLEAR1.
  Noise floor: the band read 0.2% in all six fixed-cache legs (LINKREG0b-0f, LINKREG1), 99.3% in the control.
  Declared caveats: (1) two PS fail to translate with the fixed exe in every leg (D32D70C8 / 7A869D11,
  CFB18804 - XenosRecomp failures, not linkage), so those draws are absent in the fix arm and present
  (old cache) in the control. (2) JIT was off in the control and did 0 successful translations in the
  fix arm, so it is not a second live difference.
  ALSO LEARNED: Fable's container interpolator tables are NOT stable - the new 5FEF6617 container declares
  identity TEXCOORD0-15 where the cached one declared COLOR0 (the code already notes "the engine writes
  into them"). Name-based linkage made the translation depend on WHEN the container was read; register
  linkage does not. The prelink cache also needed its .code aliases to keep shader NAMES stable: moving
  the cache without them re-named every shader (0EF7C66A appeared as 21E6C040) - restored before LINKREG1.
- LINKAGE CENSUS ON THE FIXED CACHE (docs/native_gpu/LINKAGE_CENSUS_LINKREG1.txt): 84 checked, 1
  unchecked, 51 flagged - MORE than the 30 before, and NOT the same defect. The declared-name class is 0
  BY CONSTRUCTION (both sides are named by register). The 51 are PS reads of a register the VS never
  exports (e.g. 9A193FEB reads TEXCOORD7.xy, its VS BCC6E2DE exports regs 0-6): 0 in both old and new
  translations (every undeclared output is zero-initialised), triggered because the new PS tables list
  all 16 registers. The census as written cannot tell the two apart; to be split before it is used again.
- STILL WRONG after LINKREG1 (fresh list against the plugin window): hero dark; ferns dark red/black
  silhouettes (now VISIBLE because the ground behind them is no longer black); distant trees blocky
  impostor squares; water dull teal; a black wedge on the right shore (present in the control too - not
  a regression); overall lighting flatter; no HUD. Items 2-3 were pre-registered as M5's expected wins;
  register linkage did NOT fix them, so they are a different cause.
- M5 STATUS: the reason it was chosen (the linkage class) is now fixed at the translator in two lines.
  M5 as scored (15 of 16 contract elements) is a days-long migration; its remaining case must be
  re-argued from the defects that are left, and that is the user's call.
- THE FIX-ARM TRANSLATION FAILURES, each given a verdict (claudecode-76: a scene-feature guard cannot see
  a shader that fails to translate):
    D32D70C8 (and its fresh-name twin 7A869D11): NOT caused by register linkage - the old and fixed
      XenosRecomp emit IDENTICAL HLSL for it (only the // link: comments differ), and both fail dxc on
      `oDepth` undeclared: the microcode exports depth while the container declares no depth output (the
      same metadata-vs-microcode disagreement as the linkage). The prelink cache held a copy from
      2026-09-16, so the failure appeared only because the fix arm re-translated. FIXED (XenosRecomp
      commit below): the export lands in a local, following the declaration. Translated in LINKREG2;
      LINKREG3 (with it) band 0.2%; picture change vs LINKREG1 (without it) mean |d| 3.23, 1.66% px>60 -
      INSIDE the same-cache leg-to-leg floor (LINKREG1 vs LINKREG0f: 4.67, 1.98%). So at this stand its
      draw is not visibly distinguishable; it is drawn ("pixel shader 00000000", 8/frame in AUTOIDX1).
      What it draws elsewhere is UNMEASURED.
    CFB18804: the known flaky XenosRecomp crasher (offline 4/10 old exe, 7/10 fixed exe - NOT SEPARATED at
      n=10 per arm; no claim that either exe crashes it more or less); it is NOT in
      the prelink cache either, so it is ABSENT IN BOTH ARMS - not an arm difference, and not a
      regression; still a translation gap to close.
  Verdict: no translation regression from register linkage; the fix arm now has every shader the control
  has. NOT a release question - native-gpu publishes nothing (RELEASE_GATE.md).
- REPOSITORIES CARRYING LOCAL WORK (none pushed, none to be pushed without the user's word):
    1. fable2recomp worktree wt-fable2-nativegpu, branch native-gpu (no upstream).
    2. rexglue-src (hotfix-0.2.13-plugin c4ff4bc, main 3e1a642) - never pushed.
    3. NativeGPU/reference/XenosRecomp, branch fable2, origin https://github.com/hedge-dev/XenosRecomp.git
       - a THIRD-PARTY UPSTREAM, not the user's. The linkage fix (95a268c) and the depth fix live ONLY here,
       on no remote. "The XenosRecomp fix" does not exist upstream.
  The JIT runs NativeGPU/build/xenosrecomp/XenosRecomp/XenosRecomp.exe, built BY build_xenosrecomp.cmd
  FROM reference/XenosRecomp (SRC=, verified in the script) - build/ is that build's output folder inside the
  NativeGPU repo (branch native-gpu), not a second XenosRecomp checkout. NativeGPU's one tracked
  modification is fable2_shader_common.h (+4 lines, dated 2026-09-21, not from this session): the JIT's
  header for EVERY translation, so identical in both arms - but uncommitted, and worth an owner.
- THE OWNERLESS HEADER, RESOLVED: NativeGPU/fable2_shader_common.h's uncommitted +4 lines are g_TessFactor /
  g_TessOffset (c33.zw) - the header half of the tessellator emulation (45f2e49, 2026-09-21 15:40; file
  dated 14:45 the same day). Every _q shader reads them (0EF7C66A_q: 11 references), so reverting would
  break the ground. COMMITTED with that rationale in the NativeGPU repo (branch native-gpu, local).
- FERNS / "DARK HERO" - DIAGNOSIS (2026-09-24, after LINKREG1). Looking at the pictures side by side
  (native vs plugin, same moment): the hero is NOT uniformly dark - its visible parts (red coat, axe) are
  lit; what covers its lower body is FERN, drawn as large near-black jagged masses where the plugin has
  thin orange-brown fronds, and the grass tufts are missing. So items 2-3 are mostly ONE defect: foliage.
  The position probe (FERNPROBE1) names only draws it can evaluate (16-bit indices, uncomputed
  positions) - it listed the water (F22BC502, g_WaterConstants) and 9A193FEB, NOT the ferns: a probe that
  skips a draw silently is not a denial.
  By the game's own constant names the instanced foliage VS are 8C74AF61 (g_WindDirections, g_SwaySpeed),
  DBF9A58A, 6B210267, 6E1021DB, D4686D15 (g_RepeatedMeshConstants / g_InstanceOffsetSize); trees C27115F5.
  CAUSE CANDIDATE - PsParamGen: in EVERY pair looked at, the PS reads .xy of the register ONE PAST the
  VS's last export (A92D4C17 r8 / VS 0-7; D6F02D0B r2 / 0-1; 0B24BD42 r5 / 0-4; 3DC80BD0 r9 / 0-8;
  9A193FEB r7 / 0-6; 5FEF6617 r8 / 0-7), as `abs(rN.xy) * c0.zw + c0.xy` -> tfetch2D: a SCREEN-SPACE
  lookup of the pixel position the hardware writes there (SQ_PROGRAM_CNTL.param_gen, register
  SQ_CONTEXT_MISC.param_gen_pos; the SDK: dxbc_translator.cpp PsParamGen). XenosRecomp only writes it into
  a register NOT listed as an interpolant, the container's field for it is 0 in all 46 PS, and Fable's PS
  tables list all 16 registers - so every such lookup read the SAME texel at position 0.
  FIX (cvar ngpu_param_gen, default on; off = the previous behaviour): the native side passes
  param_gen_pos + 1 in bits 24..31 of c33.z (read from the replay's own 0x2180/0x2181); the XenosRecomp
  PS prologue writes (floor(pos.x) signed by facing, floor(pos.y), 0, 0) into that register.
  PGEN1 PRE-REGISTERED (same binary 15:27, same freshly re-translated PS cache, ONLY ngpu_param_gen differs;
  control PGEN1C = false):
    PREDICTS: the ferns lose their near-black and move toward the plugin's orange-brown; the lighting of
      9A193FEB's arches/rocks CHANGES (they read r7.xy too) - toward the plugin. If the arches do NOT change
      at all, the param-gen story is wrong for them.
    NON-REGRESSION: near band stays ~0.2%; island/shrine, far hills, lake shape.
    INSTRUMENT: "param gen: N draws" must be > 0 in PGEN1 and 0 in PGEN1C, or the setting did not apply.
- PGEN1 RESULT (same binary, same cache; only ngpu_param_gen differs; instrument: 3,623,354 draws carried
  a PsParamGen register in PGEN1, 0 in PGEN1C - the setting applied). Region mean |d| on-vs-off against
  the same-cache leg-to-leg floor (LINKREG1 vs LINKREG0f / vs LINKREG3):
    water  36.67 (floor 0.28 / 0.02) - brighter, mean red 47 -> 62 (plugin 144): toward the plugin, small
    arches 20.92 (floor 3.81 / 2.79) - brighter, 68 -> 74 (plugin 138): toward the plugin - PREDICTION MET
    ferns   0.56 (floor 0.55 / 0.63) - NO CHANGE - PREDICTION NOT MET: param gen is NOT the fern cause
    ground  0.10 (floor 0.30 / 0.01) - unchanged; near band 0.2% both arms (non-regression holds)
  So PsParamGen is a real, hardware-correct fix with a measured effect on water and 9A193FEB's surfaces,
  and it is kept (default on). The ferns remain UNEXPLAINED.
- FERN IDENTITY (FERNONLY1, ngpu_only_ps = A92D4C17's low 32 bits): drawing only PS A92D4C17 leaves exactly
  the fern clumps (shots/2026-09-24_bowerlake_FERNONLY1_ps_A92D4C17_only.png) - dim red-brown fronds, on
  their own already far darker than the plugin's orange-brown. Its VS are 6E1021DB / 6B210267 / D4686D15
  (instanced repeated meshes, COLOR0 declared at reg 7). NEXT for the ferns: A92D4C17's inputs one at a
  time (textures by slot, the vertex colour reg 7, the lighting constants), same one-difference method.
- CORRECTIONS AFTER PGEN1 (with claudecode-76):
  * The (33,10,6) / 183 / (12,0,0) figures at "DISTANCE-TERM HYPOTHESIS" and rows (g)/(h) ARE sourced -
    measured on CLEAR1's native frame in this session - but the PIXEL REGIONS were never recorded and
    CLEAR1 predates register linkage and param gen (the ground behind the hero was black then). They are
    SUPERSEDED, not to be quoted for the current build. Row (g)'s inference ("partly lit, so attenuating
    not zeroing") rested on an unrecorded region and is withdrawn.
  * ROW (g) HERO DISSOLVES INTO ROW (h) FERNS: by picture (LINKREG1/3, PGEN1) the hero's visible coat and
    axe are lit; the dark mass over it is fern (PS A92D4C17, FERNONLY1). Item 2 is NOT a separate hero-
    lighting defect - do not hunt one. One row: FERNS (and missing grass tufts).
  * THE NON-REGRESSION LIST (a-e) IS A "NOT-OBVIOUSLY-WRONG" LIST, NOT A "VERIFIED-MATCHING" ONE: the
    arches were on it as correct, yet param gen moved them 20.9 against a 3.8 floor, toward the plugin
    (mean red 68 -> 74, plugin 138). It guards against gross regression only. Attributing a future change
    on those items needs a MEASURED baseline against the plugin first.
  * WATER IS BETTER, NOT CORRECT: mean red 47 -> 62 against the plugin's 144. Item 5 stays OPEN.
- RULE (claudecode-76, after the CLEAR1 figures expired): EVERY PIXEL FIGURE CARRIES ITS REGION AND ITS
  BUILD, or it cannot be re-taken and silently expires the moment the build moves.
- FERNWHITE0 (PS A92D4C17 only: texture slot 0 served white; everything else as PGEN1): fern region
  |d| 64.08 vs PGEN1 (region y420-540 x600-960, native window px). Picture
  (shots/2026-09-24_bowerlake_FERNWHITE0_left_real_right_slot0_white.png): with white albedo the ferns
  become SOLID CARDS (alpha 1) coloured CRIMSON near and TAN far. So TWO inputs are wrong:
    - slot 0 (albedo + alpha, r9 = tfetch Sampler0, squared, alpha = r9.w) decodes nearly BLACK - a
      texture decode/format suspect;
    - the NEAR lighting term is strongly red (the far branch, p0 = distance > c67.x, adds Sampler4/5
      lookups and comes out a plausible tan) - a near-lighting input suspect (vertex colour r7 / c20 /
      c22 / c80 / c87).
  NEXT: dump slot 0's texture for this draw (format, untile) and compare with the plugin's.
- PARITY BASELINE (measured, not eyeballed). Capture PGEN1; build fable2.exe 2026-09-24 15:27 with the
  register-linkage + param-gen cache; native win0_wgc 1282x759 vs plugin win1_wgc scaled to 1282 wide,
  bottom-aligned (offset -8 px); regions in native window px (y0,y1,x0,x1); luminance ratio native/plugin:
    arches (170,340,300,600) 0.58 | island_shrine (160,230,560,760) 0.81 | far_hills (60,150,0,1282) 0.86
    (includes part of the plugin's perf overlay) | water (260,330,700,1200) 0.53 | near_ground
    (600,759,0,1282) 0.52 | ferns (420,540,600,960) 0.37 | sky (40,70,300,900) 0.84
  The "correct natively" items are MEASURABLY WRONG: NEAR surfaces sit at ~0.5 of the plugin's luminance,
  FAR ones ~0.85. Shapes match; brightness does not. A common near-lighting / exposure factor is the
  obvious next hypothesis (item 6) - unmeasured.
- PARITY TABLE AMENDED: far_hills re-sampled CLEAR of the plugin's overlay, region (60,150,0,1000):
  0.81 (replaces the contaminated 0.86 row). Far end = island 0.81, far hills 0.81, sky 0.84; near ~0.5.
- RULE (with claudecode-76): WHEN A MASKING DEFECT IS FIXED, HYPOTHESES REFUTED WHILE IT WAS PRESENT ARE
  RE-EXAMINED, NOT LEFT DEAD. The DISTANCE-TERM refutation rested on CLEAR1 (near ground exactly 0, step
  profile) - sound then, not transferable now. Its specific mechanism (the dark IS the fog colour) stays
  refuted; the CLASS (a common near-lighting / exposure factor) is REOPENED by the parity baseline.
- FERN ROWS SPLIT (two faults, no shared fix):
    (h1) fern ALBEDO+ALPHA texture (A92D4C17 slot 0) decodes nearly black - texture decode/format.
    (h2) fern NEAR LIGHTING strongly red (FERNWHITE0 cards crimson near, tan far). Caveat before using the
      cards as the distance probe for item 6: near/far here is the SHADER'S OWN branch (p0 = distance >
      c67.x, far adds Sampler4/5 lookups), so it tests that branch, not necessarily a global factor.
- ITEM 6 - ONE MATERIAL AT THREE DISTANCES (claudecode-76's unconfounded probe). TERRONLY1 (only PS 81B999B2,
  the terrain colour pass) covers native rows 320-720 (shore to camera) - the distant hills are OTHER
  shaders, so the test stays inside those rows. Pixels masked to 81B999B2's coverage in TERRONLY1, values
  from capture PGEN1 (build 15:27), luminance native/plugin:
    rows 330-400 (farthest, shore) 0.53 | rows 440-540 0.49 | rows 600-720 (nearest) 0.53
  FLAT. No distance-dependent factor over this range: the parity table's near-vs-far split is WHICH
  MATERIALS are near, not distance. The distance-factor CLASS is therefore REFUTED ON THE CURRENT BUILD.
  Caveats: range shore-to-camera only; the plugin's grass tufts sit on the terrain in every band.
- EXPONENT BIAS REFUTED BY REGISTER VALUE: the SDK scales PS output by 2^RB_COLOR_INFO.color_exp_bias and the
  native path ignores it - but every RB_COLOR_INFO in PGEN1's render-target histogram has bits 20-25 = 0
  (00000000 / 00020000 / 00030000). The scene target 14010500 is colour 00030000 = format 3,
  2_10_10_10_FLOAT (HDR), bias 0. Not the ~0.5.
- NEXT HYPOTHESIS for the ~0.5 (UNMEASURED): one factor applied BEFORE tonemapping. A uniform HDR scale shows
  as a SMALLER ratio on mid-bright surfaces (terrain ~0.5) than on bright ones (sky 0.84), because the tonemap
  compresses highlights - consistent with the parity table. Candidates: the auto-exposure / adaptation value
  the tonemap reads, or how the native path stores the 2_10_10_10_FLOAT scene target. Test: the scene target
  BEFORE the tonemap, native vs plugin, same pixel.
- PRE-TONEMAP-SCALE PRE-CHECK (claudecode-76; free, same PGEN1 capture and regions as the parity table): a
  single scale before a compressive tonemap predicts the ratio RISES MONOTONICALLY with the plugin's own
  luminance. Ordered by plugin luminance: ferns 86.3 -> 0.37 | near_ground 106.6 -> 0.52 | island 137.7 ->
  0.81 | arches 145.7 -> 0.58 | water 155.1 -> 0.53 | far_hills 169.6 -> 0.81 | sky 219.5 -> 0.84.
  NOT MONOTONIC: arches and water are brighter than the island in the plugin yet far darker natively. A
  pure brightness-dependent single scale does NOT explain the table - the hypothesis FAILS its pre-check.
  What the table does split on: the ~0.5 group (ground, arches, water) are the materials seen reading a
  SCREEN-SPACE LOOKUP through the param-gen register (terrain 5FEF6617/81B999B2, 9A193FEB, the water PS -
  param gen moved arches and water); the ~0.82 group (sky, hills, island) is not known to. NEXT HYPOTHESIS
  (UNMEASURED): the screen-space buffer those materials sample (A92D4C17's Sampler8, `.x` = a light/shadow
  mask) holds the wrong values. Test: serve that one slot white for one material; if its ratio rises toward
  ~0.82 the buffer is the factor.
- SHADOWBUF1 PRE-REGISTERED: terrain 81B999B2, arches 9A193FEB and ground 5FEF6617 all read the SAME
  screen-space buffer - texture slot 8, `.x` - at the param-gen position. Leg: slot 8 served WHITE for PS
  81B999B2 ONLY (ngpu_tex_slots = ~(1<<8), ngpu_tex_slots_only_ps = 81B999B2), otherwise as PGEN1.
  Measure: luminance ratio native/plugin on rows 330-720 masked to TERRONLY1's coverage (PGEN1: ~0.5).
  PREDICTS: well above 0.5 if the buffer is the factor. Within the floor = the buffer is not the factor.
- SHADOWBUF1 RESULT. Served value checked BEFORE reading it (claudecode-76): in 81B999B2 the slot-8 value
  combines MULTIPLICATIVELY (r0.w = tfetch(slot 8).x; r2.x = r1.w * r0.w), so white = 1.0 is the neutral
  "fully lit" value and removes the term. Terrain rows 330-720 masked to TERRONLY1, luminance native/plugin:
    PGEN1 0.514 | PGEN1C 0.513 (param gen does not move the terrain) | SHADOWBUF1 0.934
  The screen-space buffer at slot 8 accounts for MOST of the terrain's deficit: its native values are far
  darker than the plugin's. Upper-bound caveat: white = lit everywhere, including pixels the plugin really
  shadows, so 0.934 over-corrects; the residual (0.93 vs 1.0) is not attributed.
  The rule for a partial rise was not written before this leg; the rise is large, not partial, so it did
  not come into play - noted, not excused.
- WHO READS IT (static, all 44 drawn PS from PAIRS_bowerlake_LINKREG1): 12 contain the param-gen screen
  transform `abs(rN.xy) * c0.zw + c0.xy` (9A193FEB, 81B999B2, 5FEF6617, D6F02D0B, 0B24BD42, A92D4C17,
  C1B813C9, 3DC80BD0, EDBD9566, 3EA51176, 957C7EA6, 6B077959); 31 do not; 1 has no HLSL (00000000).
  A first regex that required a `.xy` destination MISSED D6F02D0B (`r0.xz = ...`) - recounted. "Absent"
  means "not this exact transform", NOT "no param gen": the water PS (B9B8BECA/E33859F0) is absent here yet
  param gen moved the water, so it uses another form. The sky/hills/island membership therefore stays
  OPEN - their shaders are not yet identified.
- NEXT: the slot-8 buffer itself - what render target is resolved into it (probably the sun shadow mask),
  native vs plugin, same frame.
- TERRAIN RESIDUAL (claudecode-76): with slot 8 white the native terrain is lit as if NOTHING shadows it,
  while the plugin keeps its real shadows - so the arm should read ABOVE 1.0. It reads 0.934: 6.6% BELOW
  parity even unshadowed. OPEN ROW (i) "terrain residual", not attributed. Candidates, none measured: (1)
  the plugin's GRASS TUFTS cover much of the masked region (bright green, absent natively), raising the
  plugin's mean; (2) the region is barely shadowed in the plugin, so white was nearly neutral there;
  (3) a second, smaller lighting deficit. (1) is testable without a leg by sampling bare-dirt pixels only.
- RULE FOR THE NEXT TEST (written BEFORE it): the slot-8 buffer-contents test compares the buffer native vs
  plugin. A PARTIAL result - the buffer native/plugin ratio between 0.6 and 0.9 over the same region - means
  the buffer is ONE contributor; the row stays open with the residual named. A ratio >= 0.9 = the buffer
  contents are not the fault (the fault is in how it is SAMPLED). < 0.6 = the buffer is produced wrong.
- ROW (i) CANDIDATE 1 TESTED FIRST (claudecode-76: the only candidate able to close the row). Terrain rows
  330-720 masked to TERRONLY1, split by the PLUGIN's pixel colour in BOTH captures (dirt = r > 1.08 g and
  g > b; non-dirt = the rest), luminance native/plugin:
    dirt     120,991 px: PGEN1 0.492 | SHADOWBUF1 0.814
    non-dirt 271,487 px: PGEN1 0.520 | SHADOWBUF1 0.975
    all      400,797 px: PGEN1 0.514 | SHADOWBUF1 0.934
  REFUTED, and inverted: the residual is NOT grass inflating the plugin - grassy pixels reach 0.975
  unshadowed, and the deficit concentrates on the DIRT (0.814). Row (i) is REAL and is a DIRT-LAYER
  deficit - candidate for the same class as h1 (a texture that decodes dark): the terrain's dirt texture.
  Caveat: the dirt/grass split is by the plugin's colour, so a pixel the native blends differently can
  still fall in either class.
- ASSUMPTION NOT YET VERIFIED (claudecode-76): the bound "native base <= 0.934 of the plugin's" needs the
  slot-8 value <= 1. The combine is multiplicative, but the buffer's FORMAT (UNORM caps at 1; a float
  buffer need not) is not yet read. Do not quote a minimum base deficit until it is.
- THRESHOLD RULE AMENDED (before the buffer test): >= 0.97 = contents fine; 0.90-0.97 = MOSTLY fine WITH A
  NAMED RESIDUAL row; 0.6-0.9 = one contributor; < 0.6 = produced wrong.
- DOES ROW (i) / h1 MERGE INTO THE MORNING'S GAMMA ROW? (claudecode-76's check) NO - BY DIRECTION, before any
  format lookup: fetch-constant gamma (sign 3) tells the hardware to LINEARISE gamma-encoded texels on
  sampling, which LOWERS them (~encoded^2); not honouring it leaves the texels at their encoded, HIGHER
  values - the texture comes out BRIGHTER, not darker. Both row (i) (dirt 0.814) and h1 (fern albedo nearly
  black) are too DARK, the opposite sign. PGEN1's census: gamma-flagged textures = fmt18 (DXT1) signs 3330 x5
  only (fmt29 signs 1111 x343 = signed, already served as SNORM). The gamma row stays its own (and predicts
  something too BRIGHT somewhere - worth looking for); rows (i) and h1 remain separate darkening faults,
  possibly one decode class between them - unproven.
- GAMMA ROW'S FORWARD PREDICTION (unsatisfied, informative): the 5 gamma-flagged DXT1s (signs 3330) sampled
  unlinearised should read TOO BRIGHT somewhere; no parity region does (max 0.84). Branches: (1) they are
  used in no measured region; (2) a darkening fault in the same region masks them (compound, ratio
  uninterpretable until separated); (3) gamma is honoured somewhere after all. Discriminator for (1):
  which draws bind those 5 textures - needs the per-draw fetch-constant -> PS mapping, which no current log
  line gives (the census counts formats, not users): an instrument to add, not a lookup.
- SLOT 8 TRACED (SLOT8REP1, RTDUMP1): the terrain's slot 8 = texture 12B1D000 1280x720 fmt 2 (k_8), served
  as a real texture = the NATIVE resolve F2B1C000 of surface 14000500. The plugin's marker flags it DEPTH
  (flags 0x10); natively it copies a keyed depth that received NO depth-tested draws ("greater 0 less 0"),
  and the dump reads 0.0% lit, mean (0,0,0), ONE distinct colour. The buffer is PRODUCED WRONG (all zero)
  -> the terrain's multiplicative term is 0 everywhere -> ambient only -> ~0.5. (Branch "< 0.6 = produced
  wrong" of the rule written before this test.)
- WHY EMPTY - CANDIDATE: PS D32D70C8 is a DEPTH-RESTORE shader (oC0 = 0, oDepth = saturate(tfetch slot 13).x),
  drawn a few times a frame. Its microcode exports depth; its container declares none. This morning's
  "fix" followed the CONTAINER and dropped the write - the SAME mistake class as the linkage bug (container
  metadata over microcode). The SDK decides depth output from the microcode (Shader::writes_depth).
  FIX (XenosRecomp, local): SV_Depth declared iff the generated body writes oDepth; 1 of 45 PS affected
  (D32D70C8). The dropped-local workaround is removed.
  DEPTHOUT1 PRE-REGISTERED (binary 15:27 unchanged; the ONLY change is D32D70C8's cache entry; control =
  PGEN1 / SLOT8REP1 / RTDUMP1): PREDICTS the F2B1C000 dump is no longer one flat colour (lit > 0%), and the
  terrain ratio (rows 330-720, TERRONLY1 mask) rises above PGEN1's 0.514 by more than the leg floor, toward
  SHADOWBUF1's 0.934. Neither = the depth restore is not what fills slot 8 (or its native draw does not
  write depth - PSO depth state to check next).
- DEPTHOUT1 RESULT - PREDICTION NOT MET. D32D70C8 re-translated with SV_Depth (JIT confirmed in DEPTHOUT0);
  F2B1C000 dumps still 0.0% lit, one colour, in all four dump frames; terrain ratio 0.514 (PGEN1 0.514).
  The microcode-depth fix is kept (hardware-correct, SDK-consistent) but is NOT what empties slot 8 - or
  its native draw never reaches the depth the resolve copies.
- D32D70C8's STATE (D32REP2; a first leg D32REP1 passed a HAND-COMPUTED signed hash that matched nothing -
  the value python printed beside it was different; rerun with the computed value): RB_DEPTHCONTROL
  00708766 = z test on, z WRITE on, func 6 (GEQUAL), stencil off; RB_COLOR_MASK 0 (depth-only);
  blend 00010001. So it is a depth-only GEQUAL write - yet every keyed depth of surface 14000500 reads
  "greater 0 less 0". Its draws land on some OTHER surface/key, or the native path does not count/perform
  them. Its slot 13 (the source it restores from) is NOT in the report: the slot report stops at 12 lines
  per window - the known cap, again.
  NEXT: log D32D70C8's draw surface (RB_SURFACE_INFO / RB_DEPTH_INFO) and native depth key, and lift the
  slot-report cap to cover slot 13.
- D32REP3 (binary 16:14; the slot report now also prints the draw's TARGET registers and, every window,
  how many slot lines it MATCHED and how many the cap REFUSED - zero printed as zero; this window: matched
  36,000, printed 40, refused 35,960). D32D70C8 draws into surfaces 04000140 (colour 00020000, depth
  00010000), 05000140 (320x180, depth 000105A0) and 0A000280 (640x360) - SMALL DOWNSAMPLED TARGETS, never
  14000500. Its slot 13 restores from 13003000 (1024x1024 depth, the shadow map) and 1A110000 (1280x720
  depth = FA10F000, the scene depth). So D32D70C8 is NOT the writer behind slot 8: the "wrong resource"
  branch is answered - it writes a different resource altogether. Who writes depth on 14000500 before the
  F2B1C000 resolve is OPEN (7 colour draws on 14000500/00020000 per frame are the only ones seen there).
- DUMP TIMING (caveat stated, not assumed away): the F2B1C000 dumps are taken INSIDE ResolveNative at the
  resolve itself, from the keyed depth it copies - not at frame end - and the resolve happens once a frame
  (x56-60 per window). Four dump frames (1200..4800) all read one colour.
- CEILING (claudecode-76, from monotonicity alone): B_n/B_p = 0.934 x f(S_p)/f(1) with f increasing and
  S_p <= fully lit, so A PERFECT SLOT 8 LEAVES THE TERRAIN AT OR BELOW 0.934 of the plugin. Slot 8 is the
  largest term but cannot close the terrain gap alone; the rest is row (i) (dirt). No grey-level leg needed.
- NEXT: census the draws on surface 14000500 by PS and RB_DEPTHCONTROL (which writes its depth, if any), and
  whether that depth is cleared or re-keyed between those draws and the F2B1C000 resolve.
- SURFACE CENSUS (new ngpu_surf_census: EVERY draw into one RB_SURFACE_INFO, all classes, total printed):
  * 14000500 (SURF14A): ~4,200 draws / 600 frames in 2 classes - PS 916F6CC7 (colour 00020000) x600 and
    PS C1B813C9 (colour 000A0000) x3,600 = 7 a frame (confirms the earlier "7"); BOTH with RB_DEPTHCONTROL
    00708760 = depth test OFF, depth write OFF, stencil off; RB_DEPTH_INFO 000105A0. NOBODY writes depth or
    stencil on 14000500. D32D70C8 does not appear (population, not sample).
  * 14010500 (SURF14B, the 2xMSAA scene): 749,523 draws / 600 frames in 28 classes, ALL at depth base 0x400
    (RB_DEPTH_INFO 00010400); most with depth write AND STENCIL ENABLED (e.g. 00708767, 00708763, 00708743).
- SLOT 8 - WORKING EXPLANATION (EDRAM ALIASING; consistent with every fact, NOT YET PROVEN): a 2xMSAA
  1280x720 depth takes 1,440 EDRAM tiles (0x5A0) from base 0x400. The F2B1C000 resolve reads a NON-MSAA
  1280x720 depth VIEW at base 0x5A0 through surface 14000500 - EDRAM that lies INSIDE the scene's own
  depth-stencil. On the console that view reads the scene's depth/stencil bytes; natively depth is keyed
  per (surface, depth info), the view has no writer, and the copy is zero. The resolve is k_8 from a
  depth-stencil: whether it is the STENCIL byte (the scene writes stencil - a material/lighting mask would
  fit a multiplicative 0/1 term) is UNPROVEN. The colour path already has EDRAM ownership (ngpu_edram_owner);
  DEPTH has none. A fix is EDRAM-level depth/stencil aliasing - the SDK's render-target cache is the
  reference. This is architecture, not a one-line fix: the user's call before it is built.
- DEAD LIST FOR THE SLOT REPORT (claudecode-76: 36,000 matched / 40 printed). The report is a per-draw
  SAMPLE by design (the first draws' bindings each window), not a population census; conclusions that
  generalise from it are re-checked:
    slot 8 = 12B1D000 k_8 for 81B999B2 - sampled, CORROBORATED on the population by SHADOWBUF1 (whitening
      slot 8 on EVERY 81B999B2 draw moved the terrain 0.51 -> 0.93);
    D32D70C8's targets - sampled; SUPERSEDED by SURF14A (population: it never draws on 14000500);
    this morning's slot 16-18 readings (pre-compaction) - STILL UNCHECKED on the new report;
    "5 gamma textures" - NOT the slot report: the per-texture sign census (every created texture, 2 keys)
      - unaffected; "7 draws on 14000500" - NOT the slot report (live RT line), CONFIRMED by SURF14A.
- NAMING (claudecode-76): stop calling it "the shadow map". It is TEXTURE 12B1D000 = RESOLVE F2B1C000 of
  SURFACE 14000500 (depth view, base 0x5A0, k_8, flags 0x10). Whether it holds shadow, stencil or anything
  else is unproven; the noun follows the evidence.
- UNCAPPED INSTRUMENTS (same day, same class as the slot report): "render targets this frame" was capped at 8
  keys of a SORTED map (always the same 8) - now lists every key with the count (16 at Bower Lake);
  "resolves this frame" is capped at 10 the same way (always ten 256x256 entries) - superseded by:
- RESOLVE CENSUS (RESCEN1; every resolve key, how often it ran, how often its SOURCE had a native writer that
  frame; colour = draws, depth = depth-tested draws over default + keyed depths): 18 keys, and EXACTLY ONE
  whose source NEVER has a writer - surf 14000500 DEPTH fmt 2 flags 10 1280x720: 600 resolves, 0 with a
  writer (= slot 8). The other 17 all have writers, including the scene depth (14010500 DEPTH fmt 23:
  600/600) - the HEALTHY CASE of the aliasing explanation holds.
  So the EDRAM depth-aliasing mechanism predicts ONE broken resolve in this scene, not several - but that
  one resolve feeds every PS that reads slot 8 (9A193FEB, 81B999B2, 5FEF6617, A92D4C17, 3EA51176: arches,
  terrain, near ground, ferns). Caveats: "with a writer" is not "correct content" (the colour owner
  remap exists); the depth-writer count uses the greater/less counters, which may not count every func.
  MAGNITUDE: a non-MSAA 1280x720 view is 720 tiles from 0x5A0; the 2xMSAA scene depth is 1,440 tiles from
  0x400 (wrapping at 0x800) - the view lies ENTIRELY inside it.
- BLAST RADIUS OF THE EDRAM-DEPTH FIX (claudecode-76's question): the 5 PS reading 12B1D000 include the arches
  and near ground, which the non-regression list called correct - but that list was already relabelled
  NOT-OBVIOUSLY-WRONG on the SHAPE/COVERAGE axis, and on the BRIGHTNESS axis both are MEASURED dark (parity:
  arches 0.58, near ground 0.52), consistent with reading an all-zero term. So the fix is predicted to
  BRIGHTEN all five toward the plugin, not to regress two. The non-regression list gains an axis: a-e are
  "correct" on shape/coverage only. Status of the mechanism: ESTABLISHED (fits the broken case, the 17-key
  healthy case, and the magnitude); what the view holds (stencil?) still unproven.
- COMPLEMENT CHECK (claudecode-76: make the mechanism speak about what it does NOT claim). Parity of regions
  NOT known to read 12B1D000: island 0.81, far hills 0.81, sky 0.84 - their shaders are NOT YET IDENTIFIED, so
  their slot-8 membership is unknown; WATER 0.53 - its PS (B9B8BECA/E33859F0) do NOT read slot 8 (their
  screen lookup is slot 2). So "the shaders this buffer does not feed are at parity" is FALSE: the rest sits
  at ~0.8, and the water is ~0.5 WITHOUT slot 8 - a separate cause. What rules out a scene-wide ~0.5 FOR THE
  TERRAIN is the INTERVENTION, not the clustering: SHADOWBUF1 moved it 0.51 -> 0.93, so any scene-wide factor
  on the terrain is at most the remaining ~7%. A ~0.8 factor on the non-slot-8 regions remains possible and
  is unexplained (candidate for the 0.93 residual too).
- EDRAM-DEPTH FIX (option A) PRE-REGISTERED, BEFORE ANY BUILD (baselines: PGEN1 capture, parity regions and
  masks as recorded):
    MOVE UP TOGETHER, each by more than its leg floor: terrain 81B999B2 (TERRONLY1 mask, 0.514), near ground
    5FEF6617 (0.52), arches 9A193FEB (0.58), ferns A92D4C17 (0.37); 3EA51176's region is unidentified -
    counted as UNMEASURED, not as passing.
    CEILING: terrain <= 0.934 (monotonicity); dirt pixels stay below grass (row (i)).
    CONTROL: the water (0.53, not a slot-8 reader) must NOT move beyond its floor; island/hills/sky likewise
    unless their shaders are shown to read slot 8.
    INSTRUMENT: the resolve census must report 0 keys whose source never had a writer.
    FALSIFIER: only the terrain moves, or the water moves as much as the slot-8 readers.
- PARITY FLOOR (claudecode-76: a pre-registration without a floor accepts noise). Six SAME-RENDER legs
  (PGEN1, DEPTHOUT1 - only D32D70C8 differs, which draws nothing in these regions - and RESCEN1, RTLIST1,
  SURF14A, SURF14B - logging-only differences), same regions/masks as the parity table:
    terrain_masked 0.513-0.514 (range 0.001) | near_ground 0.524-0.525 (0.001) | ferns 0.365-0.366 (0.001)
    arches 0.582-0.618 (0.036) | water 0.529-0.579 (0.049) | island 0.806-0.857 (0.051) | hills 0.810-0.833
    (0.023) | sky 0.835-0.846 (0.011)
  Five legs agree within ~0.003 everywhere; RTLIST1 alone reads 0.02-0.05 HIGHER in the UPPER-SCREEN regions
  only (arches/island/water/hills) - a moment-dependent difference in the scene, cause unconfirmed.
- OPTION A THRESHOLDS, WRITTEN NOW (before any build): each slot-8 reader must rise by >= 0.10 (twice the
  worst same-render range) - terrain_masked, near_ground, arches, ferns; the terrain's own expectation is
  ~+0.4 (SHADOWBUF1). The WATER control must stay within +-0.05 of 0.538. Anything between = not separated.
- ROW (i) vs THE ~0.81 BAND (claudecode-76's coincidence: dirt 0.814 unshadowed; island/hills 0.81): noted,
  NOT merged. Grass reads 0.975 unshadowed, so any shared factor is NOT global - at most a class including
  dirt, island, hills, sky and excluding grass. Blocker: island/hills/sky shaders are unidentified.
- ISLAND IDENTIFIED (ISLPROBE1, probe at 0.515,0.218; the probe lists every covering draw it can evaluate and
  silently skips the rest): PS 9A193FEB with VS BCC6E2DE (ndc z 0.9988 / 0.9999 - far geometry), also
  C3620F8C / B72AB2DF passes over the same triangles. So the island IS a slot-8 reader - it leaves the
  complement. Same PS as the arches, yet island 0.81 vs arches 0.58: the slot-8 term matters less on far
  geometry (fog / distance weighting are candidates, unproven). Option A pre-registration amended: the island
  is expected to rise, with NO magnitude committed (counted "moved / not separated", not pass/fail); hills and
  sky remain unidentified; water stays the control.
- LABEL FIX (claudecode-76; same class as the non-regression list): wherever this record calls
  BCC6E2DE/9A193FEB (or PS 9A193FEB) the "healthy control" it means NOT FLAGGED ON LINKAGE - nothing more. On
  brightness 9A193FEB reads an all-zero slot 8 (arches 0.58, island 0.81). Row (a)'s linkage result is
  unaffected (interventional, cache-only difference).
- OPTION A AMENDED AGAIN: the island gets a DIRECTION criterion - it MUST NOT DECREASE (below 0.806, the
  lowest same-render reading). Upper-screen regions carry a ~0.05 floor (RTLIST1), so hills 0.81 and sky 0.84
  are ONE number at that precision, not a range.
- PRE-REGISTERED NUISANCE: upper-screen departures of up to 0.05 (as RTLIST1: arches/island/water/hills),
  cause unconfirmed, are NOT evidence for or against any change. One appearing in an option-A leg is read
  as this nuisance unless it exceeds 0.05.
- NATURAL CONTROL AVAILABLE: arches vs island = ONE PS (9A193FEB), one code path, two distances (0.58 vs
  0.81) - the distance weakening of the slot-8 term is measurable without a build.
- HILLS / SKY IDENTIFICATION ATTEMPT (HILLPROBE1 0.234,0.086; SKYPROBE1 0.468,0.024) - THE PROBE FAILS ITS OWN
  CONTROL: both return the SAME list as each other and as the fern and island probes (2A5207A8/9A193FEB 1995
  tris, 36F4DD57/9A193FEB 4016, E4D8ABB1/C3620F8C 1995, 1CF397A6/D6F02D0B 4016). Entries that appear at
  EVERY probed point do not locate anything (large meshes, or a transform the probe gets wrong); only
  POINT-SPECIFIC entries count. Island: BCC6E2DE/9A193FEB (589 and 2519 tris) appears only there - that
  identification SURVIVES. Hills: no point-specific scene entry (only shadow-map-pass lines). Sky: none - its
  draw is one the probe cannot evaluate. HILLS AND SKY REMAIN UNIDENTIFIED; the complement's only VERIFIED
  non-reader is still the WATER (n=1). Next method: ngpu_only_ps per candidate PS (population, not probe).
- DEAD LIST CLOSED: this morning's slots 16-18 came from the RAW report (VSSLOTS1), not the capped one, and
  today's SLOT8REP1 raw lines give the same fetch words (1B597058 fmt 24 k_16 heightmap, 1B6D1071 fmt 49 DXN,
  1B82907A fmt 58 DXT3A) - RE-DERIVED on a second run (still a per-draw sample, not a population).
- IDENTIFICATION BY POPULATION (only_ps): A8EAA0D5 (the depth-off 2-a-frame candidate for the sky) draws
  NOTHING visible - not the sky. 52FE118F (far terrain) covers island 100%, far hills 71%, arches 57%, water
  50% of those regions - the distant landscape; NOT a slot-8 reader (by the transform census). So "far
  hills" = mostly 52FE118F, and the island region = 52FE118F land + 9A193FEB shrine (point-specific probe).
  The FERN identification came from FERNONLY1 (population), not the probe - rows h1/h2 keep their footing.
- WHITEADDR1 PRE-REGISTERED (claudecode-76: SENSITIVITY by intervention, not membership by enumeration): new
  cvar ngpu_white_base serves texture 12B1D000 WHITE in EVERY shader and slot that binds it (by ADDRESS, not
  slot index - slot 8 holds other textures in other shaders). Compare each parity region to the same-render
  mean. Moves > the floor (ground regions 0.003, upper-screen 0.05) = SENSITIVE to 12B1D000. Caveats: white is
  not the correct value (reach, not the post-fix numbers); a non-move shows insensitivity AT WHITE only.
  Expected: terrain/near ground/arches/ferns up; water ~unchanged; hills (52FE118F) ~unchanged.
- WHITEADDR1 RESULT (texture 12B1D000 served WHITE in every shader/slot binding it: 4,719,982 bindings,
  counter confirms). Parity native/plugin, same-render baseline -> white (change):
    near_ground 0.524 -> 0.984 (+0.460) | terrain_masked 0.513 -> 0.969 (+0.456) | ferns 0.366 -> 0.676
    (+0.310) | WATER 0.538 -> 0.808 (+0.270) | arches 0.589 -> 0.808 (+0.219) | island 0.815 -> 0.862
    (+0.047, inside the 0.05 upper-screen floor) | far_hills 0.814 -> 0.822 (+0.008) | sky 0.839 -> 0.841
  REACH: 12B1D000 controls most of the near scene's brightness deficit - near ground and terrain reach
  ~0.97-0.98 of the plugin with it neutralised. HILLS AND SKY ARE INSENSITIVE: their ~0.82 is a SEPARATE
  fault, now its own open row. Island not separated.
  THE WATER CONTROL FAILED: pre-registered "must not move", it moved +0.27. Either the water PS bind
  12B1D000 at a slot the transform census did not match (by-address whitening catches every slot), or the
  "water" region holds land (52FE118F covers 50% of it). Not resolved; the water prediction is recorded as
  FAILED, not explained away.
  Note: terrain 0.969 exceeds SHADOWBUF1's 0.934 ceiling - that ceiling was derived for whitening ONE shader
  (81B999B2); here 5FEF6617's pieces over the same pixels are neutralised too. Different intervention.
- WATER CONTROL RE-CUT (claudecode-76: mask it to the water shader's own coverage) - AND A RETRACTION. Drawing
  ONLY PS B9B8BECA (ONLY_B9B8BECA) renders NOTHING visible: it covers 0% of the "water" region (5.3% of the
  frame above the r+g+b>15 threshold, all faint). B9B8BECA/E33859F0 (VS F22BC502, g_WaterConstants) are NOT
  the lake surface we see. So "the water PS do not read slot 8" was about the WRONG shaders, and the row
  "water dark WITHOUT slot 8 - a separate cause" is RETRACTED: the lake region moves +0.27 when 12B1D000 is
  whitened by address, i.e. whatever draws it is SENSITIVE to 12B1D000. The lake's shader is UNIDENTIFIED.
- OPTION A REBUILT FROM THE SENSITIVITY TABLE (WHITEADDR1), NOT FROM THE TRANSFORM REGEX - which the
  water case shows can miss a binding. TWO-SIDED thresholds (floor = 2x worst same-render range; CAP = the
  white delta, since white is the maximum a sample can return and the response is monotonic):
    near_ground +0.10 .. +0.46 | terrain_masked +0.10 .. +0.46 | ferns +0.10 .. +0.31 | arches +0.10 ..
    +0.22 | lake region +0.10 .. +0.27 | island: must not fall below 0.806, cap +0.05 (not separated).
    A rise ABOVE a subject's cap = something else changed.
  CONTROLS (measured insensitive): far_hills (+0.008 at white) and sky (+0.002) must stay within +-0.05.
  MECHANISM: the resolve census reports 0 keys whose source never had a writer.
- FERN ROWS RESTATED: with 12B1D000 neutralised the ferns reach 0.676 (from 0.366). Most of the fern
  darkness is slot 8; h1 (albedo decode) and h2 (red near lighting) are restated against 0.68, i.e. about
  half the deficit the rows claimed. Not merged with the hills/sky ~0.82 (adjacent numbers are not a cause).
- NEW ROW: far hills (52FE118F) and sky at ~0.82, INSENSITIVE to 12B1D000 - a separate fault, unexplained.
- OPTION A - THREE AMENDMENTS (claudecode-76), before any build:
  * The "lake" row is a REGION, not a subject: renamed "lake REGION (y260-330 x700-1200, native window px;
    mixed: near 52FE118F land + the unidentified lake PS)". Its +0.10..+0.27 bound applies ONLY to that
    identical rectangle; a later read masked to the lake shader is a different population and must not be
    compared with +0.27.
  * CONTROLS ARE INSENSITIVE AT A CONSTANT WHITE ONLY. Hills (+0.008) and sky (+0.002) were measured with the
    sample forced to its maximum; the fix supplies VARYING depth-derived values, and a shader using the value
    in a comparison or branch can be flat under a constant and move under a varying one. A control excursion
    after the build is a SIGNAL TO INVESTIGATE, not an automatic falsification.
  * THE CONTROLS ARE NOT EQUAL: FAR HILLS (52FE118F) is the INFORMATIVE control - ground geometry that could
    plausibly take a depth term, sharing its PS with part of the lake region. SKY is a SMOKE DETECTOR - it may
    be insensitive trivially (far plane, nothing to receive) and catches only gross global changes; it does
    not carry the argument.
- CLAIMS VOIDED BY THE WRONG WATER IDENTITY (one noun, three claims): the separate-water-darkening row
  (retracted), the water as A's control (replaced), and "the complement's only verified non-reader is the
  water" (void - there never was a verified non-reader on that basis; the complement now rests on the
  MEASURED insensitivity of hills and sky).
- DIRT vs HILLS/SKY ON ONE AXIS (claudecode-76's question): with 12B1D000 neutralised in EVERY shader
  (WHITEADDR1; same dirt/grass split and TERRONLY1 mask as before): DIRT 0.930, GRASS 0.981. The earlier
  dirt 0.814 was SHADOWBUF1 - only 81B999B2 whitened, the near-ground pieces (5FEF6617, also a 12B1D000
  reader) still dark over the same pixels. So post-slot-8 dirt is 0.93, NOT at the hills/sky ~0.82 - the
  coincidence DISSOLVES; no shared class. ROW (i) SHRINKS to ~0.05 (dirt 0.93 vs grass 0.98).
- AFTER OPTION A (upper bounds from white; the fix supplies real values, not white): near ground <= 0.98,
  terrain <= 0.97 (dirt <= 0.93), lake region <= 0.81, arches <= 0.81, ferns <= 0.68, island ~0.82-0.86.
  NOT TOUCHED by A: far hills and sky ~0.82 (insensitive); the ferns' own ~0.3 (h1 albedo, h2 red
  lighting); the dirt's ~0.05; the gamma row; the no-HUD and impostor-tree items.
- SHADOWBUF1 IS A PARTIAL-SCOPE INTERVENTION (claudecode-76: "slot 8 served white" named two treatments):
  SHADOWBUF1 whitened slot 8 for 81B999B2 ONLY; 5FEF6617 kept darkening the same pixels. Every figure from it
  is marked PARTIAL SUBSTITUTION and superseded where WHITEADDR1 (by address, every shader) has the number:
    terrain 0.51 -> 0.934 (partial) => 0.969 (WHITEADDR1); dirt 0.814 => 0.930; grass 0.975 => 0.981;
    THE TERRAIN CEILING IS <= 0.97, NOT <= 0.934 (the earlier ceiling understated what option A buys);
    row (i)'s original ~0.19 magnitude => ~0.05.
- PRIORITY RE-ORDER (so the old order does not survive on inertia): after option A, the open deficits by
  size are the far hills / sky ~0.18 (insensitive to 12B1D000) and the ferns' own ~0.3 (h1/h2), then the
  lake-region and arches remainders (~0.19 each at white), and LAST the dirt ~0.05. The dirt-texture
  investigation drops to the bottom of the queue.
- HILLS/SKY - ONE ROW OR TWO? (claudecode-76). SPREAD FIRST, no leg: the six same-render legs give far_hills
  0.810-0.833 (mean 0.814, sd 0.009) and sky 0.835-0.846 (mean 0.839, sd 0.004); RTLIST1's excursion on the
  sky is only +0.007. Both deficits (~0.19 / ~0.16) are stable and far outside their spread.
  SUBJECTS (only_ps, population): SKY = PS 63081965, a FULL-SCREEN gradient pass (covers 100% of the frame;
  rows 50/300/700 = (171,188,198)/(169,187,197)/(139,161,183); shots/..._ONLY_63081965_sky_gradient.png).
  HILLS = PS 52FE118F. 2D5EA8E8 draws nothing visible. TWO DIFFERENT SUBJECTS -> the heading splits:
    ROW (j) SKY 63081965 ~0.84;   ROW (k) FAR HILLS 52FE118F ~0.81.
  A link to test, NOT assumed (mechanism guess): distant terrain is fogged toward the sky/atmosphere colour,
  so the hills could inherit the sky's deficit. Test = the sky pass's constants/inputs, then whether the
  hills' far pixels track them.
- PRESENTPOST1 PRE-REGISTERED: every parity number so far compares the plugin's window (the GAME'S post-
  processed output) against the native window presenting the NATIVE TONEMAP of the HDR scene
  (ngpu_present_post=false, the default) - which skips the game's own post chain (haze/bloom/grading: item 6).
  Leg: same binary, ngpu_present_post=true (present the game's own last 1280-wide 8888 target). If the
  presentation path is a scene-wide factor, every region shifts and sky/hills move toward 1.0; if nothing
  moves beyond the floors, the presentation path is not the ~0.82.
- PRESENTPOST1 VOID: with ngpu_present_post=true the native window is BLACK (every region 0.000) - that path
  presents nothing under bridge replay. It does not test the hypothesis.
- FRONT-BUFFER DUMP VOID: the native dump of the game's final composite (res_FECFE000, fmt 6 1280x720, from
  14000500) is a SOLID GREEN placeholder (mean 1.2,255,1.2) - not an image.
- WHAT THE TWO VOIDS ESTABLISH: the game's own post chain + HUD into FECFE000 is NOT produced natively. The
  native window shows the NATIVE TONEMAP of the HDR scene; the plugin window shows the game's full post chain.
  So EVERY native/plugin ratio in this record compares two DIFFERENT PRESENTATION PATHS by construction. Deltas
  measured WITHIN the native path (option A's two-sided thresholds, all interventions) remain valid; ABSOLUTE
  ratios are not parity figures. Rows (j) sky and (k) hills (~0.82) may be partly or wholly the missing post
  chain (haze/bloom/grading, item 6) - untestable until the post chain renders natively. Same class as item 7
  (no HUD): both are the unported post/UI chain.
- FIXED RECTANGLES (claudecode-76; the lake trap): row (j) = SKY RECTANGLE (y40-70 x300-900, native window
  px); its subject 63081965 is a FULL-SCREEN pass covering 100% of the frame, so a read masked to its coverage
  is a different population - post-fix reads use the identical rectangle. Row (k) = HILLS RECTANGLE (y60-150
  x0-1000); 52FE118F also draws island/lake-region land. Option A's sky smoke detector: a change there is a
  scene-wide event by construction.
- FOG, TWO QUESTIONS: the terrain distance/fog hypothesis stays REFUTED on the terrain (0.53/0.49/0.53); it
  says nothing about the full-screen sky pass, and the sky guess does not revive it.
- PRESENTATION-PATH DAMAGE, BOUNDED (claudecode-76):
  UNAFFECTED - every intervention (both arms share the native path); option A's thresholds (for a fixed
  region the plugin value is a constant denominator, so ratio deltas are native deltas); the EDRAM mechanism
  (resolve census + interventions); row (a) (native vs native).
  AFFECTED - every ABSOLUTE native/plugin ratio; rows (i), (j), (k) magnitudes; the after-A absolute column;
  the terrain "<= 0.97" is a RATIO BETWEEN TWO PRESENTATION PATHS, not a parity bound (second correction of
  that figure). A tonemap is NONLINEAR, so it does NOT cancel in cross-region comparisons (row (i)'s dirt vs
  grass inherits it).
  BOUND: under WHITEADDR1 near ground reaches 0.984 and terrain 0.969 ACROSS the two paths, so the
  presentation difference is SMALL at mid luminance (~100); unbounded at high luminance (sky).
- DOES THE RESIDUAL TRACK LUMINANCE (post-chain prediction; WHITEADDR1 ratios by plugin luminance): ground 107
  -> 0.98 | island 138 -> 0.86 | arches 146 -> 0.81 | lake region 155 -> 0.81 | hills 170 -> 0.82 | sky 220 ->
  0.84 (ferns 86 -> 0.68 excluded: known own faults h1/h2, not a clean subject). A STEP above ~130, then FLAT
  and slightly REVERSED (sky above hills): PARTIAL support only - not a clean monotone relation. The post chain
  is NOT established as the cause of (j)/(k); they keep their rows. Post chain and item 7 (no HUD) are
  PROBABLY one item (the game's post/HUD chain not produced natively) - kept as separate rows until connected.
- WITHIN-SUBJECT SKY TEST (claudecode-76: the six-region luminance table is between-subject - subject identity
  and luminance are confounded, and its "step" rests on ONE point below 130). Sky-visible pixels = where the
  full native frame matches the sky-only frame (ONLY_63081965) within 6 per channel, rows 40-300: 35,458 px.
  Binned by the PLUGIN's luminance (PGEN1): 42-155 -> 1.309 | 155-246 -> 1.080 | 246-252 -> 0.738 |
  252-252 -> 0.740 | 252-254 -> 0.745.
  BLOCKED, NOT DECIDED: in the plugin the sky is SATURATED (60% of the pixels at 246-254), so there is no
  luminance range to form a dose-response; at the clip the ratio is FLAT at ~0.74 (native sky ~188). The low
  bands are CONTAMINATED - pixels where the plugin shows foreground (trees, absent or impostor natively), not
  a lower dose. The plugin's clipping is what a brighter/blooming post chain would produce, but a saturated
  reference cannot confirm it. Row (j) stays open; "partial support" for the post-chain story stays at that
  strength. Also noted as a fact, not explained: the upper five regions spread 0.81-0.86, larger than their
  per-region sd (0.004-0.009) - real structure that luminance does not order.
- THE SKY'S ABOVE-1.0 BANDS vs THE GAMMA ROW (claudecode-76: the first ratios above parity today, and the
  gamma row predicts "too bright somewhere"). LOCATION CHECK (no values): the 11,144 above-parity sky-mask
  pixels, overlaid in magenta on the plugin frame (shots/..._sky_above_parity_pixels_magenta.png), sit on
  the UPPER EDGES OF THE PLUGIN'S TREE CANOPIES (drawn natively as sky - trees missing/impostor) and on the
  plugin's PERFORMANCE OVERLAY panel; none in open sky; all have plugin luminance < 200 (median 155).
  CONTAMINATION CONFIRMED; the gamma row gets NO sighting from this.
  The sky mask is ONE-SIDED BY CONSTRUCTION (native-sky pixels): it selects exactly the native's MISSING-
  GEOMETRY defects. A symmetric "sky in both" mask needs a plugin sky-only frame, which is not available -
  so it stands as a known bias with a known direction.
- ROW (j) STRATEGY (claudecode-76): cross-path comparison has been voided/blocked twice (presentation path,
  clipping). The next step for (j) is WITHIN-NATIVE: does the sky pass produce the right values from its
  inputs (its constants c66/c67/c31 and literals) - enumerable, immune to both problems.
- PLUGIN PERF-OVERLAY AUDIT (claudecode-76: a non-game element inside the reference frames). Panel at ~y55-200
  x1072-1262 (window px). Present in ALL six same-render plugin captures (box luminance 123.4-127.6 as its
  digits change) - a constant presence, not a per-leg inconsistency. INTERSECTS NO CURRENT MEASUREMENT
  RECTANGLE (the only one that did - the first full-width far_hills row - was already replaced by the clean
  (60,150,0,1000)). Every rectangle in this record is stated with its coordinates; the panel box is now an
  explicit exclusion.
- RTLIST1's EXCURSION IS NOT THE OVERLAY: the PLUGIN side is identical across all six legs (arches 145.7,
  lake region 155.1, island 137.7, hills 169.6, to 0.1); the NATIVE side is higher in RTLIST1 only (arches
  90.0 vs ~85.0, lake region 89.7 vs ~82.2, island 118.0 vs ~111.1, hills 141.2 vs ~137.5). The departure is
  in the NATIVE render of that leg (upper scene brighter). Cause still unconfirmed; the builds differ only in
  logging, so a moment-dependent native input (e.g. cloud/lighting state at capture) is the candidate.
- RTLIST1 LOCATED AND RENAMED (claudecode-76: "upper-screen" mis-located it). RTLIST1 minus the mean of the
  other five, per region: lake region +0.049, island +0.050, arches +0.035, far hills +0.022, sky +0.009;
  terrain / near ground / ferns +0.000. It is the FAR SCENE BEYOND THE SHORELINE (native render only, window
  rows < ~350), not global and not strictly upper-screen. Renamed: "RTLIST1 far-scene excursion".
- FLOORS BOTH WAYS: range over all six vs over the five without RTLIST1 - lake 0.049/0.002, island 0.051/0.002,
  arches 0.036/0.002, hills 0.023/0.002, sky 0.011/0.005, ground regions 0.001/0.001.
  EFFECT ON OPTION A (direction checked): for READERS the +0.10 floor is a MINIMUM RISE - an inflated floor
  makes the prediction STRICTER, not easier; it stays (conservative). For CONTROLS the band is a MAXIMUM MOVE -
  an inflated band is the flattering direction. Tightened from +-0.05 to +-0.03 for hills and sky: above the
  five-leg noise (0.002-0.005) yet still covering a recurrence of the RTLIST1 excursion (hills +0.022), which
  is a known native moment effect, not the fix.
- ROW (k) PROVENANCE: its ~0.81 comes from the CURRENT hills rectangle (60,150,0,1000) (six-leg mean 0.814);
  the superseded full-width row (0.86, overlapping the plugin's panel) survives nowhere in the live numbers.
- THE TRUE FLOOR IS 0.002 (claudecode-76: RTLIST1 WAS the six-leg floor, 25x the five-leg one) - every
  "inside the floor" judgement made against 0.05 is re-examined. WHITEADDR2 repeats WHITEADDR1 (deltas vs the
  five-leg baseline): island +0.055 / +0.046 | arches +0.225 / +0.212 | lake region +0.278 / +0.270 | hills
  +0.011 / +0.008 | sky +0.003 / +0.004 | ferns +0.310 / +0.310 | near ground +0.459 / +0.459 | terrain +0.456
  / +0.456. Neither is a far-scene excursion leg (hills did not show RTLIST1's +0.022).
  CORRECTIONS: the ISLAND's response to 12B1D000 is REAL (+0.05 twice, 25x the true floor) - it was wrongly
  called "not separated". The HILLS are WEAKLY sensitive (+0.01, ~5x the floor), not insensitive; the sky
  is at ~2x the floor (+0.003/+0.004) - effectively insensitive. Controls keep +-0.03 (covers +0.01).
- OPTION A: POST-FIX READ IS n>=2 LEGS, scored on the LOWER value per subject - a single far-scene excursion
  leg could hand the lake region / island / arches up to +0.05 for free, and after the fix the ground cannot
  serve as an excursion detector (it is expected to rise).
- SHAPE THE RTLIST1 CAUSE MUST EXPLAIN (pre-registered before any candidate): by distance, ground +0.000,
  arches +0.035, lake region +0.049, island +0.050, hills +0.022, sky +0.009 - a MID-DISTANCE PEAK falling to
  ~0 at BOTH ends. Any monotone-in-distance explanation is excluded in advance.
- A FLOOR DECIDES WHICH OBSERVATIONS EXIST: tightening it 25x (0.05 -> 0.002) promoted the island to a real
  responder and demoted the hills from "insensitive" - both from the same line.
- OPTION A, FINAL SHAPE BEFORE ANY BUILD (claudecode-76):
  * NO measured-insensitive subject remains: HILLS are a WEAK SUBJECT WITH A CAP (+0.01 white, replicated),
    not a control - they catch only a gross excursion. SKY is "not shown to respond" (+0.003/+0.004 vs a
    0.002 floor), a smoke detector only. A was built around a null that no longer exists; replaced by:
  * TIERED RANK ORDER (from WHITEADDR1/2, replicated): tier 1 near ground / terrain (~+0.46) > tier 2 ferns
    (~+0.31), lake region (~+0.27), arches (~+0.22) > tier 3 island (~+0.05) > tier 4 hills (~+0.01), sky
    (~+0.004). Post-fix deltas must preserve the TIER order (swaps WITHIN a tier are allowed - real values
    are not white, and lake vs arches are close). FALSIFIER: tiers out of order, or tier 3-4 subjects moving
    as much as tier 1-2 (a global or presentation change would flatten the gradient).
  * PER-SUBJECT TWO-SIDED BOUNDS (replacing the flat +0.10, which tier 1-2 met trivially and tier 3-4 could
    never meet): each subject must rise by at least max(25% of its white delta, 0.006 = 3x the floor) and by
    no more than its white delta + 0.006. 25%, not 50%: the fix returns real shadow values, so a subject
    genuinely shadowed in the plugin can legitimately fall well short of its white delta.
  * n >= 2 post-fix legs, each subject scored on its LOWER value; resolve census must report 0 unwritten keys.
- OPTION A WINDOW AUDIT (claudecode-76; a standing step: every subject's window must be NON-EMPTY). Floor = max(25% of
  white delta, 3 x the subject's OWN five-leg noise); cap = white delta + own noise:
    near_ground     white 0.4590 own-noise 0.001 -> window [0.1148, 0.4600] ok
    terrain_masked  white 0.4560 own-noise 0.001 -> window [0.1140, 0.4570] ok
    ferns           white 0.3100 own-noise 0.001 -> window [0.0775, 0.3110] ok
    lake_region     white 0.2740 own-noise 0.002 -> window [0.0685, 0.2760] ok
    arches          white 0.2185 own-noise 0.002 -> window [0.0546, 0.2205] ok
    island_shrine   white 0.0505 own-noise 0.002 -> window [0.0126, 0.0525] ok
    far_hills       white 0.0095 own-noise 0.002 -> window [0.0060, 0.0115] ok
    sky             white 0.0035 own-noise 0.005 -> window [0.0150, 0.0085] EMPTY - UNSCORABLE
  THE SKY IS UNSCORABLE: its whole possible response (0.0035) is smaller than its own noise (0.005) - the
  instrument cannot resolve it. It keeps NO criterion in A; smoke detector only. Hills: narrow but real.
  (The earlier formula said "3x the floor 0.002" - the GENERAL floor; corrected to each subject's own.)
- ROW (j) SKY, WITHIN-NATIVE (sky pass 63081965 enumerated): reads slots 4, 11, 12, 13, 14 and scales its
  output by c20.w. FIRST-N BIAS CAUGHT: SKYREP1's sample was entirely the 280x280 4xMSAA off-screen target
  04020118 (c20.w = 1), not the scene - new cvar ngpu_slot_report_surf filters the report by RB_SURFACE_INFO.
  SKYREP2 (scene target 14010500 only): slot 4 = 1FAB3000 64x1 fmt 29 (signed LUT), 13/14 = 1A565000 16x16
  fmt 6, 11/12 = 1FC40000 1x1 fmt 6 (the game's default texture, bound in many shaders' unused slots); all
  real textures, NO placeholder, NO unwritten resolve. c20.w (exposure) 7.36-7.55, drifting slowly across
  windows (time-of-day-like); c31.x = 150. NOTHING in the sky pass's inputs is broken on this evidence; row
  (j) most plausibly remains the PRESENTATION PATH (native tonemap vs the game's post chain) - still
  untestable until the post chain renders natively. Row (j) parked there, not closed.
- ROW (j) HEADING NARROWED (claudecode-76): the enumeration covered the SAMPLED TEXTURES and c20.w only - "the
  sky pass's sampled textures and c20.w are ordinary; no placeholder, no unwritten resolve". Render target,
  blend, geometry, the rest of its constants and its translated code were NOT checked. Slots 11/12 = 1FC40000
  1x1 are what the GUEST binds: the report decodes base and size from the guest's own fetch-constant words; a
  native fallback prints "PLACEHOLDER".
- RTLIST1 CANDIDATE c20.w EXCLUDED BY THE PRE-REGISTERED SHAPE: c20.w also scales the fern PS output
  (A92D4C17: oC0.xyz = r7.xyz * c20.w) and the sky pass, yet in RTLIST1 ferns moved +0.000 and the sky
  +0.009 (the smallest) - a c20.w change cannot produce a mid-distance peak with zero at both ends.
- THE BLACK/GREEN PRESENTATION INSTRUMENTS, SIZED (claudecode-76: repair before more rows park behind it):
  the game's post chain DOES run natively - 7 draws/frame into 14000500 colour 00020000 (PS 916F6CC7 x1, PS
  C1B813C9 x6, per SURF14A) - and that target dumps SOLID GREEN: mean (1,255,1), HDR luminance p50 = p90 =
  16.0. Its resolve FECFE000 is the green front buffer. present_post=true shows BLACK for a different reason:
  it looks for the last 8888 1280-wide target and this one is colour format 2 (2_10_10_10). So the repair is
  CONCRETE, not a missing feature: two post shaders output a constant green. NEXT: enumerate their inputs
  (slot report with the new surface filter on 14000500).
- POST CHAIN INPUTS (POSTREP_*, slot report filtered to surface 14000500; slots listed = the ones the HLSL reads):
  * C1B813C9 (x6/frame, colour 000A0000, blend 07060706 = alpha blend): slot 13 = 13DE0000 64x64 / 16080000
    512x512 fmt 20; slots 2/14/15 the 1x1 default - looks like HUD SPRITES (unconfirmed).
  * 916F6CC7 (x1/frame, colour 00020000, blend 0F0E0F0E): slot 13 = 1ECFF000 1280x720 fmt 6 - which the code's
    own comment identifies as the MIRROR of the front buffer FECFE000 ((base & 0x1FFFFFFF) + 0x1000) - and
    FECFE000 is RESOLVED FROM THIS SAME TARGET (14000500 / 00020000). So the pass READS ITS OWN PREVIOUS OUTPUT:
    a feedback loop - once green, green forever. Where the green first comes from, and which scene image the
    pass should be reading instead (the scene resolve F99DF000, fmt 32, from 14010500, is the candidate), are
    OPEN. Sized: a concrete, debuggable fault in how the post pass is fed - not a missing feature. Unblocking
    it would restore absolute parity, rows (j)/(k) and likely the HUD (item 7).
- SURFACE 14000500 = SLOT 8's SOURCE = THE POST CHAIN's TARGET (claudecode-76: one subject under two nouns). The
  7 draws/frame that showed "nobody writes 14000500's depth" (SURF14A: 916F6CC7 x1 + C1B813C9 x6, depth off) ARE
  the post chain. Cross-reference: rows "slot 8 / F2B1C000 / EDRAM depth view" and "post chain / FECFE000 /
  green" concern the SAME surface 14000500. The EDRAM census stands (post passes legitimately run depth-off).
- OPTION A SCOPE - UNASSESSED INTERACTION: A changes what the depth view of 14000500 (base 0x5A0, 720 tiles,
  wrapping to 0x000-0x06F) returns; the post chain writes COLOUR into 14000500 at colour base 0 (tiles
  0x000-0x2D0). The two ranges OVERLAP at tiles 0x000-0x06F on the console's EDRAM. Whether A's implementation
  interacts with the post pass (ordering within the frame, aliasing of those tiles) is NOT assessed - stated
  to the user as such.
- GREEN: "RAN AND FED BACK" vs "NEVER LANDED" - evidence so far: the target dump is labelled 7 draws taken and
  holds 48 distinct 12-bit colours (not one uniform placeholder), which leans to RAN; but no value-independent
  execution signal (issued AND passed state) has been checked, and a value comparison cannot see a draw that
  writes the same value. Discriminator still OPEN.
- OPTION A CHOSEN BY THE USER ("a", 2026-09-24). Design phase. Native constraints: (1) NO MSAA emulation - the
  2xMSAA scene 14010500 is a plain 1280x720 RGBA16F + D32_FLOAT target; (2) NO STENCIL - depth is D32_FLOAT
  only. Scene depth control decoded (census value 00708767): stencil ENABLED, z test GEQUAL + z write, stencil
  func ALWAYS, stencil op on z-pass = REPLACE (fail/zfail KEEP) - most scene draws stamp a per-material stencil
  reference into EDRAM. HYPOTHESIS (unproven, awaiting the SDK's resolve rules): the k_8 resolve F2B1C000 is the
  STENCIL byte of the scene's depth-stencil, so the terrain's 12B1D000 term is a per-material stencil mask -
  in which case option A needs native stencil (a scope increase, to be told to the user before building it).
- OPTION A DESIGN - THE SDK'S RULES (branch_src; draw.cpp:913-923,1084-1094,1122-1134; d3d12/render_target_cache
  .cpp:5157-5360; cache.cpp:842-920): a DEPTH resolve IGNORES the programmed destination format (our k_8) and
  always writes the raw 32-bit word (depth24 << 8 | stencil8) per pixel, no swap, no bias, sample 0 at 1x,
  via the fast 32bpp copy shader. The EDRAM content comes from the CURRENT OWNER of each tile, dumped in the
  owner's own layout and read back in the resolve's. For F2B1C000: span 0x5A0..0x7FF + 0x000..0x06F (720
  tiles), wholly inside the 2xMSAA scene depth at 0x400 (1440 tiles, 0x400..0x7FF + 0x000..0x19F): resolve
  pixel (x,y), y < 608 -> scene pixel (x, 208 + y/2), sample y & 1; y >= 608 -> scene pixels 512..567 (the
  wrap) - a 2x vertical stretch. Depth tiles also swap their 40-sample column halves.
  The GAME then FETCHES 12B1D000 as k_8 1280x720: under the SDK each texel is ONE BYTE of those 32-bit words,
  scrambled by the 8bpp-vs-32bpp tiling - what the SDK happens to produce, not a documented conversion. The
  plugin reference is therefore itself an approximation. Consistent with the data: WHITEADDR (term = 1)
  brings the ground to 0.97-0.98 of the plugin, i.e. the plugin's term reads ~1 almost everywhere (the top
  bytes of near-far reverse-Z depth are ~0xFF).
  CONSEQUENCE FOR THE USER: exact option A = replicate the SDK byte-for-byte (depth-owner dump with MSAA/wrap
  layout + 32bpp-written/8bpp-read tiling) - large, and it reproduces an emulator artefact; a cheap
  approximation (serve the unwritten depth view as white / derived from depth's top byte) already measures
  within ~2-3% of the plugin. The choice between them goes back to the user.
- USER CHOSE EXACT REPLICATION (2026-09-24, over the approximation and over pausing A): reproduce the SDK's
  depth resolve byte-for-byte natively. Option A's pre-registration STANDS AS WRITTEN - exact replication
  reconstructs 12B1D000 independently of the white substitution, so the tiers/windows are a real test
  (claudecode-76: under the approximation they would have been circular).
- LOSSY-VS-WRONG, WRITTEN BEFORE THE BUILD. Known, unavoidable losses natively: NO STENCIL (the low byte of
  every word is 0, where the scene's stencil REPLACE values sit), ONE SAMPLE per pixel (the guest's 2x rows
  alternate samples; natively both come from one value), D32_FLOAT -> 20e4 conversion rounding. So:
    LOSSY BUT CORRECT = the TIER ORDER survives and resolve census reports 0 unwritten, with magnitudes at or
      below the windows (any texel the game reads from a stencil byte is 0 natively - a darker, patterned
      shortfall, not a reordering).
    WRONG = tier order broken, or tiers 3-4 moving like tiers 1-2, or a subject rising ABOVE its cap.
  The implementation is behind a cvar so the control is the same binary; post-chain interaction (tiles
  0x000-0x06F, 112 of 720 = ~16% of the view; EDRAM wraps at 0x800) is an ORDER question - checked before
  judging.
- OPTION A VERDICTS, THREE (claudecode-76), CHECKED IN THIS ORDER BEFORE ANY PIXEL IS READ:
    NOT APPLIED - the resolve census does not reach 0 unwritten keys (or the cvar/binary did not take): nothing
      about the mechanism was tested.
    WRONG - tier order broken, tiers 3-4 moving like tiers 1-2, or a subject above its cap.
    LOSSY BUT CORRECT - order kept, each subject at or above its LOSSY FLOOR = window floor minus the summed loss
      estimate, not "anywhere below the window" (that excuse clause would let a fix that barely moves pass).
  LOSS SIZING (to finish before the build): no stencil = NEGLIGIBLE if the game's k_8 fetch reads a depth byte of
  the word, TOTAL if it reads the low (stencil) byte - decided by the fetch mapping, pending; one sample =
  bounded by geometry-edge pixels; 20e4 rounding = analytic, small.
- RESOLVE PARAMS ARE SWAPPED - A REPLAY-CORRECTNESS BUG (EDDEP1/EDDEP2, new live-register capture at the marker).
  The marker takes base/surf/flags from the FRONT of a queue (g_resolve_params, filled elsewhere) but copy
  control, copy dest and now RB_SURFACE/COLOR/DEPTH_INFO LIVE from the plugin's register file at the copy.
  The live sets are self-consistent; the queued labels are SWAPPED between two adjacent copies:
    queue "F2B1C000 from 14000500" -> live: dest 1A110000 fmt 6 endian 2, surf 14010500 colour 00030000 depth
      00010400 = the 2xMSAA SCENE DEPTH (base 0x400, D24FS8);
    queue "FA10F000 from 14010500" -> live: dest 12B1D000 fmt 2 (k_8) ENDIAN 0, surf 14000500 colour 0 depth
      000002D0 = base 0x2D0, D24S8, non-MSAA.
  So natively the scene-depth texture (FA10F000/1A110000, read by the depth restore D32D70C8 and fog/DOF) gets
  the EMPTY 14000500 source, and the k_8 texture gets the other. CORRECTION to option A's geometry: the k_8
  resolve reads DEPTH BASE 0x2D0 (not 0x5A0 - that came from the swapped label), D24S8, endian 0 (lowest byte =
  STENCIL). View 0x2D0..0x59F: shadow-map depth (10000410, base 0, to 0x34C), an unowned stretch 0x34D..0x3FF,
  and the scene depth 0x400..0x59F. The mechanism (a resolve source no native draw writes) stands; the specifics
  change. NEXT: count queued-vs-live mismatches over ALL resolves.
- RETRACTION - THE "SWAPPED PARAMS / REPLAY BUG" ABOVE IS WRONG IN DIRECTION. PAIR1 (pairing census over every
  resolve, ~79/frame): surface agrees except 1,200/window = exactly the 2 full-size depth copies a frame; dest
  "differs" 42/frame. And the QUEUED record for F2B1C000 (fetch 12B1D000, fmt 2, surf 14000500) matches, field
  for field, the LIVE set captured at the OTHER marker. So the queue pairing is RIGHT and the LIVE capture at the
  marker is ONE COPY AHEAD (the plugin's register file already holds the next copy's registers when the marker
  fires); the dest mismatches fit the same offset. NO native pairing bug. What survives: the self-consistent live
  set containing dest 12B1D000 - k_8, ENDIAN 0, surf 14000500, DEPTH BASE 0x2D0, D24S8 - is the k_8 copy's own.
  FLAG: RB_COPY_CONTROL (the clear fix, 2a4ead2) is read through the same marker capture, so its clear bits may
  also be one copy ahead - to check against the queue, not assumed.
- TILING INVERSE VERIFIED: the 32bpp inverse of GetTiledOffset2D is EXACT over all 1280x720 (brute force,
  tmp/tiling_check.py); a k_8 1280x720 fetch at the same base reads every byte lane equally (230,400 texels
  each) and never leaves the 32bpp resolve output.
- THE BYTE-LEVEL MODEL IS FALSIFIED BY DATA ALREADY IN HAND - EXACT REPLICATION NOT BUILT. With endian 0, lane 0
  = stencil (0 natively, unknown in the plugin), lanes 1-2 = the LOW depth bytes (near-random per pixel), lane 3
  = the top depth byte. Under that model the plugin's term would average well below 1 (~0.5-0.75) with a
  fine-scale dither, and native-white/plugin on the ground would be ~1.3-2. MEASURED: 0.97-0.98, replicated
  (WHITEADDR1/2) - the plugin's term is effectively 1 - and the plugin's terrain at 1:1 shows no regular dither
  (shots/2026-09-24_plugin_terrain_1to1_x3.png). So the SDK/plugin does NOT do what the source-level mapping
  concluded for this resolve (a texture-cache path serving resolve destinations differently, or a different
  copy shader, are candidates - unverified). Building "exact replication" would reproduce a behaviour the
  plugin demonstrably does not have. Back to the user.
  Also recorded (claudecode-76): the view's owners were shadow depth 17% / UNOWNED 25% / scene depth 58% - a
  fourth, larger loss; moot unless the model is revived. The 16% overlap figure (from base 0x5A0) is VOID.
- ONE-COPY-AHEAD CAPTURE IS A CLASS: fields read at the marker from live registers - RB_COPY_CONTROL (the clear
  fix), RB_COPY_DEST_*, RB_SURFACE/COLOR/DEPTH_INFO - are all suspect unless matched by destination (the new
  LiveCopyFor ring). The clear fix's clear bits remain UNCHECKED against the queue.
- USER CHOSE "MATCH WHAT'S MEASURED" (2026-09-24, after being told exact replication had lost its definition):
  a DEPTH resolve whose source NEVER had a native writer is served as fully lit (white, 1.0) wherever it is
  bound, behind ngpu_edram_depth (off = the previous behaviour, same binary). Honest framing kept: the
  approximation is empirically excellent (0.97-0.98, replicated) and there is NO MECHANISM for why the plugin's
  term reads ~1.
- TEST FOR THE APPROXIMATION, PRE-REGISTERED (the tiers/windows were calibrated FROM the white legs, so for this
  fix they would be circular - not used): (1) NOT APPLIED first: its served-white counter must be > 0 and the
  resolve census still names exactly the one unwritten key; (2) EQUIVALENCE: every parity region must match
  WHITEADDR1/2 (the address-specific intervention) within the true noise (0.002; sky 0.005); (3) NO SIDE EFFECT:
  nothing outside what WHITEADDR changed may change. Failure of (2)/(3) = the general rule catches more than
  12B1D000 or less.
- CONSTRAINTS ANY FUTURE MODEL OF THE PLUGIN'S TERM MUST MEET (claudecode-76; written before a candidate): mean
  ~1 over the terrain; NO DITHER at 1:1 (the leg no tuning can rescue); white substitution landing at
  0.97-0.98 of the plugin. The falsified byte-lane model fails all three on sight.
- WHAT DIED AND WHAT STANDS: FALSIFIED - the byte-level meaning (lanes of depth24<<8|stencil). STANDS - the
  tiling inverse (address mapping, brute-force exact) and the EDRAM diagnosis (resolve census + interventions).
- OPTION A (measured approximation) RESULT - OPTA1/OPTA2, same binary, ngpu_edram_depth=true:
  (1) APPLIED: 1 unwritten depth-resolve destination, 4,699,362 / 4,729,279 slot bindings served white; the
      resolve census still names exactly 1 unwritten SOURCE (the fix acts on the destination, as designed).
  (2) EQUIVALENCE vs the white legs, per region (WHITEADDR1 / WHITEADDR2 | OPTA1 / OPTA2):
      terrain 0.969/0.969 | 0.969/0.969; near ground 0.984/0.984 | 0.985/0.984; ferns 0.676/0.676 | 0.677/0.677;
      hills 0.822/0.819 | 0.819/0.819; sky 0.841/0.842 | 0.837/0.842; island 0.862/0.853 | 0.853/0.853;
      arches 0.808/0.795 | 0.796/0.795; lake region 0.808/0.800 | 0.800/0.800.
      OPTA agrees with WHITEADDR2 to <= 0.001 in EVERY region. Against the MEAN of both white legs the
      pre-registered 0.002 floor FAILS in island (0.005), arches (0.007) and lake region (0.004): WHITEADDR1
      itself reads 0.009-0.013 higher there - the far-scene excursion shape (island/arches/lake up, hills
      small, ground flat). Recorded as a strict FAIL of the mean-based criterion, explained by one reference
      leg, NOT as a clean pass.
  (3) NO SIDE EFFECT: nothing moved beyond what the address-specific white substitution moved.
  Net: the general rule reproduces the white intervention. Terrain 0.51 -> 0.97, near ground 0.52 -> 0.98,
  ferns 0.37 -> 0.68, arches ~0.58 -> ~0.80, lake region ~0.53 -> ~0.80 of the plugin (cross-path ratios).
  ngpu_edram_depth stays DEFAULT OFF until the user decides; the depth-ownership claim code rides on the same
  cvar and is inert otherwise.
- CLEAR FIX VERIFIED (CLRPAIR1; first in the one-copy-ahead sweep, as the most load-bearing field): in EVERY
  resolve (47,400 / 48,564 / 47,934 per window, 0 unmatched) the clear bits the clear fix reads at the marker
  AGREE with the copy's OWN destination-matched RB_COPY_CONTROL - 0 spurious clears, 0 missed. The value used is
  the correct value regardless of the capture's offset; every native capture downstream of the clears stands.
- OPTION A RECORD, FOUR CORRECTIONS (claudecode-76):
  1. The pre-registered "APPLIED = resolve census reaches 0 unwritten" was designed for EXACT replication (which
     would fill the source). It is INAPPLICABLE to the approximation, which substitutes the SAMPLE and never
     writes the source - the census correctly still names 1 unwritten. APPLIED for this branch = the served-white
     binding counter (4.7M per leg). A reader seeing "census still names the source" is not looking at a failed build.
  2. SELF-CONTRADICTION RESOLVED - WITHDRAWN: the earlier "neither WHITEADDR leg carries the far-scene
     signature; the hills didn't show +0.022". It tested the signature at a WEAK point (hills, +0.022 in RTLIST1)
     instead of its PEAK (lake +0.049, island +0.050). WHITEADDR1 reads 0.009-0.013 above WHITEADDR2 on island /
     arches / lake - it DOES carry a far-scene excursion. Rule: test a signature at its strongest point.
  3. OPTION A WINDOWS FROM WHITEADDR2 ALONE (vs the two-leg mean): island +0.046 (was 0.0505), arches +0.212 (was
     0.2185), lake region +0.270 (was 0.274) - the windows move by <= 0.0065, closing the point; the
     approximation's own test does not use them (it is an equivalence test), so no verdict changes.
  4. WHY THE STRICT FAIL IS ACCEPTED AS EXPLAINED, NOT EXCUSED: OPTA matches WHITEADDR2 to <= 0.001 in EVERY
     region - a genuine implementation error would not land inside a thousandth of one specific reference leg
     while missing the mean. That is the reason, not a story alongside it.
- OPTION A ON BY DEFAULT (the user's answer, 2026-09-24). Verified on the artifact AND in a leg with NO override
  (OPTADEF1): the log reads "ngpu_edram_depth true", 4,730,523 bindings served white, terrain 0.969 (= OPTA; the
  off-state is 0.513). The user then left for the night: "work fully autonomously, always choose recommended
  option" - decisions below are taken as the recommended option and recorded here with their reasons; the
  standing limits (no push, lock, saves, own PIDs, pre-registration) are unchanged.
- ISLAND WINDOW, per subject (claudecode-76 asked which subject the <=0.0065 belonged to): lake 0.274 -> 0.270
  (0.004), arches 0.2185 -> 0.212 (0.0065), island 0.0505 -> 0.046 (0.0045; its floor 0.0126 -> 0.0115, cap
  0.0525 -> 0.048). The island moves by about a tenth of its window - closed, now stated per subject.
- RTLIST1 / FAR-SCENE EXCURSION LOCATED (the WHITEADDR1/2 natural control pair: identical treatment, one carries
  it). Shape check first: WHITEADDR1 - WHITEADDR2 = ground 0, arches +0.013, island +0.009, lake region +0.008,
  hills +0.003, sky ~0 - the pre-registered mid-distance peak with ~0 at both ends: SAME PHENOMENON as RTLIST1.
  Where (native luminance difference map, shots/2026-09-24_WHITEADDR1_minus_WHITEADDR2_native_luminance_x8.png):
  concentrated on the LAKE SURFACE (rows 278-338: mean +2.15, 44.5% of pixels changed by >3), plus tree canopies
  (sway) and a small patch at the hero; ground rows 398-758 ~0 (<0.4% changed). The arches / island / lake-region
  rectangles all contain lake water; the ground regions contain none - which is why the excursion peaks mid-distance.
  NATIVE-SPECIFIC: both windows' lake pixels change between captures (the water animates), but only the NATIVE lake
  MEAN shifts; the plugin's regional means are identical across legs (to 0.1). So the native water's average
  brightness varies with the capture moment. Candidate link (untested): the "lake flash" noted for Fable II.
  CONSEQUENCE FOR MEASUREMENT: regions containing lake water carry a native, moment-dependent term of up to ~0.05;
  the n>=2 rule and lower-value scoring already guard option A against it. Row RTLIST1 -> renamed "native lake
  brightness varies between captures", open, with the lake's shader still unidentified.
- AUTONOMY GRANT - PROVENANCE (claudecode-76 cannot see mid-turn user messages and asked; recorded for the next
  reader). It arrived as a USER MESSAGE TYPED DURING MY TURN (the harness surfaces those mid-turn, not as a
  separate turn), verbatim: "I will be out for the night work fully autonomously, always choose recommended
  option." Followed later by, verbatim: "That looked great, a lot of progress!!". The option-A choices
  ("Exact replication", "Match what's measured", "On by default") came through the question widget.
  LIMITS FIRST: the grant widens WHO DECIDES among options put to the user; it does NOT touch the standing limits -
  nothing pushed or published, no launch without the lock carrying a real until=, saves verified by content after
  every leg, only my own PIDs, pre-registration before judging.
- CORRECTION - RTLIST1 AND THE WHITEADDR1/2 PAIR ARE NOT ONE PHENOMENON (claudecode-76: rank order / scaling
  disagree - arches is ~2x out of line; my shape constraint was too loose to separate two mid-peaked shapes).
  Map check (shots/2026-09-24_RTLIST1_minus_PGEN1_native_luminance_x8.png): RTLIST1 - PGEN1 is a UNIFORM lift of
  the WHOLE LAKE SURFACE (rows 278-338 +5.81, 59.9%) and of the BLOCKY TREE IMPOSTOR cards (rows 38-218 +2.8..+4.0),
  plus the hero; ground rows exactly 0.00. WHITEADDR1 - WHITEADDR2 is mostly SIGNED, animation-like noise on the lake
  with a small positive bias. So: RTLIST1 = a systematic brightening of TWO MATERIALS (lake water, tree impostors)
  in that leg; the white pair is NOT a validated control for it. The "WHITEADDR1 carries the far-scene excursion"
  wording above is narrowed to "WHITEADDR1 reads 0.008-0.013 above WHITEADDR2 on far-scene regions, largely lake
  animation"; its role as the explanation of option A's strict mean-based fail stands on the <= 0.001 match to
  WHITEADDR2, not on it being RTLIST1. Shape constraints tightened: rank order + approximate proportionality, as
  for option A's tiers.
- RTLIST1 CONSTRAINT WITHDRAWN AND REPLACED (claudecode-76): the "mid-distance peak, ~0 at both ends" shape was an
  ARTEFACT OF REGION SAMPLING - a lift of two SURFACES (lake water, tree impostor cards) seen through distance-ordered
  rectangles. The old profile numbers remain true as measurements and false as a description. NEW constraint: the
  cause must lift the LAKE SURFACE and the TREE IMPOSTOR CARDS uniformly and nothing else. Cross-reference: the
  impostor cards also head open item (4) (blocky distant trees, likely the deliberate ngpu_skip_impostor gap,
  unconfirmed). Rule (third instance tonight): region sampling turns surface-specific effects into spatial
  gradients - measure by surface id where one exists; the rectangle is presentation only.
- FRAME TRACE (new ngpu_frame_trace; docs/native_gpu/FRAME_TRACE_bowerlake_f4800.txt, 287 runs in draw order). The
  frame ends: scene on 14010500 (colour 00030000 = fmt 3 2_10_10_10_FLOAT, and 000C0000 = fmt 12
  2_10_10_10_FLOAT_AS_16_16_16_16), colour BASE 0 -> small post targets 05000140 / 0A000280 (D32D70C8 depth restore,
  77630897, ACB18EE7, 122DE50E, 7D59AFCA, 7A850C75 x6) -> 14000500/000A0000 (fmt 10 2_10_10_10_AS_10_10_10_10)
  C1B813C9 x6 alpha blend -> 14000500/00020000 (fmt 2 k_2_10_10_10, UNORM) 916F6CC7 x1, blend 0F0E0F0E =
  src*CONSTANT_ALPHA + dst*(1-CONSTANT_ALPHA) with src = the previous front buffer (1ECFF000).
- HYPOTHESIS, PRE-REGISTERED BEFORE ANY BUILD (it arrived exactly when wanted, so it is suspect until measured):
  the post views (fmt 10 / 2, UNORM 10-bit) sit on the SAME EDRAM colour base 0 as the HDR scene (fmt 3 / 12,
  7e3 FLOAT). On the console the unorm views read the scene's 7e3 BITS as unorm - a log-like curve: the game's
  TONEMAP BY REINTERPRETATION. Natively every view is its own RGBA16F target, so it never happens: the post target
  starts empty (the green), and the native window shows its own Reinhard instead. PREDICTS: take the native HDR
  scene values, encode as 7e3, read the bits as unorm10 - the result matches the plugin window far better than
  the native Reinhard does, in particular sky/hills (rows j/k, ~0.82 now) should move toward 1.0. If it does not,
  the hypothesis is wrong. Test is offline: a raw float16 dump of the scene target.
- TONEMAP-BY-REINTERPRETATION: REFUTED (RAWDUMP1; new raw RGBA16F colour dump under ngpu_truth_keep;
  tools/native_gpu/reinterp_test.py). The native HDR scene (14010500/00030000, frame 4800; luminance p10/p50/p90/
  p99 = 0.45 / 0.84 / 1.57 / 2.98) encoded as 7e3 and read as UNORM10 -> 8-bit, compared with the plugin window,
  region by region (native-window/plugin | reinterpreted/plugin): sky 0.839 | 0.692, hills 0.820 | 0.608, island
  0.855 | 0.629, arches 0.799 | 0.591, lake region 0.804 | 0.604, ferns 0.676 | 0.495, near ground 0.985 | 0.725.
  The pre-registered prediction (toward 1.0, especially sky/hills) FAILS - every region moves AWAY from the plugin
  (e.g. HDR 0.84 -> 7e3 bits 343 -> unorm 0.335 -> 85 of 255, vs the plugin's ~100-107 there). The post views
  sharing EDRAM base 0 with the scene remain a FACT; this particular reading of them is dead. The green post
  target stays open.
- NEW ROW (l): NaN PIXELS IN THE NATIVE HDR SCENE - 2.47% of the 14010500/00030000 target at frame 4800, starting
  at row ~363 (the shoreline band). NaNs propagate through blends and any tonemap; source unidentified.
- THE BLACK FERNS ARE NaN - ROOT-CAUSED TO A TRANSLATOR SEMANTICS GAP.
  * The NaN pixels (2.47% of the HDR scene) ARE the near fern clumps: 99.8% of them are black in the native window,
    and the overlay lies exactly on the ferns (not on the lake - so row (l) and RTLIST1's lake lift are separate).
  * FERNNAN1 (only PS A92D4C17 drawn, raw dump): 38,628 pixels written, 24,062 (62%) with NaN COLOUR and a finite
    alpha - so the native alpha test (clip(oC0.w - 0.502); state: RB_COLORCONTROL 8700001C = test ON, func GREATER,
    ALPHA-TO-MASK on) passes them, and they land as black. (The alpha-test flag IS set correctly in replay: Reg(0)
    reads the replay registers.)
  * CLASS: Xenos (Shader Model 3) multiplies treat +-0 x ANYTHING as +0 - the SDK's DXBC translator implements it at
    every mul/mad/dot (dxbc_translator_alu.cpp:95,196,532,625,661,894). XenosRecomp emits plain IEEE "a * b" /
    dot(): a clamped rsqrt of a zero-length vector (FLT_MAX) times 0 then feeds inf - inf = NaN.
  * FIX (XenosRecomp + the shared header, local): xmul/xdot helpers in fable2_shader_common.h (select(or(|a|,|b| <
    FLT_MIN), 0, a*b); xdot = dot(xmul(a,b), 1)), emitted for Mul, Mad, Dp3/Dp4, Dp2Add, Muls, MulsPrev. Offline:
    80 of 88 containers compile; the 8 failures are the bool/loop constants FixHlsl rewrites in-game + CFB18804.
  * XMUL1 PRE-REGISTERED (all shaders re-translated; control = the pre-xmul cache with JIT off, same binary):
    PREDICTS the fern-only NaN count falls from 24,062 to ~0 and the ferns' parity rises from 0.676 (option A
    on) toward the plugin; NON-REGRESSION: terrain / near ground / arches / lake region / hills / sky within their
    floors of the option-A values (0.969 / 0.985 / 0.80 / 0.80 / 0.82 / 0.84). A NaN count that does not fall =
    NaN comes from somewhere else.
- XMUL1 RESULT - PREDICTION NOT MET. The re-translation APPLIED (A92D4C17's new DXIL differs from the old, its HLSL
  uses xmul/xdot 56 times; 91 shaders re-translated over XMUL0a-c, only CFB18804 fails) - yet the fern-only NaN
  count is UNCHANGED at 24,062 of 38,628 (XMUL1F), and the full scene holds 22,844 NaN (XMUL1). The multiply
  semantics are kept (they match the SDK) but are NOT the NaN source.
- NEXT CANDIDATE, PRE-REGISTERED: OVERFLOW TO INF, THEN THE BLEND. Facts: fern-only mode writes 0 inf and 24k NaN;
  FERNWHITE0 (albedo white) gave finite crimson/tan cards; the fern PS divides albedo by alpha^2 through a
  FLT_MAX-clamped rcp; the fern blend is src*1 + dst*0; the console's scene target is 7e3 (saturates at 31.875),
  the native one RGBA16F (overflows to inf). An overlapping fern draw then computes src + inf*0 = NaN. TEST: clamp
  every PS colour export to half range (XenosRecomp, "oC0 = clamp(oC0, -65504, 65504)" before the alpha test;
  all PS re-translated). PREDICTS the fern-only NaN count falls to ~0. Unchanged = not the mechanism.
- CLAMP RESULT - MECHANISM CONFIRMED, FERNS NOT FIXED. With PS colour exports clamped to half range (all PS
  re-translated, CLAMP0a/b; only CFB18804 fails): fern-only (CLAMP1F) writes the same 38,628 px with 0 NaN (was
  24,062), 0 inf, 0 above 31.875 - overflow-to-inf-then-blend WAS the NaN mechanism. But the ferns are still black:
  those 24,062 px now hold GREEN = -65504 (clamped from -inf) with red/blue small and finite (0.03-0.14) - the
  poison is ONE CHANNEL, NEGATIVE infinity. Fern region parity unchanged at 0.676 (CLAMP1/2).
- NOT CONSTANTS: the whole pixel bank for the fern draw (FERNCONST1, new non-finite-constant census, count always
  printed) holds exactly 1 non-finite value, c129.z = FFFFFFFF - and A92D4C17 reads only c0..c87. Excluded.
- FAR-SCENE LIFT STRUCK TWO LEGS: XMULC (the pre-xmul cache, same binary, JIT off - i.e. the OPTA cache) and XMUL2
  both read arches ~0.89, lake region ~0.94, island ~0.90-0.92 against ~0.80 in OPTA2 and CLAMP1/2; ground, terrain,
  ferns identical. Same class as RTLIST1 (native lake + impostor lift), larger (+0.1). Cannot be attributed to
  xmul or the clamp (the control leg shows it too). The n>=2 / lower-value rule is what keeps it out of verdicts.
- IN PROGRESS: fern-only slot sweep (one texture slot served white at a time: 0,1,4,5,8,10), counting green
  <= -60000 px, to find which input poisons the green channel.
- TRANSLATOR BUG CLASS FOUND WHILE READING D4686D15: XenosRecomp emits Log/Logc, Rcp/Rcpc/Rcpf and Rsq/Rsqc/Rsqf ALL as
  clamp(op(x), FLT_MIN, FLT_MAX) - FLT_MIN is the smallest POSITIVE float, so every NEGATIVE result is clamped to
  ~+1e-38: log2(x < 1) -> ~0, so every pow built as exp2(y * log2(x)) returns 1; a negative rcp turns positive. The
  SDK (dxbc_translator_alu.cpp:720-775): Log/Rcp/Rsq plain IEEE; Logc: only -inf -> -FLT_MAX; Rcpc/Rsqc: +-inf ->
  +-FLT_MAX; Rcpf/Rsqf: +-inf -> +-0. PRE-REGISTERED (LOGFIX): all shaders re-translated with the SDK's semantics.
  Expected: a SCENE-WIDE change (pow-based lighting/specular/fog touch most materials), direction not predictable
  from the code; VERDICT RULE written now: parity moves TOWARD the plugin in more regions than away, and no region
  moves away by more than its floor x 5 - otherwise the fix is recorded as a regression and investigated, not kept
  on faith. NaN count must stay ~0 (the export clamp remains).
- RETRACTION (same hour): "XenosRecomp clamps to [FLT_MIN, FLT_MAX] and destroys negative results" is WRONG - the
  shared header defines FLT_MIN as asfloat(0xff7fffff) = -FLT_MAX, so the emitted clamp is [-FLT_MAX, FLT_MAX] and
  negatives survive; pow() is NOT broken. The LOGFIX pre-registration's premise is void. What REMAINS true and
  smaller: XenosRecomp gives every variant that clamp, while the SDK flushes Rcpf/Rsqf +-inf to +-0 (XenosRecomp:
  +-FLT_MAX) and leaves plain Log/Rcp/Rsq IEEE. The +-FLT_MAX-where-hardware-gives-0 difference is a candidate
  source of the huge values that overflow a channel. NARROWED TEST (RCPF1): per-opcode exact semantics (xlogc /
  xrcpc / xrsqc / xrcpf / xrsqf helpers, plain log2/rcp/rsqrt for the IEEE ops); PREDICTS the fern green -inf
  (24,062 px at -65504 after the export clamp) falls if the ferns' shaders use Rcpf/Rsqf; unchanged = not this.
- THE GREEN POISON IS ONE VERTEX SHADER (FVS_*, only_vs + raw dump): D4686D15 (the COLLISION-SPHERE vegetation VS -
  g_CollisionSpheres, g_CollisionPivotPointHeight - bending near the player) writes 24,771 px in rows 363-498 and
  ALL 24,771 have green at -65504; 6E1021DB (mid-distance ferns, rows 223-433) writes 17,740 with 0; 6B210267 draws
  nothing at this stand. Its code computes rcp(g_CollisionSpheres(k + a0).w) and rcp(g_CollisionPivotPointHeight.x)
  in a loop over spheres (loop constant i0) - a zero radius / height is the obvious suspect.
- PER-OPCODE SCALAR SEMANTICS (RCPF1) did NOT remove the poison (D4686D15-only: 24,771 / 24,771 green NaN) - kept
  (matches the SDK). The NaN LITERAL c255 (0x7FE00000) in D4686D15 is not it either (NANLIT1: zeroed, unchanged).
- INTERPOLANT BISECTION (single-shader DXIL variants of D4686D15, provenance-checked - the JIT's HLSL recompiles
  byte-identically to the cached DXIL - installed with backup, JIT off, restored after): zero oTexCoord0-3 -> 0 px
  written (the uv/alpha); 4-7 -> 24,771 px, 0 green NaN; 4-5 -> all NaN; 6-7 -> 0; 6 -> all NaN; 7 -> 0. THE POISON
  IS oTexCoord7 (= r8, the per-instance colour: LightmapFactorAndGamma.xyz * exp2(gamma * log2|col|)).
- ROOT CAUSE - A TRANSLATOR CO-ISSUE BUG: in one Xenos ALU instruction the vector and scalar halves run in parallel
  and read their sources BEFORE either writes. XenosRecomp emits the vector write first, so a scalar op reading the
  vector's destination sees the NEW value. D4686D15: "r6.xyz = r3.yzw + g_WorldPosition...; ps = exp2(r6.y)" - the
  scalar should read the OLD r6.y (gamma*log2|green|) but reads the world position y (~140): exp2(140) = inf ->
  r8.y inf -> the near ferns' green channel NaN.
  FIX (XenosRecomp): when the vector half writes a temp register the scalar half reads, emit "rsnap = rN;" before the
  vector write and point the scalar operand at rsnap (float4 rsnap declared in the prologue beside ps). Census
  (offline, all containers): the hazard is WIDESPREAD - CFB18804 21, 0CD26829 12, 6B210267 10, 6E1021DB 8, 1D2667F2
  8, E6415E83 7, D4686D15 6, 0EF7C66A (terrain) 6, 3EA51176 6, the water pair B9B8BECA/E33859F0 5, ... - so this can
  change many materials at once.
  COISSUE1 PRE-REGISTERED (all shaders re-translated; comparison legs CLAMP1/2 = same binary, pre-fix cache):
  PREDICTS D4686D15-only green NaN 24,771 -> ~0 and the ferns' parity rises from 0.676. SCENE-WIDE VERDICT RULE:
  parity moves toward the plugin in more regions than away, and no region moves away by more than 5x its floor;
  otherwise recorded as a regression and investigated.
- COISSUE1 RESULT - PREDICTION MET, SCORED ON THE LOWER VALUE AS PRE-REGISTERED. (91 shaders re-translated over
  COISS0a-c, only CFB18804 fails; D4686D15's HLSL carries 6 snapshots.) D4686D15-only (COISS1V): 24,771 px written,
  green NaN 24,771 -> 0; its colour is now a dim red-brown in HDR (mean 0.068 / 0.021 / 0.002; x c20.w ~7.5 =
  orange-brown). Scene (CLAMP1/2 -> lower of COISS1/COISS2): ferns 0.676 -> 0.693 (+0.017, 17x floor, both legs),
  terrain 0.969 -> 0.971, near ground 0.985 -> 0.984, sky/hills/island/arches/lake 0.000. Verdict rule: toward in 1
  region beyond 5x floor, away in 0 -> PASS. (A naive two-leg mean would have credited COISS1's far-scene lift - arches
  0.897, lake 0.939 - to the fix; the lower-value rule is what excluded it.)
  STILL WRONG on the ferns: dark silhouettes, FAR LARGER than the plugin's fronds (shots/..._COISS1_native_vs_plugin.png)
  - the SIZE is a separate open item (alpha test / alpha-to-mask, or instances the game kills that are drawn natively).
- THE "FAR-SCENE LIFT" IS THE WATER SOMETIMES RENDERING NEARLY RIGHT: in COISS1 (a lift leg) the native lake is light
  blue and close to the plugin's; in the dull legs it is teal. Legs carrying it so far: RTLIST1, XMULC, XMUL2, COISS1
  (4 of ~16) - intermittent, not binary-dependent. A lead on item 5 (water dull): some water input is present in
  some frames and absent/stale in most (a resolve ordering or a race is the obvious family; unmeasured).
- LOWER-VALUE RULE HAS PAID: on its first live use (COISSUE1) it excluded COISS1's far-scene excursion that a two-leg
  mean would have credited to the fix.
- PRE- vs POST-CO-ISSUE NUMBERS: every figure recorded before COISSUE1 (sensitivity table, floors, option A windows
  and verification, parity table) was measured on PRE-fix translations. COISSUE1 (lower-value) moved terrain +0.002,
  near ground -0.001, sky/hills/island/arches/lake 0.000 and ferns +0.017 - so outside the ferns those numbers stand
  within their floors ("not obviously changed", not "proven unchanged"); the ferns' figures are pre-fix and superseded.
  Option A's verification is NOT re-run: its subjects moved <= 1x floor except the ferns.
- SWZ1 (instance-matrix rows loaded .xyzw instead of the microcode's .yxwz): 7,783 px, moved to the far right (x
  963-1279) - nowhere near the plugin's ferns. REFUTED; the microcode swizzle is right. Fern SIZE remains open.
- WATER STATE, EVERY LEG CLASSIFIED (lake region native/plugin; good > 0.88): of 85 full-scene legs, water GOOD
  (~0.94) in 7 - CLAMP0b, COISS0b, COISS1, FTRACE1, RCPF0c, XMUL2, XMULC - all from the last few hours; DULL in 77
  (clusters at ~0.45, ~0.53, ~0.80 track the build/cache eras). RTLIST1 is DULL at this threshold (0.58 - a smaller
  lift). So:
    * THE ROW IS INVERTED (claudecode-76): the dull water is the DEFECT and the good legs are the correct rendering;
      the question is "why is the water wrong in most legs", and the surface constraint now describes the CORRECT
      rendering's footprint, not a defect's.
    * THE CO-ISSUE BUG IS NOT THE WATER'S CAUSE: post-fix, COISS1 good / COISS2 dull - not 2/2.
    * GROUPED NUMBERS: the five-leg floor (PGEN1/DEPTHOUT1/RESCEN1/SURF14A/SURF14B) is all DULL - clean. WHITEADDR1/2,
      OPTA1/2, CLAMP1/2, COISS2 are DULL; XMULC, XMUL2, COISS1 are GOOD - any statistic mixing them mixes two
      populations and must say so.
- WATER FLIPS WITHIN A RUN (SERIES1/2: new bridge_leg_series.sh, native+plugin every 5 s through the stand; lake region
  native/plugin): SERIES1 0.93 0.78 0.79 0.80 0.80 0.80 0.81 0.82 0.82 0.83; SERIES2 0.92 0.79 0.79 0.80 0.80 0.80
  0.81 0.82 0.83 0.82 (10 of 12 captures each). GOOD at the first sample (~5 s into the stand), DULL from ~10 s on,
  creeping up slowly. The "good legs" are those whose single +30 s capture happened before the degradation - so the
  water is a STATE CHANGE a few seconds into the stand, not a per-run coin flip; timing differences between builds /
  diagnostics explain why good captures cluster in recent legs. Corrections (claudecode-76): "the co-issue bug is
  excluded" is narrowed to "NOT SUFFICIENT"; with the within-run flip, necessity is moot - the water is correct at
  first and then degrades in every run observed. The classification's 85th leg is ONLY_63081965 (sky-only) -
  UNCLASSIFIABLE, reported (8 "good" in the listing included it; 7 real + 77 dull + 1 unclassifiable = 85).
  NEXT: identify the lake's PS (B9B8BECA alone drew nothing visible), then log its bindings over time to find what
  changes at the flip.
- LAKE IDENTIFIED + THE FLIP IS A MATERIAL SWITCH. The lake surface is PS E33859F0: alone it covers 100% of the lake
  region and renders LIGHT BLUE (181,209,215) - its own output is fine. Without it (NOLAKE1) the lake bed is lit sandy
  terrain (~113), steady over time - the "dark bed" idea is refuted. Its bindings on the scene target CHANGE during the
  run (LAKEREP1): early slot13/14 134F3000/13533000, slot2 13E30000 128x128 (DXN), slot4 1FAB1000 (64x1 fmt 29),
  slot5 1FAB0000 (256x1) -> later 13403000/13443000, 14D60000 256x256, 1FAB3000, 1FAB2000. The game switches the water
  material's textures/LUTs a few seconds in, and natively one of the LATER inputs renders dull. It also reads slot 8 =
  12B1D000 (the option-A term). TEST (WATSLOT*): each later slot served white for E33859F0 only, lake region read after
  the flip; the slot whose whitening restores ~0.93 is the one that decodes wrong.
- WATER: THE REFLECTION INPUT. Per-slot white for the lake PS E33859F0 (after the flip; lake region; dull ~0.80,
  good ~0.93): slot 14 -> 0.936 (REPRODUCES the good state), slot 13 -> 1.227 (overbright), slot 4 -> 0.876, slot 5
  -> 0.743, slot 2 -> 0.606. All four swapping textures are NATIVE RESOLVES (the fetch-form "RESOLVED" marker in the
  slot report never matches - it compares fetch form with base-form resolve pages; noted): slot 14 = F3442000 /
  F3532000, 256x256 from the 280x280 4xMSAA target 04020118 (the REFLECTION pass: 9A193FEB, 52FE118F and the sky
  63081965 x3 per the frame trace); slot 13 = F3402000 / F34F2000, 256x256 from the 320x180 target 05000140.
  REFL1 (new ngpu_dump_res_min_w=256): the slot-14 resolves are NOT black - 100% lit, mean (111,115,106), HDR p50
  0.156 p90 0.359, early and late IDENTICAL; the black 04020118 target dump was DUMP TIMING (frame end, after the
  resolve's clear-after-copy). The reflection pass runs at exposure c20.w = 1 (vs 7.5 in the scene, SKYREP1) - by
  design as far as can be told. Consistent account, UNPROVEN: the early "good" water is the white placeholder served
  before the first reflection resolve lands (white = 0.936), and the dull water is the native reflection content,
  dimmer than the plugin's. NEXT: what the water PS does with slot 14, and whether the native reflection is dimmer
  than it should be (its 52FE118F content carries row (k)'s ~0.82 deficit too).
- THE B9B8BECA RETRACTION WALKED IN REVERSE (claudecode-76: its test isolated B9B8BECA and retired a pair). E33859F0
  IS the lake and binds 12B1D000 at slot 8 (LAKEREP1). So the three conclusions that died stay dead, for the RIGHT
  reason now: the lake is a slot-8 READER (no "separate water darkening"; no control; not the complement's
  non-reader). What was wrong was the stated basis ("those shaders aren't the lake") and the transform regex missing
  E33859F0's form of the lookup. Nothing to restore.
- LEG LABEL: "water state" is replaced by CAPTURE TIME WITHIN THE RUN - the state is a function of when the capture was
  taken (good for the first ~5 s of the stand, dull after).
- PLACEHOLDER-TIMING TEST, PRE-REGISTERED TOLERANCE "the first reflection resolve lands between the s00 and s01 captures":
  FAILS. SERIES1: first F3442000 registration 21:35:27.2; s00 (GOOD) 21:35:33.5; s01 (dull) 21:35:40.0. The resolve
  existed while the water was still good.
  NEW HYPOTHESIS (not a rescue; its own test pending): LAKEREP1 bound F3532000/F34F2000 EARLY and F3442000/F3402000
  LATER; F34F2000 first appears only at 21:36:15 (after the flip). So the early-bound pair did not exist yet
  (placeholder -> good) and the later pair did (native reflection content -> dull): the flip = the GAME SWITCHING ITS
  BINDING between two reflection buffers, not a resolve landing. TEST: time-stamp the lake PS's slot-14 base per
  window and check the switch falls between s00 and s01.
- CROSS-REFERENCE: the reflection pass is 280x280 4xMSAA and the native path has NO MSAA emulation - a 4-sample source
  reconstructed single-sample; whether that dims it is unmeasured. Measure the reflection's own brightness native vs
  plugin directly rather than inferring it from the lake.
- DUMP-TIMING, third instance tonight: a dump is a claim about the moment it was taken (the black 04020118 dump was taken
  after the resolve's clear).
- TIMING TEST RE-STATUS (claudecode-76): the "fail" above tested F3442000 - the LATE buffer, not the one bound during the
  good window - so it did not address the hypothesis: VOID, not a kill. Re-registered as ONE hypothesis, resources named
  by ID: during the good window slot 14 = 13533000 (F3532000), not yet produced -> placeholder read; the flip is the
  EARLIER of (a) slot 14 switching to 13443000 (F3442000, produced) or (b) F3532000's first registration. PREDICTS that
  event falls between the s00 and s01 capture times of a series leg (SLOTSERIES1, new uncapped SLOT CHANGE log).
  Rule taken: a test named by ROLE ("the reflection resolve") inherits the role's ambiguity - name the resource id.
- SLOTSERIES1 - VOID (COVERAGE), WITH ONE NEW FACT. Captures: s00 22:04:42.0 (lake 0.92, good), s01 22:04:48.5 (0.79,
  dull) - the flip reproduced. But the slot report matched NO lake draw (E33859F0 on 14010500) until frame 4511
  (22:05:36) and logged only ONE window line (22:05:41, 239,200 matched / 40 printed), although its frames > 900 gate
  should open ~22:04:45. So the instrument did not see the lake during the flip window - its silence there is not
  evidence; the pre-registered test is VOID, not decided. Why the matches start so late is itself unexplained (the
  lake draws on another surface key early? the report's own gating?) - to be found before re-running.
  NEW FACT (from the first logged frame): slot 14 CYCLES among >= 4 reflection buffers WITHIN ONE FRAME (13443000 ->
  134C3000 -> 13543000 -> 13443000 ..., slot 13 likewise: 13403000 / 13483000 / 13503000), i.e. per water draw. The
  "early pair vs late pair" of LAKEREP1 was a per-window SAMPLING artefact; there is no single binding switch.
- SLOTSERIES1 VOID WITHDRAWN (the VOID was itself wrong on its basis): the log had ROTATED into SLOTSERIES1.1-.5.log;
  the lake was seen from frame 998 (22:04:37.4) in .5. The matches did not start late; I read one file of six.
  SURVIVES/DIES on per-window slot samples (claudecode-76's split, adopted): DIES - "early pair / late pair" and the
  single-switch story; DIES AS A SINGULAR IDENTIFICATION - "slot 14 = the 256x256 resolve of the reflection pass" (it is
  a SET, see DRAWLIST below); SURVIVES - "whitening slot 14 reproduces the good state (0.936)", an intervention on
  every binding, untouched by aliasing; UNAFFECTED - the image-level series (good ~5 s, then dull).
- TEXTURE TRACE (new cvars ngpu_tex_trace_a/_b: a line per creation / re-upload of a fetch base, with frame). Test
  pre-registered: "a creation or re-upload of 134C3000 or 13543000 falls between the last good and first dull capture".
  TEXTRACE1: VOID (no good window - already dull at s00 - and run without ngpu_hooked_draws=false, unlike SERIES1/2).
  TEXTRACE2 (matched): flip between s01 22:15:04 (0.900) and s02 22:15:10 (0.795); both textures CREATED once at
  frame 1018 (22:14:50, the lake's first draw) and NEVER re-uploaded; log covers to 22:16:00; the re-upload path was
  live (906 re-uploads in the run). FAILS: their native content is a frozen snapshot of guest memory.
- INPUT CHANGE LOG (slot report, per FRAME, never per window): the SET of (slot, base) pairs E33859F0 binds on 14010500
  each frame, printed the first time each set is seen; every pixel constant, a line per 60 frames of every register
  whose first-draw value moved (old -> new); SERVED CHANGE per (slot, base): WHITE (option A / white_base) / real /
  placeholder. The old per-slot SLOT CHANGE line is REMOVED: a slot's base changes per draw, it logged 192,013 lines a
  leg and rotated the log six times (DRAWLIST1 lost frames 960-1679 to it).
  INLOG1 (flip s03 22:19:57 0.938 -> s04 22:20:03 0.799): no binding-set change in the window. s4/s5 (1FAB1/3/5000,
  1FAB0/2/4000) rotate EVERY frame from the lake's first draw to the end (60/s, steady) - a triple-buffered LUT, not
  flip-linked. The reflection triples swap wholesale (13403/13483/13503 + 13443/134C3/13543 <-> 134F3/13573/135F3 +
  13533/135B3/13633) at ~+47 s and back at ~+57 s WITH NO BRIGHTNESS CHANGE: which triple is bound does not matter.
  INLOG2 (flip s00 22:23:39 0.921 -> s01 22:23:46 0.790), pre-registered "a constant steps, or c16/c19/c78/c79 stop
  moving, in the window": FAILS. Those four settle in the 4th significant digit (camera easing) and c19/c78/c79 stop
  AFTER s01; exposure c20.w went 16 -> 8.15 -> 7.33 by 22:23:38, before the good s00. No step anywhere in the window.
  INLOG3 (flip s00 22:27:49 -> s01 22:27:55), pre-registered "a SERVED CHANGE (slot 8 12B1D000 WHITE -> real) in the
  window": FAILS. Slot 8 is served WHITE by option A from frame 1012 to the end; NO served state changes at all (57
  lines, every one a first sighting).
- THE FLIP IS NATIVE AND LOCAL: native lake luminance 144 -> 123 while the plugin holds 156 (4 legs); native sky -3,
  ground 0. It is a FADE over ~1 s, not a step (TEXTRACE2 s01 caught 140).
- LAKEONLY1 (only_ps E33859F0): VOID - the far (reflection-dominated) water clips to white (252) in 9 of 10 captures
  (light blue 175 in one), so a fade cannot show there.
- WB13443 (ngpu_white_base 13443000, the one natively re-rendered slot-14 buffer): no good window (dull at s00, so the
  pre-registered drop was not evaluable); ~1,500 bindings/s served white and the lake region UNCHANGED (123) - the
  visible lake does not read 13443000.
- DRAWLIST (the draw -> buffer map, 3 frames every 120): the lake is 2 groups x 10 draws per frame; in each group 5
  draws read (13403000, 13443000), 4 read (13483000, 134C3000), 1 reads (13503000, 13543000). The three slot-14 values
  are THREE REFLECTION PLANES drawn every frame, not a triple buffer. The buffers are contiguous 256 KB tiles:
  A 13403000, B 13443000, C 13483000, D 134C3000, E 13503000, F 13543000, G 13583000; the planes read (A,B) (C,D)
  (E,F). The game's RECORDED resolves (queued params, the swap-boundary base lists) go to A, B, C, E, G - so D and F
  are never resolved even by the game's own recorded copies, and natively they are a stale guest-memory snapshot.
  How D and F get written on the console (a taller resolve rect, memexport, a CPU copy) is OPEN. Mapping identical in
  all 87 sampled frames (both triples). DRAWLIST1/2 had no good window (2 of 2).
- POST-HOC CORRELATION (found after the fact - a HYPOTHESIS, not a result): in 5 of 5 legs the flip falls in the
  capture interval holding the SECOND swap-boundary event carrying 256x256 resolves after the lake appears (the first
  arrives with the lake, water still good): INLOG1 lake 19:32, events 19:33 / 20:01-03, flip 19:57-20:03; INLOG2
  23:36, 23:37 / 23:41, flip 23:39-46; INLOG3 27:44, 27:46 / 27:50, flip 27:49-55; TEXTRACE2 14:50, 14:53 / 15:05,
  flip 15:04-10; DRAWLIST2 40:25, 40:25 / 40:29, dull at s00 40:29. Events recur every 4-15 s, so coincidence is
  cheap; it needs a fresh pre-registered leg. The carried batches hold the lake's A B C E G resolves (1 each per event).
- NOCARRY1 (same binary, ngpu_carry_resolves=false), pre-registered "no flip: lake >= 0.90 in every capture": FAILS -
  0.45 in every capture (0.450 .. 0.472), far WORSE than dull and with no flip. Carrying is involved in the lake's
  content, but not as predicted.
- CORRECTION (claudecode-76): "the 51-second blind window" was MY READING of one rotated file of six, not a defect
  of the instrument - the fifth coverage artefact of the night and the first in READING rather than capturing. Rule:
  concatenate every rotated file (<TAG>.N.log .. <TAG>.log) before reading a timeline; RESPOS/ORDER legs do.
  SCOPE of "18 resolve keys, exactly one with no native writer": it enumerates RESOLVE DESTINATIONS. A buffer READ every
  frame and NEVER resolved (the lake's D 134C3000 and F 13543000) has no key and is not in the population at all. The
  figure answers "which destinations lack a writer", not "which read resources lack one". A census of read-but-never-
  resolved resources is OPEN (D and F are two; how many others is unknown).
  RULE1 (fresh leg for the post-hoc 2nd-carry rule): VOID as pre-registered (dull at s00), though consistent. The rule
  is not pursued further: its chance level was never computed (events every 4-15 s, flip intervals ~6.5 s), and the
  mechanism below explains the coincidence.
- THE REFLECTION COLLAPSE, FOUND AND FIXED (behind a switch, DEFAULT OFF). Chain, each link measured:
  * RESDUMP1: the lake's four reflection copies B C E G (F3442000 F3482000 F3502000 F3582000, all from the 280x280
    4xMSAA target 04020118) are distinct at frame 960 (C E G mirrored views of the arches, B a hillside view) and
    IDENTICAL by ~1560 (max|B-C| 0.38 -> 0.06 -> 0.006 -> 0.0002): every plane gets B's image.
  * RESPOS1 (copy positions): distinct and steady (G 632, C 672, E 690, B 773 of ~2,540) across the flip - FAILS
    the "positions collapse" prediction. REFLCAM1 (vertex constants of the target's draws): 48 distinct camera
    hashes every frame, same 90/56/14/8/6 structure - FAILS "cameras collapse". ORDER1: the 136 draws interleave with
    the copies (72 | 13 | 3 | 48 | 0) in good and dull frames alike - FAILS "ordering".
  * DEPTHOFF1 (new ngpu_depth_off_surf=04020118: no depth test on that surface only): copies stay distinct ALL RUN
    (0.46-0.47) - PASSES. The draws were being DEPTH-REJECTED.
  * Why: the copies carry RB_COPY_CONTROL 00100060 (no clear bits), the draws use GREATER_EQUAL (reverse-z) with
    z-write, none clears by drawing, and ngpu_depth_alias_clear (ALIASCLR1) saw zero clear rects - so natively that
    depth is NEVER cleared after creation and accumulates the nearest surface of every plane of every frame.
    On the console it IS cleared: the reflection depth sits at EDRAM tile 224 (000100E0) inside the scene's
    ranges (14010500, 2xMSAA: colour tiles 0..1439, depth 1024..2463 wrapping to 0..415), and the scene's
    end-of-frame copy F99DF000 carries 00100340 (colour + depth clear, RB_DEPTH_CLEAR 0). One EDRAM: that clear
    reaches the reflection's tiles. Natively each target has its own textures, so it did not.
  * FIX: ngpu_edram_clear_alias (EdramClearAliases, after every clearing copy). Three black-frame failures on the way,
    each recorded: EDRAMCLR1 cleared every overlapping alias to its own ClearZ guess (282k clears/run, native frame
    black); EDRAMCLR2 used the EDRAM value model with guessed values (black); EDRAMCLR6 used the registers' own values
    (RB_COLOR_CLEAR FFFFFFFF on the front-buffer copy, RB_DEPTH_CLEAR 0; ClearZ(scene) returns 1, wrong for reverse-z)
    and was still black - the SCOPE, not the value, was the fault. NARROWED to three guards: depth clears only, onto
    aliases whose DEPTH tiles overlap; only depth buffers already used this frame; only when the value is that
    buffer's own empty value by its votes. The copy's own RB_DEPTH_CLEAR / RB_COLOR_CLEAR now ride in the
    destination-matched LiveCopyRegs ring (the marker's live set is one copy ahead).
  * EDRAMCLR7 (guarded), pre-registered P1 copies distinct at every dump >= 1560 / P2 lake >= 0.88 every capture /
    P3 sky and ground within +-3 of INLOG3: P1 PASSES (0.47-0.48 all run; 20,006 clears, 18,802 refused), P2 FAILS
    (0.77-0.84), P3 FAILS narrowly (sky -3.6/-3.7; ground unchanged). EDRAMREFL1 (same, restricted to 04020118 by the
    diagnostic ngpu_edram_clear_alias_only): sky back within +-1 - the -3.6 comes from the OTHER aliases (10000410
    key 800, 05000140 key 840, 0A000280 key 8B8) - and the lake still dull (P2 FAILS).
  * STATUS: default OFF. It corrects the reflections (a correctness fix matching the console's EDRAM) but did not
    fix the water, and the full version moves the sky by more than the pre-registered bound. Turning it on needs the
    sky shift explained (is the console's sky -3.6 too? the plugin comparison can tell) - not done.
- THE WATER FLIP IS NOT THE REFLECTION COLLAPSE, NOR DEPTH ACCUMULATION: with the reflections correct (EDRAMCLR7,
  EDRAMREFL1) the water is dull; DEPCLR1 (new ngpu_dep_frame_clear: every keyed depth cleared at first use each frame)
  - pre-registered "lake >= 0.88 every capture" FAILS (0.77-0.82). The timing coincidence was two depth-related
  effects of the first seconds, not one causing the other.
- IN-SCENE SIGNAL (independent of captures): the native scene target's lake region (rows 222:292, cols 700:1200 of
  the 14010500 dump) is 1.34-1.80 at frame 1080 and 0.94-1.00 from frame 1200 on, in every dump run, fix or not. The
  flip happens ~2 s after the lake appears in dump legs (before s00 - why those legs have no good window).
- EXPOSURE: the lake PS's c20.w steps 16 -> 8.15 -> 7.33 at frames ~1048-1135 in every leg (INLOG2/3, DRAWLIST1/2,
  RULE1), ~1-2 s after the lake appears. Pre-registered "the step falls in the flip interval" FAILS as registered:
  the good captures come after it (INLOG2 settled 23:38, good 23:39; INLOG3 settled 27:47, good 27:49) - unless the
  capture timestamps lag the grab by > 1 s (unmeasured). PIN16 (new ngpu_pin_c20w_ps/_milli: E33859F0's c20.w held at
  16): the pre-registered ">= 138 at every capture" PASSES AS WRITTEN BUT WAS MIS-SPECIFIED - pinning raised the whole
  level (165-183) above the threshold - and THE FADE PERSISTS (182.8 -> 165.4, s00 -> s01, the usual size). The lake's
  own exposure constant is not the flip.
- OPEN, in order: (1) the flip is in the lake's pixels, persists with every PS input held (bindings, PS constants,
  served state, c20.w pinned) - the lake's VERTEX constants (camera / view vector for the Fresnel) are the one input
  never watched; (2) D and F (never resolved; frozen guest snapshots) - how the console writes them; (3) census the
  read-but-never-resolved resources; (4) the edram_clear_alias sky shift, against the plugin.
- (00:00-00:50 2026-09-25) THE LAKE'S INPUT LIST, ticked (claudecode-76's point - "the one input never watched"
  was a completeness claim with no enumeration): texture bindings per-frame SET - checked, steady; served state -
  checked, steady; D/F contents - frozen snapshots; B/C/E/G contents - collapse fixed, water still dull; A (13403000,
  from the 04000140 impostor surface - a TREE IMPOSTOR image at frame 960) - UNCHECKED over time; s4/s5 LUTs
  (1FAB0000 256x1 / 1FAB1000 64x1, CPU-written every frame, triple-buffered) - checked below; other slots' contents -
  unchecked; PS constants - checked; VS constants - checked below; vertex streams - unchecked; depth state - partly;
  blend/RT state over time - unchecked; the bed - checked (NOLAKE1); translated code / JIT swaps - unchecked.
  READING-RULE, a sixth coverage artefact: DRAWWALK1's per-draw dumps "did not exist" because my listing filtered
  out every name containing "_rt_" - and the per-draw names contain it. Grep for what a file IS (seq), never exclude
  by a substring the target may share.
- VS CONSTANTS (INPUT VS CONSTS, new: the lake's first-draw VS bank, a line per 60 frames, old -> new): no register
  steps in the flip window (VSIN1). c10 is a clock (+1.0/s), c15.x and c17.w follow it; c19/c76 drift (sun); c54 is
  the VS copy of the exposure. VSIN2 pre-registered "the flip is tied to the game clock (c10 in 27.9..34.9)": PASS -
  last good at c10 28.6 (bound < 34.9), first dull at 34.6 (bound > 27.9) - but CONFOUNDED (every leg's load/stand
  structure is identical, so game clock and wall time since the lake appears move together).
  PINT26 (VS c10 pinned for the lake): VOID - no counter; the waves kept moving (s05/s06 |diff| 4.47 vs 4.34 free),
  so whether it applied was unknowable. FREEZE1 (VS c10/c15/c17 frozen, COUNTED: 80,001 draws) and FREEZE2 (+ PS c17,
  c118, c119 frozen): VOID by the pre-registered validity rule (waves still move, 4.40 / 4.47 vs bound < 1.5) - the
  water's motion is not in the constants. The fade persisted in both (17.1 / 16.2).
- LUTS: LUTTRACE1 - after the first ~5 s the LUTs are re-uploaded ONCE PER 60 FRAMES, always mid-write ("newest
  write tick" = now-1): the settle rule (two quiet frames) never fires on a buffer rewritten every frame, so only the
  one-second force refreshes it. NOSETTLE1 (new ngpu_tex_nosettle_traced: re-upload on any write): validity met (20
  re-uploads/s) - pre-registered "drop <= 8" FAILS (17.2). Stale LUTs are real but not the fade.
- THE PER-DRAW WALK (DRAWWALK2/3: ngpu_dump_scene_draws=2, seq 1100-1600, .f16, frames 1080/2160/3240; the lake
  region in scene coordinates rows 222:292 cols 700:1200). The scene frame, in order: last frame's leftover (1.48 at
  1080 / 1.02 at 2160) -> lake group 1 (E33859F0, seq ~1161) -> B9B8BECA ZEROES the region (colour AND alpha 0) ->
  the bed redrawn (A92D4C17, seq ~1256: 0.92 / 0.84) -> lake group 2 (E33859F0, seq ~1531) ADDS on top. Destination
  alpha before group 2 is 1.00 in both frames - not the difference. What differs is what group 2 ADDS: +0.61 at 1080,
  +0.17 at 2160 (fix off); with the reflection fix on +0.79 / +0.32 / +0.16 at 1080 / 2160 / 3240 - a GRADUAL DECAY of
  the lake's own contribution over tens of seconds, bed steady. The window "flip" is the first seconds of that decay.
  SLOTW (per-slot whitening sweep, "lake adds" at 1080 and 3240, bound ratio >= 0.6): base 0.26; slot 14 0.30 - FAILS,
  but its 1080 value barely moved (0.567 vs 0.601), so whether the mask applied is UNVERIFIED (next: add a counter).
  DEPTHPS1 (new ngpu_depth_off_ps: no depth test for the lake PS only): VOID - the contribution EXPLODES (+163 ->
  +18,534 -> +48,020): the lake blends additively into a region nothing clears between frames when depth does not
  limit it. It says depth normally bounds the lake heavily; it cannot say whether depth is what decays.
- NEXT (in order): (1) count and verify the per-slot whitening, then finish the slot sweep on "lake adds" (slots 0 1 2
  4 5 10 11 12 13 15 16); (2) the lake group-2 draws' depth test result over time - dump the scene DEPTH at the same
  seqs (ngpu_dump_scene_depth) and see whether the lake's pass rate falls; (3) A's content over time (a tree impostor
  as plane 1's slot 13 is suspect in itself); (4) the edram_clear_alias sky shift against the plugin.
  Dumps from tonight: D:\fable2_flash\ngpu_dumps_20260924 (moved off C:, counts and bytes verified).
- CORRECTIONS to the 00:50 entry (claudecode-76's three points):
  (1) FREEZE1/2: VOID for the FADE question (validity = frozen waves, not met). The counter (80,001 draws held) makes one
  narrower statement sound: the wave MOTION is not driven by the six registers frozen (VS c10/c15/c17, PS c17/c118/c119).
  "The water's motion is not in the constants" OVERCLAIMED - ~30 other PS registers and the rest of the VS bank still
  animate. Retracted to the six-register statement.
  (2) The reflection SET is B C E G (F3442000 F3482000 F3502000 F3582000, all from 04020118) - A (F3402000, from the
  04000140 surface) was never in it; the clear-alias guard is scoped by SURFACE (04020118), not by that list. But A, bound
  as the lake's plane-1 slot 13, holds a TREE IMPOSTOR image at frame 960: CROSS-REFERENCE the impostor subject across
  rows - the far-scene lift (impostor cards), open item "blocky distant trees", and now the lake's plane-1 slot 13.
  Resource ids: impostor surface 04000140, the 256x256 series F2703000..F2907000 (4 surfaces each), and F3402000.
  (3) THE DECAY IS NOT A LAW - and not established as gradual. Lake-adds (final group) by leg:
      fix OFF DRAWWALK2 : 1080 +0.610  2160 +0.174  3240 +0.157   (3240 read from the existing dumps)
      fix ON  DRAWWALK3 : 1080 +0.788  2160 +0.318  3240 +0.164
      fix ON  SLOTW_base: 1080 +0.601  2160 +0.146  3240 +0.157
  The two fix-ON legs disagree at 1080/2160 by more than fix-on vs fix-off does, so the reflection fix's effect on the
  lake term is NOT established (n=2 per arm would still be under-powered). All three reach the same ~0.16 plateau by
  frame 3240. Shape: a drop from ~0.6-0.8 to ~0.15-0.3 between frames 1080 and 2160, then a plateau - "gradual decay
  over tens of seconds" is withdrawn.
  CONSTRAINT on the cause (written before any candidate): it lowers the lake's OWN additive term from ~0.6-0.8 to a
  ~0.16 plateau within ~1,000 frames of the lake's first draw; bed (~0.84) and destination alpha (1.00) steady;
  plateau identical with and without the reflection fix. Excludes anything acting on the bed or the blend destination.
- PROFILE1 (per-draw walk every 60 frames, final lake group, fix off). Pre-registered reading: STEP if start ->
  plateau within <= 120 frames, RAMP if spread over >= 4 samples. Lake-adds: 1020 +0.928, 1080 +0.577, 1140 +0.149,
  then +0.145 .. +0.207 creeping up to frame 2700 (the slow window-ratio rise). Verdict STEP (1020 -> 1140 = 120
  frames). Bed steady 0.83-0.85. The step sits on the frames where the game's exposure settles (c20.w 16 -> 8.15 ->
  7.33 at frames ~1048-1135 in every logged leg).
- EXPOFREEZE1 (new ngpu_freeze_exposure: VS c54 + PS c20 held at the first lake draw's values). Validity: applied,
  c54 frozen at (0.0633, 0.0339, 0.0196, 15.99999). Pre-registered "lake-adds >= 0.5 at every sample from 1140":
  PASS, observed +1.27 .. +1.37; window ratio 1.04-1.07 all run (bound for "no flip" >= 0.88) - no flip. A smaller
  residual step remains (+2.14 at 1080 -> +1.28 at 1140).
  The lake VS (F22BC502, ucode 362D6820BEB238C7) does NOT read c54 (it reads c0-3, c9 eye, c113+ water block) - so
  the PIXEL c20 did the work. The lake PS (E33859F0, ucode 15833F609E3A25C6) ends oC0.rgb = r2.xyz * c20.w: its output
  is LINEAR in the game's exposure, by the game's own code.
- RETRACTION: "PIN16 ... the lake's own exposure constant is not the flip" was WRONG. PIN16's window level 165-167
  against the plugin's ~156 is ratio ~1.06, the same as EXPOFREEZE1; the "fade" I read (182.8 -> 165.4) was the
  residual step, which EXPOFREEZE1 also shows. Holding c20.w at 16 DOES keep the water up.
- THE DISCREPANCY NOW: the plugin's lake, sky and ground are ALL steady across the flip (lake 154.5-156.4, sky
  186-187, ground 92/107, four legs) - no post-chain compensation visible - while the native lake follows the game's
  exposure down (linear in c20.w). The native frame does what the lake shader says; the plugin's does not show it.
  OPEN, in order: (1) does the plugin's lake see the same exposure step at the same guest frame (plugin frame vs
  native replay frame alignment is UNVERIFIED - the window flip time varied 4-25 s after the lake while the in-scene
  step is always at frames ~1080-1140, so the native WINDOW's lag behind the replay is itself unmeasured); (2) the
  residual step under frozen exposure; (3) the step is 6x on a 2.2x exposure change - not linear in the TERM, so the
  blend (oC0.w = r4.w, never written -> 0) needs reading.
- TWO EVENTS, NOT ONE (claudecode-76's discriminating test, read from PROFILE1's dumps, no new leg):
  A = IN-SCENE, GLOBAL, FIXED FRAME: the native scene target steps at frames 1020-1140 EVERYWHERE - sky 41.2 -> 18.0
      (x0.44), ground 0.888/0.980 -> 0.532/0.588 (x0.61), whole lake region 2.34 -> 0.97 (x0.42, about the sky's factor:
      the lake is mostly reflected sky). It lands on the exposure settle (c20.w 16 -> 7.33, frames ~1048-1135, every
      logged leg). Every window capture is taken AFTER it (s00 is >= ~5 s in), so no window can show it.
  B = WINDOW, LAKE-ONLY, VARIABLE TIME (4-25 s): INLOG1's window lake drops 145.6 -> 123.8 at s03 -> s04 (~20 s in)
      while the window ground holds (97.6 -> 98.0) and the sky moves -4. A lagged copy of A would move the ground by
      x0.61 in-scene - clearly visible - so B is NOT A seen late. CORRECTION of the inference "the flip was never
      intermittent, only the capture timing varied": A is fixed-frame; B's timing genuinely varies, and B is what the
      window's good/dull labels measured. The earlier re-label by capture time stands as a DESCRIPTION; the mechanism
      behind B is OPEN.
  No in-scene dump exists yet from a run whose window flip (B) came late - the dump legs are slower and B lands before
  s00 in them. NEXT: a leg with in-scene sampling of the lake at low cost (the lake region's mean only, logged per 60
  frames, no dumps) plus the window series, so A and B are both timed in one run.
  EXPOFREEZE1 removed the window flip (ratio 1.04-1.10 all run) with the lake's exposure held - so B, too, depends on
  the lake's c20.w - and left a residual in-scene step x0.60, close to the ground's x0.61 (the reflected content
  following A). All of that is an account to be tested, not yet a measurement.
- B IS NOT EXPOSURE (the variable itself, read from logs already on disk - claudecode-76: "log c20.w, not the lake
  mean"): the lake PS's c20.w, logged every 60 frames in INLOG2 and INLOG3, is FLAT through B - 7.321-7.349 across
  INLOG2's B window (22:23:39-46), 7.319-7.348 across INLOG3's (22:27:49-55). A global exposure change cannot make a
  lake-only drop, and none happens there anyway.
  RETRACTED: "EXPOFREEZE1 removed B, so B also depends on the lake's c20.w". PIN16 (c20.w pinned) shows a B-sized
  drop at s00 -> s01 (182.8 -> 165.4, -17; unpinned B is ~-20) - B survives the pin. EXPOFREEZE1's window looked flat
  most likely because its B fell before s00 (unverifiable). Also EXPOFREEZE1 was a TWO-VARIABLE intervention (VS c54 +
  PS c20); for A the code reading separates them (the lake VS never reads c54), for B nothing was ever separated.
  STATUS: A = the game's global exposure settle, fixed frame, explained by construction (oC0.rgb = r2.xyz * c20.w
  and the rest of the scene the same way); how the plugin's window stays level through A is the presentation-path
  question. B = a lake-only window drop at a genuinely varying time, NOT exposure, mechanism OPEN - back on the
  earlier input list (vertex streams, blend/RT state over time, translated code, other slots' contents unchecked).
  RULE (reusable): tell two candidate events apart by whether a THIRD subject moves (the ground), when their timing is
  the thing that cannot be measured.
- AXIS LABELS FROM HERE ON (claudecode-76): every figure is tagged [WINDOW] (a capture of a presented window) or
  [SCENE] (the native scene target in a dump) or [LOG] (a replay-side log line). 182.8 -> 165.4 (PIN16) is [WINDOW];
  it was read as [SCENE] twice before.
- EARLYA1 (new tools/native_gpu/bridge_leg_early.sh: captures from the start of the stand; asked for 1 s spacing, got
  ~2.5 s - one grab of two windows takes that long). Pre-registered on the [WINDOW] axis: "native window follows A"
  if the native ground drops >= 10% across the logged c20.w step, "level" if < 3%.
  [LOG] c20.w 16 -> 8.15 at 01:06:25.76 (frame 1058), -> 7.33 at 01:06:26.75.
  [WINDOW] s01 (mtime 01:06:26) native lake 145.7 / sky 162.0 / ground 96.3, plugin 154.9 / 187.3 / 93.3;
           s02 (mtime 01:06:28) native lake 123.0 / sky 157.3 / ground 95.6, plugin 156.1 / 187.0 / 92.7.
  Verdict: native window ground -0.7% = LEVEL (bound < 3%), plugin level too - CONDITIONAL on s01 being grabbed
  before the step (mtime 26 vs step 25.76-26.75: grab time vs mtime is unmeasured; s01's lake at 145.7 = the "good"
  level suggests it was). In the SAME pair the native window lake drops 145.7 -> 123.0 (-15.6%).
  CONSEQUENCE: the native WINDOW does not show A's [SCENE] ground x0.61 - the native presentation compensates the
  global exposure step (it runs the game's own tone map with its auto-exposure curve, recorded earlier), as the plugin
  does. After that compensation the lake, which falls MORE in-scene (x0.42 vs the ground's x0.61), is left with a
  LAKE-ONLY window drop. That is the B signature - so in this leg B COINCIDES WITH A.
  RETRACTED: "B is NOT A seen late" - the argument assumed the window ground would show A's in-scene x0.61; it does
  not. The earlier "B is not exposure" (c20.w flat through the INLOG2/3 B windows) now rests on the B TIMES in those
  legs, which are [WINDOW] capture intervals 1-8 s after the [LOG] step - consistent with B = A seen late, which is
  back ON THE TABLE. PIN16's [WINDOW] -17 under a lake-only pin is compatible too (the reflected sky still follows A).
  OPEN, restated: the lake falls x0.42 in-scene while the ground falls x0.61 through the same exposure step - WHY
  the lake falls further is the question (the lake = reflected sky, the sky fell x0.44: the lake follows its
  reflection). Whether the console's lake does the same at A is unmeasured; the plugin's [WINDOW] lake is level.
  "Unverifiable" earlier -> "not measured by that leg; settleable by an earlier capture" (EXPOFREEZE1's B timing).
- EARLYA1 CONDITIONAL, CORRECTED (claudecode-76): the one timestamp available puts s01 (mtime 01:06:26) INSIDE the
  [LOG] step window (25.76-26.75), not before it; only the unmeasured grab-to-mtime offset could move it earlier. And
  "its lake at the good level suggests it was early" is CIRCULAR (it uses the lake's state to time the capture that
  explains the lake's state). So: the [WINDOW]-level verdict is conditional on an offset the only timestamp argues
  AGAINST. Settle by measuring grab_windows.py's grab-to-mtime offset once, or a capture known to precede the step.
- THE REFLECTED-SKY ACCOUNT FAILS on a third subject (signature test): [SCENE] lake x0.42 and sky x0.44 are nearly
  the same, so if the lake's extra fall were its reflected sky the [WINDOW] sky would drop like the [WINDOW] lake. It
  does not: [WINDOW] EARLYA1 s01 -> s02 native sky 162.0 -> 157.3 (-2.9%) vs lake 145.7 -> 123.0 (-15.6%); INLOG1
  s03 -> s04 native sky 161.1 -> 157.1 (-2.5%) vs lake 145.6 -> 123.8 (-15.0%). The sky behaves like the ground. Sky and
  lake sit in a similar tonal range (~160 vs ~145), so the tone map's compression cannot plausibly turn the same input
  factor into -3% vs -15%. The lake's extra [WINDOW] fall is ITS OWN, not its reflection's.
  ARITHMETIC CAVEAT (recorded so nobody reads it either way): [SCENE] ratios do not compose through the nonlinear tone
  map; lake/ground = 0.42/0.61 = 0.69 against an observed [WINDOW] -15.6% is neither refutation nor confirmation.
  OPEN, restated: what makes the lake fall more than sky and ground through the same exposure step - in-scene the lake
  region falls x0.42 while its own reflected sky source falls x0.44 and the ground x0.61, yet in the window only the lake
  shows it. Next: the [WINDOW] vs [SCENE] relation for the lake region itself (does the game's tone map treat the lake
  pixels differently - bloom / luminance-curve input / alpha as luminance, cf. "console alpha = luminance", corr 0.97;
  the lake PS never writes oC0.w).
- THE ALPHA LEAD DIES AT ITS FIRST CONDITION (claudecode-76's two-part test, read from PROFILE1's dumps, no leg).
  Needed BOTH (a) native alpha tracks luminance for non-lake subjects, and (b) the lake's alpha differs. [SCENE],
  final dump of each frame, alpha mean / sd / corr(alpha, luminance):
    frame 1020: sky 0.957/0.106/-0.01, ground 0.930/-0.05 and 0.921/-0.09, lake 0.998/0.029/-0.00, whole frame +0.00
    frame 1140: sky 0.958/0.107/-0.00, ground 0.930/-0.05 and 0.923/-0.09, lake 0.998/0.027/+0.02, whole frame +0.01
    (1080 and 1500 the same). Alpha is flat through the step while luminance falls x0.44-0.61.
  (a) FAILS: nothing native writes luminance-tracking alpha (every corr within +-0.09). (b) holds trivially (lake ~1.00
  vs 0.92-0.96) but explains nothing without (a). The console's alpha-luminance corr 0.97 is a SECOND-POPULATION
  measurement (console post chain) and does not transfer to the native path. The companion prediction (the lake's
  window/scene relation invariant over time) is moot with the mechanism gone - not run.
  NOTED for later, separately: console alpha tracks luminance (0.97) and native alpha does not (~0) - a native/console
  difference in its own right, whatever it does to the lake.
  OPEN, unchanged: what makes the lake's [WINDOW] fall exceed sky and ground through the exposure step.
- THE WINDOW IS SOLVED - IT ADDS NOTHING (claudecode-76: tone-map INPUT levels, not output levels; our shared
  "similar tonal range ~160 vs ~145" argument reversed the axis and is WITHDRAWN - the [SCENE] inputs are sky 41.2 and
  lake 2.34, ~18x apart).
  Also WITHDRAWN: "the native window runs the game's own tone map" (EARLYA1 entry). It does not: present mode 17
  (src/ngpu_shaders/ngpu_ps_xs.hlsl) = per-channel Reinhard c/(1+c) on the HDR scene at ngpu_exposure 1.0, no gamma.
  Test (pre-registered by claudecode-76: curvature explains it if the predicted lake/sky relative-drop ratio is near
  5), computed PER PIXEL from PROFILE1's [SCENE] dumps through mode 17:
    full step 1020 -> 1140: linear sky -16.8% ground -23.7% lake -28.3% (ratio 1.7); sRGB -8.2/-11.8/-14.0 (1.7).
    By the letter: FAILS (1.7, not ~5). But the full step predicts the [WINDOW] ground -12..-24% where it was
    observed -0.7%: the capture pair did not span the full step - s01 was already past the ground's part of it
    (confirming the timing objection: s01's mtime sits inside the [LOG] step window).
    Matched interval 1080 -> 1140 (chosen AFTER seeing the full-step mismatch - POST HOC, anchored by the ground as a
    third-subject clock, not by fitting the lake): linear sky -2.8% ground -1.2% lake -15.2% vs observed [WINDOW]
    s01 -> s02 sky -2.9% ground -0.7% lake -15.6% - all three within 0.5 points; sRGB predicts lake -7.2% (swapchain is
    evidently linear). 1080 -> 1200 gives the same.
  CONCLUSION: the [WINDOW] series is the [SCENE] passed through a fixed curve. "B" = the SECOND HALF of the lake's
  in-scene step: 1020 -> 1080 everything falls with exposure (ground x0.61, sky x0.44, lake 2.34 -> 1.42); 1080 -> 1140
  ONLY THE LAKE keeps falling (1.42 -> 0.97, x0.68) after sky and ground have finished. The window's "lake-only" drop is
  that second half, caught by a capture that straddled frames ~1080-1140.
  AND: the plugin window stays level because it presents the game's post chain (auto-exposure); the native window is
  a fixed Reinhard, so the absolute native/plugin lake ratio (0.79 "dull", ~0.93 "good") crosses two presentation paths
  and is NOT by itself a water defect (already noted as a caveat days ago - now it is the explanation of the headline
  number). The lake row reduces to ONE [SCENE] question: why does the lake's fall lag the global exposure step by ~60
  frames and go a further x0.68? Whether the console's lake does the same is unmeasured (scene-against-scene needs a
  plugin [SCENE] read - the plugin's own dumps / readback).
- WORDING of the matched-interval result (claudecode-76): ONE parameter (the interval) fitted on ONE subject (the
  ground, as clock); sky and lake PREDICTED OUT OF SAMPLE (-2.8 vs -2.9, -15.2 vs -15.6); the lake - the row's subject -
  never used in the fit; sRGB EXCLUDED by the same data (lake -7.2 vs -15.6), so the linear swapchain is measured, not
  assumed. Not to be discounted as a "post-hoc match".
- WATER ROW STATUS: OPEN, EVIDENCE OF A DEFECT VOID. Every figure it rested on (the teal 47->62 vs 144 impression, the
  0.79/0.93 lake ratios, good/dull leg labels) compares the native Reinhard-at-exposure-1.0 present against the
  plugin's auto-exposure post chain - a cross-path ratio. No valid [SCENE]-against-[SCENE] evidence of a water defect
  exists. What IS established on one axis: the native [SCENE] lake falls in two stages at the exposure settle (with
  everything 1020->1080, then alone a further x0.68 over 1080->1140).
- PRE-REGISTERED, before any plugin scene read exists: read the PLUGIN's scene lake region ([SCENE] axis, the plugin's
  HDR scene - candidates: the scene resolve F99DF000 fmt 32 from 14010500 in guest memory under readback, or the
  plugin's own dumps) at frames ~1020, ~1080, ~1140, ~1500, plus its ground and sky as clocks.
    NO DEFECT if the plugin's lake/ground ratio falls by x0.68 +-0.10 over the interval where its ground has finished
    stepping (the game does this; row closes as "the presentation difference").
    DEFECT if that ratio holds within x0.90-1.10 while the native's falls x0.68 (real, localised to the lake's second
    stage, stated on one axis).
    UNRESOLVED otherwise, or if the plugin's frames cannot be tied to the native's by the ground clock.
  Instrument not built yet (guest-memory reads are only as current as the plugin's readback - verify the read changes
  frame to frame before using it: a stale readback would read as "holds" = a false DEFECT).
- PLUGIN SCENE TEST, TWO ADDITIONS before the instrument exists (claudecode-76):
  (1) TOLERANCE = max(+-0.10, 3 x the plugin read's OWN repeatability), measured on the plugin read itself (same
      frames, same region, repeated) BEFORE any scoring; then confirm both bands (NO DEFECT x0.68+-tol, DEFECT
      0.90-1.10) are NON-EMPTY and do not overlap - otherwise the test cannot return one of its verdicts.
  (2) POSITIVE CONTROL (a correctness test, not a liveness test): the plugin's own [SCENE] ground must reproduce the
      global exposure step already known natively - a fall of ~x0.61 at the settle (~frames 1048-1135 by [LOG]) - which
      validates liveness, buffer identity, stride and the fmt-32 decode in one check. If it fails, no lake number from
      the read counts. ASSUMPTION stated: this presumes the plugin's scene ground follows exposure as the native's does
      (ground [WINDOW] parity ~0.98 supports it; a plugin ground that does NOT step would be ambiguous between "wrong
      instrument" and "the plugin's scene differs" - scored UNRESOLVED, not DEFECT).
  Each side uses its OWN ground as clock, so plugin-vs-native frame alignment is never needed.
- PLUGIN SCENE READ BUILT AND RUN (new ngpu_guest_scene_probe / ngpu_guest_scene_dump_frame: the plugin's HDR scene
  resolve F99DF000, fmt 32 = k_16_16_16_16_FLOAT, read from guest memory at 199DF000 and 199E0000 (both live),
  untiled with TiledOffset2D* at log2_bpp 3, big-endian halves; region luminance + content hash per probe).
  GATES: (i) LIVENESS - the hash changes every probe in the world (static zeros in the menu). (ii) POSITIVE CONTROL -
  the plugin's [SCENE] ground steps at the known frames: GSCENE1 0.981 (1020) -> 0.556 (1080) -> 0.536 (1140),
  GSCENE2 1.038 -> 0.562 -> 0.536 (x0.52-0.57 vs native x0.61) - PASS. (iii) REPEATABILITY measured on the read:
  consecutive-probe relative change of lake/ground, sd 0.0014 (max 0.0064) -> tolerance max(0.10, 3 x 0.0014) = 0.10;
  bands NO DEFECT x[0.58, 0.78], DEFECT x[0.90, 1.10] - both non-empty, non-overlapping.
  SCORE (pre-registered, each side clocked by its own ground over the post-ground-step interval):
    [SCENE plugin] lake/ground 1080 -> 1140: GSCENE1 1.957 -> 1.959 = x1.001; GSCENE2 1.953 -> 1.959 = x1.003 -> DEFECT band.
    [SCENE native] lake/ground over the matching interval, ground flat: PROFILE1 2.619 -> 1.831 x0.70; REFLCAM1 x0.715;
      ALIASCLR1 x0.698; RESDUMP1 x0.717 (1080 -> 1200). EXCLUDED, recorded: ORDER1 x0.985 (its lake's second stage came
      BEFORE 1080 - the native second stage's timing itself varies); EDRAMCLR7 (ground not flat, x0.785 - clock fails).
  VERDICT: DEFECT, n=2 plugin legs, n=4 native legs. The plugin's lake follows exposure with its ground (x0.57) and
  stops; the native lake follows exposure AND then falls a further ~x0.70 on its own. The water row's DEFECT is now
  ESTABLISHED on one axis ([SCENE] against [SCENE]); it is the native lake's second stage.
  CAVEATS: (1) ALIGNMENT - the plugin full decode (GALIGN1, frame 1500) is BLOCKY: the 64 bpp untile is not fully right
  (content displaced within tiles; log-luminance corr with the native image 0.535 at the best shift dy 6 dx -16). The
  lake rectangle still lies inside lake content in both images, and a within-plugin RATIO OVER TIME measures the same
  (displaced) pixel set at every probe - so the verdict's quantity is unaffected; ABSOLUTE plugin-vs-native levels are
  NOT trustworthy until the untile is fixed (e.g. [SCENE] sky native 25.0 vs plugin 1.75 at frame 1500 - unexplained,
  do not read it as a finding yet). (2) The native second stage's timing varies between legs (ORDER1).
  NEXT: fix the 64 bpp untile (verify against the native image, corr should approach the 0.9s); then what in the
  native lake draw makes its second stage (the lake's inputs over 1080-1140, now with a correct target to compare).
- VERDICT RECORD, AMENDED (claudecode-76):
  * DENOMINATOR: native legs scored 4 OF 6 - ORDER1 excluded (its second stage fell before 1080), EDRAMCLR7 excluded
    (ground not flat, clock fails). Plugin legs 2 of 2.
  * ORDER1 IS A FINDING, not only a disposal: the native second stage is NOT pinned to a fixed frame (unlike the
    exposure settle, which is). The 1080-1140 window is a convention that fitted four legs; any future leg must LOCATE
    its own second stage (the ground-flat interval where lake/ground falls) rather than assume the window.
  * PREMISE, named: the within-plugin ratio is valid only if the lake rectangle's pixel set is (nearly) all lake under
    the blocky decode. Indirect support: the same decode's ground rectangle tracks the known exposure step at the known
    frames (a badly mixed sample would be unlikely to), and visually the plugin's lake band covers the rectangle. Not
    verified pixel by pixel - it becomes verifiable once the untile is fixed.
  * ROBUSTNESS, stated without borrowing a native value as the plugin's truth: the control's x0.52-0.57 vs the native
    x0.61 is NOT a demonstrated instrument error (x0.61 is the NATIVE ground's step; the plugin's true step is unknown -
    reading the gap as calibration would be a second-population inference). What holds: the scored quantity is a RATIO
    OVER TIME of one fixed pixel set, in which a multiplicative decode error cancels; to move x1.001/x1.003 into the
    NO-DEFECT band (<= 0.78) needs a ~22% time-varying error, and the read's measured time-variation (repeatability)
    is sd 0.0014 per probe. The verdict does not depend on the absolute decode being right.
- POSITIVE CONTROL, RESTATED (claudecode-76, following the calibration correction): its MAGNITUDE criterion ("~x0.61")
  was a native expectation and never licensed for the plugin. What it validly establishes is DIRECTION AND TIMING: the
  plugin's [SCENE] ground steps DOWN at the frames where the exposure settles - enough to exclude a wrong buffer, a
  dead readback or a grossly wrong stride. Record it as "stepped down at the expected frames; magnitude not comparable
  across paths", not as a near-miss on a target. ROBUSTNESS in two necessary halves: the decode is a FIXED spatial
  distortion, so it cannot make a time-varying ratio error on a fixed pixel set (bounds slow drift by construction);
  and the per-probe sd 0.0014 bounds the fast part empirically - the ~22% time-varying error needed to reach the
  NO-DEFECT band is ~150 sd away.
- PLUGIN SCENE UNTILE - NOT FIXED YET. Raw guest bytes dumped once (new: ngpu_guest_scene_dump_frame also writes
  guestscene_<addr>_f<frame>_raw.bin); offline variants scored by log-luminance correlation with the native [SCENE]
  image at the same frame (tools/native_gpu/guest_scene_untile_try.py; best shift searched +-24 px):
    tiled 64 bpp, pitch 1280, big-endian halves 0.530; as two 32 bpp texels (2x pitch) 0.555 (best, still blocky);
    8in32 swap 0.48-0.50; pitch 1312/1344 0.32-0.42; linear pitch 1280-1536 0.30-0.34; every little-endian variant ~0.05.
  So: big-endian 16-bit halves (certain), tiled (not linear), pitch 1280 - and a residual local block displacement
  (~16x4 px strips from neighbouring positions), i.e. a pipe/bank swizzle difference for 8-byte blocks or a
  resolve-specific layout. The earlier synthetic sweep "passed bpb 1-16" compared the hand code with the SDK's
  GetTiledOffset2D - if both share a swizzle assumption that the plugin's RESOLVE writer does not, the sweep could not
  see it (a shared-source differential). NEXT: read the plugin's resolve-writer layout (Xenia resolve shaders /
  texture_util for 64 bpp resolves) rather than guessing; the ceiling of the correlation is unknown (native and plugin
  scenes genuinely differ), so judge the fix by the blockiness vanishing, not by a correlation number.
- UNTILE, a REFERENCE-FREE metric (claudecode-76): blockiness = mean |adjacent log-luminance difference| ACROSS a
  candidate block boundary / INSIDE blocks, per stride (tools/native_gpu/guest_scene_blockiness.py). The native image
  (the ideal) reads 0.90-1.01 at every stride. Current decodes:
    64 bpp big-endian halves: x/4 1.30, x/8 1.61, x/16 2.22, x/32 2.16 | y/2 1.21, y/4 1.42, y/8 1.84, y/16 1.75, y/32 1.75
    as two 32 bpp texels:     x/4 1.83, x/8 1.85, x/16 1.87, x/32 1.75 | y 1.28-1.46
  So the displacement sits on 16-PIXEL COLUMNS and 8-ROW BANDS. The "two 32 bpp" variant's higher correlation
  (0.555 vs 0.530) does NOT make it closer: it is structurally worse (artefacts from x/4 up). FORMAT conclusions rest
  on the large gaps only (big-endian halves vs little-endian 0.05, tiled vs linear 0.33, pitch 1280); 32-vs-64 bpp is
  OPEN until the displacement is fixed, then re-scored.
  Pipe/bank swizzle variants (x>>2/3/4, y&4/8/16, bank shift 6/7/8; tools/native_gpu/guest_scene_swizzle_try.py):
  best worst-stride ratio 1.71 against the pre-registered target <= 1.11 - the swizzle term alone is NOT the fault.
  Lead: 8 rows = the EDRAM tile height at 2x MSAA (80x16 samples); the resolve may write in a layout tied to the
  source's EDRAM tiling, not a plain texture tiling. NEXT: read the plugin's resolve path (rexglue-src resolve /
  draw_util copy dest address computation) for 64 bpp with an MSAA source. The water verdict does not depend on this.
- PLUGIN SCENE UNTILE FIXED - IT WAS THE ADDRESS, NOT THE LAYOUT. The copy's OWN registers (new COPY DEST REGS log,
  destination-matched, DESTREGS1): F99DF000 -> RB_COPY_DEST_BASE 199E0000, pitch 1280, height 720, INFO 0000F001
  (format 32, endian 1 = 8in16, number 7 = float), copy_control 00100340. Every untile trial above had used the raw dump
  of 199DF000 - 4 KB early, which shifts data across macro tiles: that WAS the "block displacement". The 64 bpp
  big-endian decode at 199E0000 (GRAW1's second dump, no new leg): blockiness x 1.01/1.01/1.01/1.01, y 1.00/1.00/1.00/
  1.00/1.02 - PASSES the pre-registered <= 1.11 (native 0.90-1.01); the image is clean; correlation with the native
  image 0.605 at dy -2 dx -2 (the renderers' real differences, now measured on a correct decode). 32-vs-64 bpp SETTLED:
  64 bpp (the two-32bpp variant stays 1.28-1.85). Also noted: the synthetic-sweep lesson stands (shared-source
  differential), but this defect was an ADDRESS error in my reader, found by reading the producer's own register.
  Census note: the 27 swizzle variants were a SAMPLE (3x3x3 of a larger space), not an exhaustive family - moot now.
- VERDICT RE-SCORED ON THE CORRECT ADDRESS (both addresses were logged every probe - no new leg):
  control: plugin ground 1020 -> 1080 x0.564 (GSCENE1) / x0.541 (GSCENE2), steps down at the expected frames;
  repeatability sd 0.0011 / 0.0010; SCORE plugin lake/ground 1080 -> 1140 x1.003 / x1.003 -> DEFECT band, UNCHANGED
  (native x0.70-0.72, 4 of 6 legs). The verdict never depended on the decode, and now does not need the argument that
  it didn't.
- ABSOLUTE LEVELS, NOW POSSIBLE BUT NOT YET LIKE-FOR-LIKE: the plugin's copy is taken MID-FRAME at the scene resolve
  (native seq ~1728); the native "end of frame" RT dump includes draws after it. GALIGN1 frame 1500, end-of-frame native
  vs the plugin copy: lake 1.036 vs 1.042, ground 0.658 vs 0.539, sky 24.9 vs 1.75 - DO NOT READ these as findings
  until the native side is sampled at the copy's own position (a per-draw dump at seq ~1728, or read the native
  resolve texture F99DF000 itself). If they hold there: the native lake at its plateau would equal the plugin's in
  absolute terms, and the sky would be ~14x too bright natively - both large claims, so measure first.
- ABSOLUTE [SCENE] LEVELS, LIKE-FOR-LIKE (ABSLVL1, frame 1500): the NATIVE scene resolve texture F99DF000 against the
  plugin's copy of the same resolve (199E0000). Gates: the native resolve equals the native end-of-frame RT exactly
  (corr 1.000 - nothing draws on 14010500 after the copy, so the mid-frame worry is moot); alignment native vs plugin
  corr 0.636 at dy -1 dx -1 (pre-registered: shift <= 2 px, corr >= the 0.605 clean-decode ceiling) - PASS; quarantine
  lifted.
    region   native    plugin   native/plugin
    sky      25.258    1.747    x14.46
    whole     4.606    0.827    x5.57
    lake      1.575    1.042    x1.51
    ground    0.659    0.540    x1.22
  FINDING: the native HDR scene is far brighter than the plugin's, dominated by the sky (x14.5) - consistent with, and
  sharper than, the old row "our HDR scene target is ~3.5x too bright going into a correct tone map". The lake is among
  the LEAST wrong regions. (A peer read the sky pair with the labels swapped - native is 24.9-25.3, matching PROFILE1's
  native 18-41; the 1.75 is the plugin's.)
  ALSO: native lake at frame 1500 was 1.575 here and 1.036 in GALIGN1 - consistent with ORDER1: the native second
  stage's timing varies between legs (or it does not always occur by 1500).
  PRIORITY IMPLICATION (for the user): the sky / whole-scene brightness (x5-15 in [SCENE]) is a much larger native
  defect than the lake's second stage (x0.70 on one region); rows (j) sky and the "scene 3.5x too bright" row should be
  re-opened on this scene-against-scene instrument before more time goes into the lake.
- ROW (j) SKY (PS 63081965) UN-PARKED - it now has single-axis evidence: [SCENE] native/plugin sky x14.46 (ABSLVL1).
  THE WINDOW REVERSED THE SIGN: the [WINDOW] parity had the native sky at ~0.84 of the plugin (slightly DARK); like-for-
  like [SCENE] it is fourteen times too BRIGHT. Mechanism: native 25.3 through the fixed Reinhard saturates near 1.0
  (~157 in the capture); the plugin's own tone map lifts its 1.75 to ~187. Cross-path numbers were not merely
  imprecise - they could point the opposite way. That is why every "evidence void, cross-path" call stands.
  SHAPE CONSTRAINT, written before any candidate: ground x1.22, lake x1.51, sky x14.46 - NOT one multiplier. A global
  exposure / range / format scale is EXCLUDED on this arithmetic; the cause is overwhelmingly concentrated on the sky
  (ground within ~20%), and the whole-frame x5.57 is the sky's coverage-weighted consequence, not a separate fact.
  PRIORITY: the sky row outranks the lake's second stage (x0.70 on one region); the lake is among the LEAST wrong
  regions (x1.51).
  RECORDING RULE (a peer misread "sky 24.9 vs 1.75" by sentence order): label BOTH sides of every pair inline
  ("native 24.9 / plugin 1.75"), never rely on order.
- ROW (j) SKY, WORK BEGUN (proceeding on the USER's standing grant "work fully autonomously, always choose recommended
  option" - the priority switch lake -> sky is the recommended option; a peer pointed at the grant, it did not supply it).
  INSTRUMENT: native [SCENE] sky recovered from the WINDOW at present exposure 0.05 (tools/native_gpu/sky_invert.py:
  per channel c = x/(1-x)/E, linear swapchain). VALIDITY vs a known quantity: SKYBASE1 s00-s03 sky 24.0-25.8 (bound
  ABSLVL1 25.3 +-15%), ground 0.66-0.69 (ABSLVL1 0.659), 0% saturated - PASS. (Sky drifts to 19/16 at s04/s05 - noted.)
  SUBJECT CHECK: the sky PS 63081965 alone takes the [SCENE] sky region to 29.85 (DRAWWALK2 f1080 seq 1153) - it IS
  the producer; the earlier "full frame = sky-only frame within 6/channel" was a [WINDOW] match on a SATURATED sky and
  carried no information.
  PS 63081965 read (630819653FA50E86_p.hlsl): an atmospheric-scattering evaluation, output oC0.rgb = r1.xyz * c20.w
  (exposure); inputs slot 4 (1FAB* 64x1 LUT, fmt 29), slots 13/14 (1A565000 16x16, sampled then squared), slots 11/12
  (1x1 default, only inside the NGPU_BOOL(129) branch), constants c19 c28 c31 c46 c47 c64-c67 c72-c76, literals
  c250-c255.
  EXCLUDED: (1) fmt 29 decode - native maps it to R16G16B16A16_FLOAT exactly as the plugin (rexglue d3d12
  texture_cache.cpp); (2) the literal block - c255.x = log2(e) 1.4427, c251 +-pi / -2, c252 pi/2, c254.w 1/(2pi), plus
  atan/acos polynomial coefficients: sane; (3) the sampled TEXTURES - per-slot whitening on 63081965 only (counter:
  ~19,540 bindings whitened per run - applied): slot 4 x1.05, 11 x1.00, 12 x1.00, 13 x1.07, 14 x1.00 of baseline
  (pre-registered candidate threshold > x2 either way) - no slot carries a 14x.
  VOID: bool 129 forced 0 and 1 (ngpu_bool_force): x1.00 both ways, ground unchanged - identical results for 0 and 1
  mean no evidence the force reaches the replay path; NOT an exclusion.
  Constants reach the native path from the plugin's own register writes, so they are the same values; with identical
  inputs and microcode, a 14x points at the TRANSLATION (XenosRecomp's HLSL for this shader) or at a constant-bank
  mapping. NEXT: (a) per-constant sensitivity on 63081965 (c64/c65 scattering coefficients inside exp2(-(...)*log2e),
  c67, c72-c76) - the coarse candidates; (b) the xmul/log2/exp2/rsqrt semantics on this shader's specific ops (the
  `ps = log2(abs(r6.w))` / `exp2(r1.z)` pair and the Xenos log/exp edge rules); (c) verify the bool force reaches
  the replay before re-running (b129).
- SKY ROW, three answers (claudecode-76's questions, from the translated output and the source - no leg):
  (1) CO-ISSUE: 630819653FA50E86_p.hlsl carries the fix's snapshots (rsnap = r2 / r3 / r0 at its co-issued sites) - the
      fix's pattern is present in this shader's translation. That is "the fix reached this shader", not "every hazard
      in it is covered"; the exp2 inputs (r1.z = log2|r6.w| * c254.x -> pow(|r6.w|, 0.2); the c64/c65 extinction
      term exp2(-(r2.y*c65 + r2.x*c64) * log2e)) are to be traced by hand next.
  (2) fmt 29 "BOTH SIDES" = the native GetTexture mapping (hand-written in this repo, case 29 -> R16G16B16A16_FLOAT)
      against the table the PLUGIN EXECUTES (rexglue d3d12 texture_cache.cpp k_16_16_16_16_EXPAND ->
      DXGI R16G16B16A16_FLOAT; vulkan -> VK R16G16B16A16_SFLOAT; these legs ran backend 'd3d12'). A runtime-behaviour
      parity check, not two readers of one shared table - the exclusion stands.
  (3) PRE-REGISTERED for the constant sweep: x14.46 = exp2(3.85). A candidate constant must produce ~3.85 of log2
      exponent (x14.5 +- 25%) under a PLAUSIBLE error of that constant (a unit / scale / sign / component-swap slip),
      moving the native sky DOWN toward the plugin's. One that has influence but the wrong size is not the carrier.
      One of the right size in the WRONG direction (raises the sky) is recorded as "exponent-sensitive, wrong sign" -
      evidence the fault lives in that term, not a fix.
  NEW NOTE: the plugin logs "D3D12 draw resolution scaling is enabled" in these legs - the plugin's scene copy in guest
  memory is its UNSCALED resolve path. Region MEANS should survive a downscale, but that is an assumption to state,
  not a verified fact, against every plugin-copy absolute level (ABSLVL1's x14.46 included).
- RESOLUTION-SCALING ASSUMPTION, BOUNDED (claudecode-76): a box downscale of a large smooth region preserves its mean
  to within a few percent (averaging an average moves it only where the region edge cuts the sampling grid) - it can
  move a region mean by a few percent and CANNOT produce x14.46 (two orders of magnitude short). POSITIVE CONTROL
  already in hand: the ground lands within x1.22 on the same copy; a copy-path defect cannot be selective between sky
  and ground in the observed way. Status: "supported by the ground's near-parity, magnitude-bounded to a few percent".
- NOT ADOPTED: "sky-only 29.85 vs full frame 25.3 means something draws over the sky and pulls it down". The two
  numbers are from DIFFERENT FRAMES (29.85 = DRAWWALK2 f1080, exposure still settling at 8.15; 25.3 = ABSLVL1 f1500,
  7.33) and the native sky drifts within a run (SKYBASE1 25 -> 19 -> 16). No over-draw is established; it needs the
  same frame, before and after the later sky-region draws. A hand-trace candidate is judged against the sky pass's
  own output at a stated frame and exposure, not against either number blindly.
- SKY RATIO OVER A RUN (claudecode-76: does the plugin's [SCENE] sky track exposure like the native's?). Frame-matched
  within each side's own run; the native and plugin series are from DIFFERENT legs (valid only because the plugin value
  turns out constant):
    [SCENE plugin] GSCENE1 1020 3.33 -> 1380 1.75, then 1.74-1.75 to frame 4980; GSCENE2 3.57 -> 1.75, then 1.74-1.75.
    [SCENE native via WINDOW, E=0.05] SKYBASE1 s00-s09: 24.0 25.4 25.8 25.3 19.3 15.9 16.2 15.4 16.7 18.6.
  (a) BOTH track the exposure SETTLE (plugin x0.53-0.49 over 1020 -> 1380; native x0.44) - at the settle the difference
      is in magnitude, not in kind.
  (b) AFTER the settle the plugin sky is FLAT (1.74-1.75) while the native sky WANDERS by ~x0.6 over the run with
      exposure flat (c20.w 7.32-7.55): a NATIVE-ONLY time variation of the sky, not exposure.
  HEADLINE RESTATED: native/plugin [SCENE] sky is x8.8 - x14.7 over one run after the settle (x14.46 at ABSLVL1 f1500,
  exposure 7.33, is one point on it). Carry frame and exposure with any single figure.
  LEAD (a difference in kind, now): what varies the native sky over tens of seconds at constant exposure? The sky PS's
  time-varying inputs - the per-frame CPU-written LUT in slot 4 (whitening it moved the sky only x1.05, so its CONTENT
  cannot carry a x0.6 swing... unless the swing is in a constant), c19 (eye), c66 (sun direction, drifts slowly),
  c64/c65 (scattering, time-of-day?). The per-constant sweep should first LOG those constants across s00-s09 and see
  which one moves with the native sky.
- SKY WANDER - SCOPE AND CONSTANTS (claudecode-76's ordering: scope before cause).
  SCOPE: the native ground does NOT wander (SKYBASE1 0.661 -> 0.700, +6%, monotonic; lake a slow rise after its second
  stage) while the sky goes 24.0 -> 25.8 -> 15.4 -> 18.6 (non-monotonic, x0.60). SKY-SPECIFIC. Reproduced (n=2):
  SKYCONST1 23.96 25.45 25.96 25.40 19.40 16.06 16.30 15.37 16.72 18.68 - deterministic and time-locked, not noise.
  PRE-REGISTERED (before logging): the cause produces a NON-MONOTONIC ~x1.68 swing on the native sky at constant
  exposure, co-timed with the series, that the plugin sky does NOT show (plugin flat to 1%); exponent criterion now a
  RANGE log2 3.14-3.88 (x8.8-14.7); headline leads with the FLOOR: the native sky is at least x8.8 the plugin's.
  RESULT (SKYCONST1, the per-frame input log filtered to 63081965 on the scene target, sampled 1 s before each
  capture; tools/native_gpu/sky_const_corr.py): 81 of 156 logged components move; of the registers the sky PS READS,
  14 move, ALL MONOTONICALLY (|corr| 0.76-0.81 with the up-down series - what any monotonic drift scores against it):
  c20 (exposure 7.32-7.55), c64/c65 (scattering, <3% change), c67.x (16.9 -> 18.9), c66 (SUN DIRECTION: y -0.46 ->
  -0.26, x -0.76 -> -0.65, the largest relative mover). No read register shows the co-timed non-monotonic shape.
  The top raw correlations (c78, c42, c38, c142, c109, c80, c23, c36 at 0.82-0.89) are registers the sky PS does NOT
  read - spurious with n=10 and monotonic drifts.
  STATUS: c66 is NOT excluded - a monotonic sun sweep can make a non-monotonic sky if a phase peak passes the view;
  the shader indexes its slot-4 LUT by cos(sun, view) (r2.w = saturate(dot(c66 / |view|, view))). The plugin's FLAT
  sky under the SAME c66 sweep is the discriminator: the native sky responds to the sun's motion and the plugin's does
  not. NEXT: freeze c66 on 63081965 (the constant-freeze mechanism, counted, validity = the native sky stops
  wandering) - if the wander vanishes, the fault is in the sun-angle path (LUT lookup / phase terms) of the translated
  shader; also re-run the slot-4 whitening across the WHOLE run (the first sweep measured s00-s03 only, the sky's
  high part).
- SKY WANDER, two interventions scored on WANDER AMPLITUDE (max/min of the [SCENE] sky over s00-s09), level NOT scored
  (claudecode-76: a level threshold cannot see whether the wander collapses; the first texture sweep used a level
  threshold (> x2) on s00-s03 only and so did NOT exclude textures as carriers of the WANDER - it excluded them only as
  carriers of the LEVEL at the run's start). Pre-registered: baseline x1.69; PASS (the input drives the wander) <= x1.15;
  FAIL >= x1.5.
    SKYFZ66 (new ngpu_freeze_ps / ngpu_freeze_ps_reg: c66 of 63081965, the SUN DIRECTION, held at its first value
      -0.770 -0.237 0.592; count logged >= 1, applied per draw by the code path): sky 23.3 25.3 25.4 26.1 20.8 16.2 16.1
      15.3 16.4 18.5 -> x1.70: FAIL. The sun's motion does not drive the native wander.
    SKYW4AMP (slot-4 LUT whitened on 63081965 for the whole run, 19,487 bindings): 25.1 26.7 27.1 26.8 20.8 17.3 17.6
      16.7 18.0 20.0 -> x1.63: FAIL. The per-frame LUT's content does not drive it either.
  PROPERTY OF THE DEFECT (recorded as such): the wander is DETERMINISTIC and TIME-LOCKED (n=2 near-identical series) -
  every stochastic account is excluded; one well-instrumented run characterises it.
  RULE: |corr| 0.76-0.81 is what a monotonic series scores against a non-monotonic one - a null result, not a hit.
  OPEN QUESTION NOT BUILT: an INDEPENDENT plugin-side read of c66 at the sky draw (the native path's constants ARE the
  plugin's recorded register writes, so a same-source comparison would validate transcription only).
  HYPOTHESIS ONLY (fits every constraint; not tested): OTHER DRAWS over the sky region - the per-draw walk shows the
  region rebuilt in the second scene pass by several shaders around the sky pass (2D5EA8E8, 338B8AB7, 957C7EA6 sampled
  at seq 1525-1529). Clouds moving over the sky would give a deterministic, time-locked, non-monotonic, sky-only swing.
  TEST: per-draw [SCENE] walk of the sky region at a HIGH-sky frame and a LOW-sky frame of one run (same run, both
  sides of the swing), naming the draw whose contribution differs.
- ROW (j) RE-ATTRIBUTED - THE "SKY" EXCESS IS A LOCALISED OBJECT DRAWN BY PS 338B8AB7, NOT THE SKY PASS.
  (claudecode-76: over-draw only fits if the over-drawn layer is itself wrong natively; test by disjoint boxes first.)
  DISJOINT BOXES (tools/native_gpu/sky_boxes.py, [SCENE native via WINDOW], three legs): NOT lockstep - box CL
  bottoms at s06 / peaks s03, CR bottoms s08 / peaks s01, pairwise corr 0.06-0.61; and LEVELS differ wildly: L 1.2-1.5,
  R 2.5-5.5, CL 26-49, CR 23-57. Something bright MOVES across the top centre; not a whole-pass cause.
  PLUGIN vs NATIVE BY BOX ([SCENE], ABSLVL1 f1500): R native 2.56 / plugin 2.54 (x1.01); L 1.43 / 1.03 (x1.39);
  CL 41.35 / 1.65 (x25); CR 55.69 / 1.78 (x31). Native top-200 rows: 7,638 px above 10, max 1,194 at (4, 414); plugin
  0 px above 10, max ~3. The sky AWAY from the object matches the plugin (x1.01).
  WRITER (SUNWALK1: every draw dumped, seq 1140-1160 and 1512-1536, frame 1080): second pass - after the sky PS
  63081965 (seq 1524): 22 px > 10, max 18.0 (the sky itself is fine); then PS 338B8AB7 (VS 037F92DA): seq 1527 2,385 px,
  max 909; seq 1528 7,751 px, max 1,043; nothing after changes it. PS 338B8AB7 WRITES THE OBJECT.
  338B8AB7891DDC44_p.hlsl: samples slot 13 with +-1 texel offsets (a height-to-normal cloud-layer pattern), lighting
  from c47/c51/c71/c77/c79; output oC0.rgb = r0.xyz * c20.w (exposure ~7.3) - so ~1,000 natively is ~140 BEFORE
  exposure, absurd for a cloud layer (its other branch outputs 0/1 comparisons). CANDIDATES, none tested: blend state
  (three consecutive draws accumulating), a near-zero rsqrt / divide in the lighting path, the slot-13 cloud texture's
  content or format.
  CORRECTIONS: (1) row (j) had been identified by ROLE ("the sky" = 63081965, the full-screen gradient); the subject is
  a DIFFERENT draw - name by resource id, again. (2) The sky-PS work tonight (textures, fmt 29, literals, c66 freeze,
  slot-4 amplitude) is correct but was aimed at an innocent pass; its exclusions stand as statements about 63081965.
  (3) The x8.8-14.7 "sky" ratio is the object's coverage-weighted contribution; the wander is its motion.
  NEXT: per-draw state of 338B8AB7's three draws (blend, depth, its constants), its slot-13 texture, and the same
  region in the plugin's copy per draw is not available - use the native draw's inputs against the rule set above.
- HEADLINE RESTATED BY SURFACE (claudecode-76): "native sky x8.8-14.7 / whole frame x5.57" were REGION MEANS
  CONTAMINATED BY A LOCALISED OBJECT. Correct statement: A LOCALISED LAYER DRAWN BY PS 338B8AB7 (VS 037F92DA) IS
  ~x25-31 TOO BRIGHT IN THE BOXES IT COVERS (object pixels 150-900 against a plugin max ~3); THE SKY ITSELF IS AT PARITY
  (R box x1.01, L box x1.39). The "sky wander" and the "sky level" rows are ONE row - merged on MEASURED CO-LOCATION (the
  variation lives in the boxes containing the object, not in L/R), not on numerical adjacency.
  RULE HARVEST: a RECTANGLE standing in for a SURFACE has now cost seven investigations tonight (fake spatial gradient,
  mixed lake rectangle, sky region vs the full-screen pass, and a bright object inside a sky box reported as the sky).
- THE CLOUD LAYER, first discriminators (SUNWALK1 dumps, frame 1080; pre-registered before looking):
  OVERLAP: of 7,751 object pixels, 1,315 written only by draw 1527, 5,366 only by 1528, 1,048 (13%) by both - brightness
  is NOT concentrated where draws coincide: blend accumulation across the draws is NOT the mechanism.
  DISTRIBUTION: p10 212, p50 359, p90 704, p99 923, max 1,043; p99/p50 = 2.57 (pre-registered: tail >= 5, uniform < 3)
  -> UNIFORM inflation, not a heavy tail: a near-zero rsqrt/divide is NOT the mechanism. Object bbox rows 0-167, cols
  13-1250: a whole top band, not a blob. SHAPE: a uniform over-SCALE of ~x100-300 against the plugin.
  EXP_ADJUST (a 2^exp texel scale in the fetch constant; x256 would fit): EXCLUDED - the run's census reads 0 folded,
  0 unhandled: no texture carries a non-zero exponent adjust.
  CLOUD DRAW'S INPUTS (CLOUDREP1, slot report on 14010500): blend 07060706, alpha test GREATER 0.02, depth 00708766;
  TARGET RB_COLOR_INFO 000C0000 (colour format 12 - NOT the scene's usual 00030000 format 3, same surface); slot 13
  (the +-1-texel height source) = 16CE0000 512x512 fmt 20 (DXT4_5 / BC3, hardware UNORM on D3D12 - an 8-bit-as-integer
  x255 read is implausible for BC3); c20 = (0.0633 0.0339 0.0196 16) at the reported draws; c31 = (5 5 0 0); c67 =
  (19.17 1900 1 0.0005); NON-FINITE c129.z = FFFFFFFF.
  OPEN CANDIDATES, ordered by fit to "uniform x100-300": (1) the draw's colour format 12 vs the target's native RGBA16F
  (on the console a k_2_10_10_10_FLOAT target clamps at 31.875 - but the plugin's max ~3 says the console never
  reaches that, so clamping alone does not explain it); (2) a constant in the lighting path (c47 / c51 / c71 / c77 /
  c79, and c67.y = 1900); (3) c129.z non-finite - check whether 338B8AB7 reads c129. NEXT: log 338B8AB7's full read
  constant set and its output before * c20.w (r0) at one pixel; compare c67/c71/c51 magnitudes against a plausible
  cloud radiance.
- CLOUD LAYER: THE SHADER CANNOT PRODUCE THE VALUES SEEN. PS 338B8AB7's read set (from its HLSL): c20 c31 c46 c47 c51
  c69 c70 c71 c77 c78 c79 + literals (c252 = c253 = 0; c254 = (0, 0.001, 1000, 1) - a 1000-2000 distance fade; c255.x
  = 32, the exponent of a rim term (1 - saturate(n . -c78))^32). Output = (rim*c47.zyx + c71.x) * c51.x * texel.rgb *
  c20.w, texel from slot 13 16CE0000 512x512 fmt 20 -> native BC3_UNORM (<= 1). LOGGED (CLOUDREP2, new EXTRA CONSTS
  line in the slot report): c47 (0.82-0.85, 0.65-0.68, 0.31-0.32, 1), c51.x 0.9-1.0, c71.x 0.9-1.0, c69.x = c70.x =
  0.5, c46 small, c77 a world position, c78 the sun direction, c79 the eye. So the output is bounded at ~2 x texel x
  c20.w <= ~15 BEFORE blending (blend 07060706 = standard src-alpha / one-minus-src-alpha). The native per-draw dumps
  read up to ~1,043 on the scene target right after this draw: THE VALUES ARE NOT THIS SHADER'S ARITHMETIC ON THESE
  INPUTS. Constants and texture decode are EXCLUDED as carriers of the scale.
  LEAD: the draw targets COLOUR FORMAT 12 (RB_COLOR_INFO 000C0000, k_2_10_10_10_FLOAT_AS_16_16_16_16) on the scene
  surface whose other draws use format 3 (00030000). On Xenos that REINTERPRETS the same EDRAM bits. If the native path
  keys a separate target by colour info (ngpu_rt_key_color), or blits/aliases between them, the draw's output (or the
  following read of the surface) can arrive through a format reinterpretation - which would give a uniform scale of a
  large factor, matching the shape. NEXT: which NativeRT the 338B8AB7 draws bind (key incl. colour), what writes it
  back into the format-3 target, and what bit conversion is applied there.
- CLOUD LAYER, follow-ups (claudecode-76's consolidated message):
  c129.z: PS 338B8AB7 does NOT read c129 - its read set from its own HLSL is c20 c31 c46 c47 c51 c69 c70 c71 c77 c78
    c79 (no NGPU_BOOL use in its body). Established on THIS shader, not inherited from the sky shader's exclusion.
  PREMISE, marked: "BC3 decodes to UNORM <= 1" is an assumption about the decode behaving as declared; the output bound
    (~15) inherits that status.
  FREE EXCLUSION: the inflation is even across a contiguous band (rows 0-167; p99/p50 2.57) - the height-to-normal,
    texture sampling and gradient work cannot scale every pixel equally; uniformity retires the geometry as well as
    the arithmetic.
  SCALE: output bound ~15 against the observed ~1,043 already gives >= ~x70 (a better-derived figure than the box means
    x25-31 or the object figure x100-300, which are different quantities).
  THE FORMAT-12 LEAD DIES ON CODE READING: ngpu_rt_decode_color (default TRUE) folds colour format 12
    (2_10_10_10_FLOAT_AS_16_16_16_16) onto 3 in GetRT, so the cloud draws bind the SAME native RGBA16F target as the
    scene's other draws (the frame census lists the raw registers separately - 758 + 488 draws - but they share one key).
  A CONTRADICTION TO RESOLVE: before the first cloud draw the top rows hold <= 18; after it ~909 - from a shader bounded
    at <= 15 under src-alpha / one-minus-src-alpha with alpha in [0, 0.5], which cannot exceed max(src, dst). So one
    premise of the bound is FALSE: (a) per-draw constants (only the first draws per window were logged; c46 already
    differs between them), (b) the texel (BC3 <= 1, above), or (c) the blend state AS ISSUED to the host GPU (a factor-6
    decode as SRC_COLOR would give src^2 - ~1,000 at src ~32). NEXT: log the host blend descriptor Plume receives for
    the 338B8AB7 draws, and the constants of ALL three draws of a frame (not the first per window).
- CLOUD ATTRIBUTION CONFIRMED BY INTERVENTION (DROPCLOUD1: ngpu_drop_ps = 338B8AB7, 720 draws/window removed - applied).
  Pre-registered: confirmed if the centre boxes fall to <= 5, wrong if they stay >= 20. [SCENE native via WINDOW]:
    box L 1.21-1.23 | CL 1.64-1.66 | CR 1.78-1.80 | R 2.52; wander amplitude x1.00-1.02 in every box.
  CONFIRMED. And against the plugin's [SCENE] boxes at f1500 (ABSLVL1): CL native-no-cloud 1.65 / plugin 1.65; CR 1.80 /
  1.78; R 2.52 / 2.54; L 1.22 / 1.03. NATIVE WITHOUT THE CLOUD DRAWS MATCHES THE PLUGIN within ~1% in three of four
  boxes: on the console the cloud layer contributes essentially NOTHING to these boxes; natively it adds x25-31 and all
  of the wander.
  STANDING CONTRADICTION: the translated 338B8AB7, bounded from its logged inputs (ordinary constants, BC3 texels on
  all four slot-13 bindings 16CE0000 / 16980000 / 13E60000 / 13E80000, standard alpha blend decoded correctly by
  XenosBlend: 6 -> SRC_ALPHA, 7 -> INV_SRC_ALPHA), cannot exceed ~15 or max(src, dst) - yet natively its draws take
  pixels from <= 18 to ~909-1,043. So what EXECUTES natively for these draws is not what was read: the next checks are
  the pipeline actually bound (which DXIL / variant, keyed by state), its blend-enable and the alpha test as issued (the
  draw has alpha test GREATER 0.02 on: SPEC_CONSTANT_ALPHA_TEST must be set for its variant), and the three draws'
  own constants. A native skip of 338B8AB7 is a measured WORKAROUND for the sky (parity within ~1% in 3 of 4 boxes),
  NOT a fix - not applied.
- THE CLOUD CONTRADICTION RESOLVED AT THE TRANSLATED-CODE LEVEL: THE LIGHTING IS UNREACHABLE. My ~15 output bound
  assumed the lighting path runs; re-reading lines 763-893 of 338B8AB7891DDC44_p.hlsl: p0 = !(alpha-ish r0.w > 0.001)
  (visible pixels: p0 FALSE); then `if (p0) { if (p0) {r0.xyz = compare} if (!p0) {...lighting...} ... }` - every
  (!p0) lighting block is NESTED INSIDE `if (p0)`, so it can never run. For a VISIBLE pixel the whole block is skipped
  and r0.xyz keeps its INPUT value - the interpolant r0, used on line 765 as a WORLD POSITION (r0.yx - c79.yx, c79 =
  the eye; c77 ~ (-720, -133, 1802)) - and the output is world position x c20.w, alpha-blended at alpha <= 0.5: values
  in the hundreds to ~1,000, matching the measured p50 359 / p90 704 / max 1,043. The premise that broke was mine:
  "the lighting runs".
  CLASS CENSUS (tools/native_gpu/predicate_nest_census.py): 9 of 92 translated shaders contain a predicated block
  nested inside its own negation (dead by construction): D4686D15 v (the fern VS) 83, B41EE8F1 v 83, 19256301 v 61,
  6B210267 v 30, 81B999B2 p 26, DBF9A58A v 25, 338B8AB7 p 23, 8C74AF61 v 17, 6E1021DB v 16.
  WHERE, NOT YET SETTLED: XenosRecomp's control-flow emission (shader_recompiler.cpp ~1889-1928 cond_jmp -> `if (p0 !=
  cond) {` closed at the jump label; ALU predicates `if (p0 == pred)`) follows Xenia's documented polarity - so the
  dead nesting may be faithful to the microcode. But taken at face value the console's visible cloud pixels would also
  output world position x exposure, and the plugin shows none. So EITHER (a) the input r0 is not a world position on
  the console - the VS -> PS REGISTER LINKAGE (my change earlier today, 95a268c) would be implicated for this shader -
  OR (b) this control flow is translated differently from what the plugin runs. INDEPENDENT CHECK AVAILABLE: the SDK's
  DxbcShaderTranslator (the plugin's own, compiled into this build - the 406/406 differential) - read ITS control flow
  and input mapping for 338B8AB7 / D4686D15. Do not "fix" the nesting until that says which of (a) / (b) holds.
  WORKAROUND measured, not applied: dropping 338B8AB7 gives sky parity within ~1% in 3 of 4 boxes.
- ROW (j) FIXED - A XENOSRECOMP CONTROL-FLOW BUG (XenosRecomp d68e8f1, local branch fable2, third-party - never push).
  GROUND TRUTH: the plugin's own dump (dump_shaders, PSDUMP1; cloud PS = plugin shader_61976295517D7015, matched by
  microcode content) disassembles as an IF/ELSE: `(!p0) jmp L7; (p0) exec compare; (p0) jmp L11; L7: (!p0) exec lighting;
  L11: exece r0 *= c20.w`. The two forward jumps' ranges CROSS; XenosRecomp's "simple control flow" turns every forward
  jump into an if-block closed at its target, which cannot represent crossing ranges - the else-branch (the lighting)
  landed inside the then-block, dead, and visible pixels output the world-position interpolant x exposure. The register
  linkage (alternative (a)) is NOT implicated.
  FIX: the pre-pass records each forward conditional jump's [index, target) and falls back to the existing general
  pc/switch control flow when any two cross. OFFLINE: all 9 affected shaders re-translate to the switch form and compile
  (with the in-app NGPU_LOOP rewrite); dead-block census 9 of 92 -> 0 of 10 re-translated.
  DEPLOY: the 9 cached DXIL/layout pairs moved to out/build/win-amd64-Release/ngpu_cache_pre_crossjmp_20260925 (kept);
  the JIT rebuilt all 9 in-leg (03:56:43-45).
  SKYFIX1 (cloud draws ON, E 0.05), pre-registered: VALIDITY all 9 rebuilt - PASS; P1 CL <= 5 and CR <= 5 at every
  capture - PASS (CL 1.65-1.78, CR 1.79-1.87, unfixed 23-57; wander amplitude x1.04-1.17, box pairs now corr 0.98-0.999);
  P2 R within +-10% of 2.52 - PASS (2.52-2.55); ground within +-5% of SKYBASE1 - PASS (0.658-0.699 vs 0.661-0.700, <= 1%
  at every capture); lake unchanged (1.01-1.12).
  AGAINST THE PLUGIN (f1500 boxes): CL 1.66 / 1.65, CR 1.80 / 1.78, R 2.53 / 2.54, L 1.22 / 1.03. THE SKY IS AT PARITY
  WITH THE CLOUD LAYER DRAWING.
  UNREGISTERED OBSERVATION (visual only, shots/skyfix_before_after_plugin_20260925.png: before / after / plugin): the
  FERNS, black before, are RED-ORANGE after - matching the plugin - and green grass tufts appear where the plugin has
  them. The fern VS D4686D15 carried the most dead blocks (83). The open row "ferns still dark" should be RE-MEASURED
  (pre-registered) rather than closed on a picture.
- FERNS AFTER THE CROSSING-JUMP FIX, SCENE AGAINST SCENE (tools/native_gpu/scene_regions.py: native resolve F99DF000
  vs the plugin's copy, frame 1500; fern rect = the old parity rect, scene rows 382-502 cols 600-960, hero included in
  both arms). Baseline computed BEFORE the legs (ABSLVL1, pre-fix): ferns 0.813 (native/plugin), fern R/G native 1.17 /
  plugin 1.42; ground 1.221; near ground 1.217; sky R 1.007.
  Pre-registered (n=2, lower leg scored): P1a fern/ground relative ratio >= 0.90; P1b fern R/G within +-0.10 of the
  plugin; P2 ground 1.22+-0.05, near ground 1.22+-0.05, sky R 1.007+-0.05 (lake excluded - its second stage varies).
  ABSLVL2 / ABSLVL3 (identical to 3 decimals): ferns 1.036 / 1.036; R/G 1.38 / 1.38 (plugin 1.42); ground 1.218;
  near ground 1.217; sky R 1.008; sky CL 1.086 / 1.084 (was 25.1).
  VERDICTS: P1a FAIL (0.851 < 0.90) - the criterion assumed the ferns should carry the ground's x1.22 excess; they do
  not: ABSOLUTELY they are now at plugin parity (x1.036, from x0.813). Recorded as a failed criterion with the absolute
  result beside it, not re-scored. P1b PASS (1.38 vs 1.42). P2 PASS on all three.
  STATUS: the ferns' luminance and colour now match the plugin within ~4% scene-against-scene - the "ferns dark" row is
  resolved by the crossing-jump fix as far as this rect measures (size / alpha-to-mask not assessed). OPEN on the same
  instrument: the GROUND and NEAR GROUND at x1.22 (native brighter) and the LAKE at ~x1.5 at frame 1500.
- GROUND x1.22 (scene against scene, ABSLVL2 f1500) - first reads, row OPEN.
  SHAPE: near-UNIFORM - 8x8 block ratio native/plugin p10 0.99 p25 1.11 p50 1.21 p75 1.30 p90 1.41 (texture-level
  noise), and only weakly dependent on the plugin's brightness (quintiles 1.257 -> 1.177, dark to bright) - NOT the
  shadow shape (missing shadowing would concentrate in the dark blocks); a lighting-scale shape. Ferns sit at ~1 (parity).
  WRITER - NOT YET FOUND, one misattribution retracted: GROUNDWALK1 (every 2nd draw, seq 1180-1530) showed the ground at
  0 after B9B8BECA (seq ~1182) and 0.676 at seq 1527, crediting PS 2D5EA8E8 (VS A165FECD). 2D5EA8E8 is a textured
  dome pass (oC0 = texel.rgb * c31.xyz * texel.a * c20.w, slot 13 = 16C80000 1024x128 BC3, c31 = 0.357 grey, depth
  test GEQUAL no write) - RGBM-shaped but NOT a scene recomposite. GROUNDWALK2 (EVERY draw, 1176-1190 and 1515-1530):
  ground 0.662 (alpha 0.930) through seq 1181, 0 from B9B8BECA (1182-1190), and ALREADY 0.661 at seq 1526 (the sky PS),
  with no scene-target draw dumped between 1190 and 1526 - so 2D5EA8E8 is NOT the writer, and the ground is restored
  by something that is not a draw on the scene target (a copy/restore into it, or a draw on an aliased key). NEXT: log
  what writes the scene target between the B9B8BECA clear and the second pass (resolves, copies, alias clears,
  epoch/restore paths) and compare that path's output with the pass-1 content - the x1.22 may enter there.
- AFTER THE CROSSING-JUMP FIX - five follow-ups (claudecode-76's consolidated message):
  (1) WITHDRAWN (claudecode-76's own): "an even multiplier across a contiguous band cannot come from the per-pixel code /
      uniformity retires the geometry" - the cause WAS in the per-pixel code (a control-flow fault); and the ">= x70 scale"
      framing - there was never a scale: the shader emitted a different QUANTITY (a world position). RULE: "looks
      uniform" does not imply "is multiplicative" - a slowly varying wrong quantity is indistinguishable from a correctly
      scaled right one.
  (2) "CENSUS 9 -> 0" WAS TAUTOLOGICAL (the fix emits a different form, so the nesting detector reads 0 by construction).
      REAL CHECK - REACHABILITY (tools/native_gpu/switch_reachability.py: walks the pc/switch cases tracking p0's
      possible values on every path): all 10 re-translated files - EVERY predicated block reachable (D4686D15 85, B41EE8F1
      85, 19256301 63, 81B999B2 38, 6B210267 32, DBF9A58A 27, 338B8AB7 25, 8C74AF61 19, 6E1021DB 18; never-reachable 0).
      NEGATIVE CONTROL (tools/native_gpu/fixtures/deadctl_p.hlsl, one provably dead p0 block): flagged exactly 1 - the
      checker can fail.
  (3) WHAT THE NINE DRAW: D4686D15 + 6E1021DB with the fern PS A92D4C17 (covered by the fern measure); 338B8AB7 clouds
      (covered); DBF9A58A with PS 0B24BD42, 19256301 / 8C74AF61 with PS D6F02D0B (first pass), 81B999B2 (41 draws on the
      scene target, subject unidentified), B41EE8F1 / 6B210267 (not seen in any walk) - UNMEASURED BY SUBJECT. Covered
      instead by a SUBJECT-FREE whole-frame census (32x32 blocks, native/plugin [SCENE], f1500, pre ABSLVL1 vs post
      ABSLVL2/3): noise floor between the two identical post legs 15 of 858 blocks (|ratio-1| differs > 0.10; p99 0.109);
      toward parity 86 / 82 blocks (>> floor: a broad real improvement); away 11 / 9 (WITHIN the floor as a count), but
      9 blocks moved away in BOTH legs - WATCH LIST, not called regressions: scene (y, x) (256,0) (256,96) (320,320)
      (352,832) (352,928) (384,576) (384,640) (448,608) (448,640) - most around the fern bush / hero, where the ferns'
      shape changed.
  (4) FERNS: the re-measure used the scene-against-scene instrument (window ratios 0.37 / 0.68 are cross-path and were
      not the criterion); result x1.036 at parity. The old residual rows h1/h2 (fern faults on the window axis) should
      be re-examined against this before either is worked on - a good part of the fern decode work was plausibly aimed
      at a symptom of dead blocks in the fern VS (83).
  (5) SKY L BOX, OWN ROW: native 1.22 vs plugin 1.03 (x1.18) with the fix (was 1.43 / 1.03 before; native without clouds
      1.22). THREE of four sky boxes reach parity, not four.
- GROUND x1.22 - THE WRITERS FOUND (and two of my walk readings corrected on the way):
  * GROUNDWALK2's "no scene-target draw between 1190 and 1526" was an artefact of its two dump WINDOWS, not an
    observation; and its 0.662 before the mid-frame clear was the PREVIOUS FRAME's leftover (value-change detector blind
    to duplicates - GROUNDWALK4 without a clear read a flat 0.669 across seq 100-1180).
  * GROUNDWALK3 ABANDONED (world never loaded - heavy per-draw dumping at frame 1080, during loading, moved the
    time-based pad script); saves restored. Re-run at frame 2160.
  * GROUNDWALK5/6 (target cleared before its first draw, ngpu_dump_scene_clear): the ground is 0 after the clear in both
    halves until seq ~1522, where it appears in one step credited to the sky PS 63081965 - because per-draw scene
    dumps SKIP TESSELLATED draws unless ngpu_dump_tess_draws is set.
  * GROUNDWALK7 (tess draws included, every draw 1400-1530, cleared): the ground is painted by the TESSELLATED TERRAIN
    PATCHES, VS 6A56B867: PS 81B999B2 +0.479 and PS 5FEF6617 +0.188 (= the final 0.675).
  81B999B2 was one of the nine crossing-jump shaders (26 dead blocks) - but the ground ratio did not move across that fix
  (ABSLVL1 1.221 -> ABSLVL2 1.218), so its dead blocks were not the x1.22.
  Both PS: full lighting (sun c66-c68, light colours c11 c19-c22, c0 c28 c31 c45 c80 c87 c109), oC0.rgb = r0 * c20.w.
  NEXT: per-slot whitening and per-constant sensitivity on 81B999B2 / 5FEF6617 measured SCENE-AGAINST-SCENE on the
  ground rect (pre-register: the carrier moves native/plugin ground from 1.22 to within +-0.05 of 1.0, the ferns and
  sky R boxes unchanged), and their textures' formats / decode (gamma variants) against the plugin's load table.
- GROUND x1.22 - GAMMA TEXTURES NEVER LINEARISED (candidate, pre-registered 2026-09-25 before any leg):
  TERRREP1 slot report on PS 81B999B2 (surface 14010500): it samples slots 0 2 3 4 5 8 13 14; slots 0 / 13 are
  1024x1024 fmt 18 (DXT1). The run's sign census: fmt18 signs(xyzw)=3330 on 5 textures = GAMMA on rgb. No path
  honoured sign 3: every texture went up UNORM, so the shader got the ENCODED albedo. New ngpu_tex_gamma (default
  OFF) uploads a sign-3 BC1/2/3 texture as its sRGB twin (an approximation of the Xenos PWL curve; logged per
  texture, counted when no twin exists).
  PRE-REGISTERED: legs GAMMA1 (on), GAMMA0 (same binary, off), GAMMA1B (on), f1500, scene_regions.py. PASS = ground
  native/plugin within 1.00 +-0.05 in BOTH on legs, GAMMA0 reproduces ~1.22 (the control), and ferns / sky_R move
  by no more than max(0.10, 3 x sd of the two on legs) from GAMMA0. MAGNITUDE check (mechanism-on-cue): a fully
  gamma albedo linearised would darken far more than x1.22 if the albedo carried the whole ground - so a pass that
  lands well BELOW 1.0 is a fail as well, and the textures named in the GAMMA TEXTURE lines are the subject list.
  RESULT (GAMMA1 / GAMMA0 / GAMMA1B, [SCENE] f1500, native/plugin; saves restored, 0 copied back):
    ground       0.984 / 1.218 / 0.984   -> PASS (1.00 +-0.05 in both on legs; control reproduces 1.22)
    near_ground  0.982 / 1.217 / 0.982
    sky_R        0.989 / 1.009 / 1.004   -> unchanged (within 0.10)
    ferns        0.899 / 1.037 / 0.899   -> FAIL AS WRITTEN: moved 0.138 > 0.10.
    lake 0.993 / 1.523 / 1.519 and sky_CL 0.992 / 1.083 / 1.078: GAMMA1 alone differs, GAMMA1B (same setting) matches
    the control - the known lake second-stage timing variance (ORDER1), not the switch.
  THE FERN CLAUSE WAS MIS-SPECIFIED, measured not argued (new tools/native_gpu/region_pixel_shift.py, per-pixel ON/OFF
  in the fern rect): 50.8% / 51.6% of the rect's pixels moved, all by x0.807 (p10-p90 0.802-0.814 = the ground's
  own x0.806) - TERRAIN BETWEEN THE FRONDS - and those now sit at 1.031 / 1.021 of the plugin. The other half (the
  ferns themselves) did not move (ON/OFF 0.319 / 0.319) and are at x0.72 of the plugin in all three legs.
  => THE FERNS ARE NOT AT PARITY: native x0.72. The earlier "ferns x1.036" was a too-dark fern averaged with a
  too-bright ground. FERN ROW REOPENED (and the rows h1/h2 re-examination with it).
  CORRECTION: the gamma textures are 1B0F6000 768x768 (terrain PS slot 3, not slots 0 / 13 as written above) plus four
  small ones (1B0E3000 96x64, 1B0E6000 64x192, 1B0EB000 64x96, 1B0F1000 192x32), all DXT1 signs 3F.
  sRGB vs the plugin's PWL (rexglue-src dxbc_translator_fetch.cpp:2096 PWLGammaToLinear, applied after the fetch):
  sRGB/PWL 0.33 at 16/255, 0.82-0.93 at 64-160, 1.045 at 192, 0.985 at 224. The uniform x0.807 implies the terrain
  texel sits near 214/255, where the two agree (PWL 0.677, sRGB 0.678) - PREDICTION, texels not read. Exact PWL
  decode (CPU BC1 decode to 16-bit + PWL, or in-shader) remains owed for the low / mid range; ngpu_tex_gamma now ON.
- FERNS x0.72 (reopened above) - FIRST READINGS:
  * Fern-pixel character (GAMMA1 / GAMMA1B, the non-terrain half of the fern rect): native/plugin per-pixel median
    0.746, p10 0.32, p90 2.0 - the fronds do NOT align pixel for pixel (wind sway / alpha-test edges), so only region
    means are admissible. Mean per channel R 0.77 G 0.69 B 0.86-0.95; by row band 0.65 (top) -> 0.83-0.86 (lower).
  * FERNREP1 (slot report, PS A92D4C17 on 14010500): it samples 0 (DXT1 256^2 13F00000 or 128^2 13DE0000), 1 (16x16
    fmt6), 2 (DXN), 4 (64x1 fmt29), 5 (256x1 fmt6), 8 (12B1D000 k_8), 10 (32x32 fmt6). It BINDS slot 3 = the gamma
    terrain texture 1B0F6000 but never fetches it (consistent with the fern pixels not moving under ngpu_tex_gamma).
    Alpha test on (func 4, ref 0.502), blend ONE/ZERO.
  * K8DUMP1 (new ngpu_guest_k8_addr: the plugin's 12B1D000 read from guest memory at f1500): NOT A PLUGIN IMAGE -
    three byte values (0 x691200, 192 x168960, 255 x61440) in a periodic stripe fill. The plugin (resolution-scaled)
    keeps this resolve host-side; 12B1D000 cannot be compared scene-against-scene by guest read. The native
    f001500_res_F2B1C000 depth dump reads all-zero too (instrument unverified, not a finding).
  * RUNNING: FERNWHITE_{0,8,2,1,10} - the fern PS alone with one slot served white; control K8DUMP1. Reading =
    the fern-rect native mean against the control (terrain in the rect is untouched by a fern-only mask). Pre-registered
    reading only, not a fix criterion: a slot whose whitening moves the fern rect by >= 0.10 is a CARRIER CANDIDATE;
    the x0.72 deficit predicts a candidate whose content is wrong DARK (whitening raising the ferns).
- RECORD CORRECTIONS (claudecode-76, 2026-09-25):
  * WATCH LIST IS A FINDING, NOT TIDY-UP: 11 and 9 blocks moved away; random overlap of an 11-set and a 9-set among
    858 is 11 x 9 / 858 = 0.12 blocks - observed 9 (~75x). "Within the floor as a count" was true of each count and
    says nothing about the SET, which reproduces. The 9 coordinates are a reproducible local regression.
  * FOLDED INTO THE FERN ROW: they cluster around the fern bush, and the fern row has since changed sign (x1.036 was
    fern x0.72 averaged with ground x1.22). With gamma ON the ground half moved to parity, so the watch-list blocks
    must be re-scored on GAMMA1/GAMMA1B before being called fern regression or terrain; that re-score is part of the
    FERNWHITE reading.
  * CENSUS DENOMINATOR 9 vs 10: the population is 9 shaders. The 10th file in the reachability run was a DUPLICATE -
    tmp/xr_new/338B8AB7_p.hlsl (the standalone cloud re-translation) beside 338B8AB7891DDC44_p.hlsl - not a tenth
    shader and not the fixture (the negative control deadctl_p.hlsl was a separate run). Read "all 10 files" as
    "9 shaders, one of them twice".
- WATCH LIST RE-SCORED (native/plugin, 32x32 block, GAMMA0 / GAMMA1 / GAMMA1B; terrain share = pixels moved by gamma):
    (256,0) 1.116/0.929/0.929 t.81 | (256,96) 0.826/0.743/0.745 t.39 | (320,320) 0.716/0.691/0.694 t.15
    (352,832) 0.856/0.765/0.757 t.54 | (352,928) 0.781/0.729/0.736 t.41 | (384,576) 0.791/0.725/0.732 t.47
    (384,640) 2.580/2.182/2.181 t.61 | (448,608) 1.291/1.117/1.105 t.63 | (448,640) 2.187/2.008/2.038 t.29
  Two kinds: six blocks DARK (~0.7, the fern deficit) and two BRIGHT (x2.0-2.2 at x=640, rows 384-480).
  PICTURE (shots/2026-09-25_bowerlake_GAMMA1_fernbush_native_vs_plugin.png, scene 352-512 x 560-720, Reinhard +
  1/2.2 for viewing only): native fronds are SOLID, OVERSIZED spiky blades; the plugin's are fine and lacy. The
  native blades COVER THE HERO (only the lower coat and axe show); bright white specks sit at the bush's base (the
  x2 blocks). => the fern x0.72 is mostly COVERAGE (what is in front of what), not shading. The FERNWHITE sweep
  measures shading and is kept as a reading, but is not the subject.
  FERN ALPHA (A92D4C17 HLSL 903-906): oC0.w = step(saturate(1 - c15.y*(dist - c15.z)) + noise(slot 10 .w at screen
  pos * c50), 1) * albedo.w (slot 0, DXT1 256^2). Frond SHAPE = the slot-0 alpha under alpha test 0.502. Blobby
  solid blades are what a too-coarse MIP of a lacy alpha looks like. PRE-REGISTERED: NOMIPS1 (ngpu_tex_mips=false,
  level 0 only, same binary) - if the fronds turn lacy and the hero reappears (by picture AND the dark watch blocks
  moving toward 1.0 by >= 0.10), the carrier is the mip chain (its data or its selection); if unchanged, mips are
  excluded and the next candidates are the VS frond expansion (size) and the gate term.
- FERNWHITE RESULT ([SCENE] f1500 fern rect native/plugin; control K8DUMP1 0.900, gamma ON; ground 0.984 and sky_R
  0.99-1.005 in every leg = the mask stayed on the fern PS):
    slot 0 albedo 5.369 (alpha -> 1: whole cards, expected) | slot 8 12B1D000 0.899 (+0.000) | slot 10 gate noise
    0.899 (+0.000) | slot 2 DXN normal 0.783 (-0.117, CANDIDATE by the rule, moves AWAY) | slot 1 16x16 fmt6 0.994
    (+0.094, BELOW the 0.10 rule - not a candidate as registered, noted because it lands on parity).
  Slot 1 in the HLSL: r11.xyz = fetch, r5 = r11 * r11 (manual gamma-2 decode), an additive colour term. Whitening
  an additive term raises the sum; that alone says nothing about the content being wrong.
  12B1D000 has NO effect on the ferns now (was the dominant fern lever on the window axis before the crossing-jump
  fix: 0.366 -> 0.676) - that earlier lever is retired for the ferns.
  Shading sensitivity does not explain x0.72 (the subject is coverage, above); NOMIPS1 is the coverage test.
- NOMIPS1 (ngpu_tex_mips=false, level 0 only): the native fronds turn LACY (picture); dark watch blocks +0.02..+0.06
  (< the registered 0.10) -> FAIL AS WRITTEN on the blocks half. The hero stays missing with lacy fronds.
- TEXLVL1 (new ngpu_dump_tex_levels_addr + tools/native_gpu/bc1_levels.py; shots/2026-09-25_TEXLVL1_13F00000_levels.png):
  the fern albedo's uploaded chain L0-L4 is the SAME fern at every level (alpha coverage 0.31 / 0.37 / 0.45 / 0.57 /
  0.72 - DXT1 1-bit alpha fattens as it shrinks). Layout correct. Sampling: uniform control flow, implicit Sample,
  fetch constant trilinear, no bias, no aniso; host sampler trilinear, no aniso.
- THE REFERENCE WAS ENHANCED - EVERY SCENE-AGAINST-SCENE FIGURE ON TEXTURED SURFACES IS AFFECTED.
  out/build/.../fable2_settings.cfg: resolution_scale=2, anisotropic=1, texture_pack=1, texture_ai=1, texture_scale=2
  (log: "GPU: internal scale 2x2"). The plugin drew the AI texture pack (fable2tex2) at 2x with anisotropic filtering;
  the native path draws the game's own textures at 1x. REF1X (plugin unenhanced: pack/AI off, 1x, no aniso,
  texture_scale 1; settings restored, hash verified; log "internal scale 1x1"):
    ferns 1.042 | ground 0.976 | near_ground 0.971 | sky_R 1.008 | sky_CL 1.094 | lake 1.306
    watch blocks: (256,96) 0.857 (320,320) 0.998 (352,832) 0.985 (352,928) 0.990 (384,576) 0.996 | bright:
    (384,640) 2.490 (448,640) 2.127 (448,608) 1.186
  PICTURE (shots/2026-09-25_bowerlake_REF1X_native_vs_plugin1x_vs_pluginEnhanced.png): the unenhanced plugin draws
  the SAME solid spiky fern blades as native. => "FERNS x0.72" WAS THE TEXTURE PACK, NOT THE PORT. The fern shape,
  the mip question and the FERNWHITE sweep are closed; ferns at parity (x1.042) against the game's own textures.
  The GROUND gamma fix stands on the unenhanced reference too (0.976).
  RULE FROM HERE: scene-against-scene legs run through tools/native_gpu/bridge_leg_ref1x.sh (D:\fable2_flash\gameplay).
  Figures measured against the enhanced reference remain valid only for untextured / smooth regions (sky, lake
  means) and only as far as REF1X corroborates them: sky_R 1.008 (was 0.99-1.00), sky_CL 1.094 (was 1.08), lake 1.306
  (the lake's stage timing varies, ORDER1).
  REAL DIFFERENCES LEFT IN THIS VIEW: (1) THE HERO'S UPPER BODY AND LEGS ARE NOT DRAWN natively (the plugin shows the
  whole coat, legs, boots and hat; native shows the lower coat and the axe) - NEW ROW; (2) bright white specks at the
  fern bush's base (blocks (384,640) / (448,640) at x2.1-2.5); (3) sky_CL x1.09 / sky L x1.18; (4) the lake.
- HERO MISSING - CAUSE FOUND: XenosRecomp DROPPED THE CONDITION OF CONDITIONAL EXEC CLAUSES.
  Walk: HEROWALK1/2/3 (REF1X settings, per-draw, cleared): the hero's body is never painted; a hero-shaped hole
  is cut from the ferns at seq 626, i.e. depth present, colour absent. RANGECLAMP1 (ngpu_range_clamp) and
  HERODEPTH1 (depth test off for PS 9A193FEB) both leave it missing; HERODEPTH1 shows a flat white shape on the ground.
  SKINFLAT1 (ngpu_skin_debug=1: skinned meshes only, flat, no depth): the skinned meshes land as ONE small flat quad
  at the hero's feet - the body COLLAPSES. Rigid attachments (axe, coat tails; VS 2A5207A8 = WVP x position) draw.
  The skinned VS 1CF397A6 (bones from stream 3 by blend index, 4 weights) ends:
    r0.xyz = -abs(r0.xxx) > c255.xxx;   (= 0 always: the skinned position overwritten)
  The plugin's disassembly of the same microcode (shader_82F6433A69263C75, matched by byte-swapped ucode):
    /* 2.1 */ cexec b0
    /* 35  */   sgt r0.xyz_, -r_abs[0].xxxx, c255.xxxx     <- the game's "collapse this mesh" switch, under b0
  shader_recompiler.cpp read a CondExec's address / count / sequence and never emitted its condition (neither the
  bool form cexec bN nor the predicate form (p0) exec); shouldReturn also tested CondExecEnd twice (never
  CondExecPredCleanEnd).
  FIX (XenosRecomp local fable2, NOT pushed - third party): the clause body is wrapped in if (bN != 0 / == 0) or
  if (p0 / !p0) by the instruction's condition bit; END still ends after the clause either way. Polarity checked on
  1CF397A6: "if (b0 != 0) { r0.xyz = ... }". BLAST RADIUS: old vs new exe over all 93 cached containers - 40 differ
  (terrain 81B999B2 / 5FEF6617, ferns A92D4C17 / D4686D15, sky 63081965 / 338B8AB7, the skinned VS family, 9A193FEB...);
  CFB18804 crashes both exes (new 5/12, old 7/12, a different output every run - pre-existing). Deploy: 38 cached
  DXIL/layout pairs moved to out/build/.../ngpu_cache_pre_condexec_20260925 (kept); old exe kept as
  build/xenosrecomp/XenosRecomp/XenosRecomp_pre_condexec.exe.
  PRE-REGISTERED (CONDEXEC1, REF1X settings, f1500, against REF1X): (a) the hero's body is drawn (picture) and the
  hero rect (scene 330-500 x 600-690) native/plugin goes from 1.17 to 1.00 +-0.10; (b) no region moves AWAY from 1.0
  by more than 0.05 (ferns 1.042, ground 0.976, near 0.971, sky_R 1.008, sky_CL 1.094); (c) whole-frame 32x32 census
  vs REF1X: blocks moving away <= 15 (the identical-leg floor).
  RESULT CONDEXEC1 (both forms, 38 pairs re-translated): FAIL - hero rect 1.348 (was 1.172); ferns 1.289, ground
  3.451, sky_R 0.166, sky_CL 0.283, lake 0.501, near 1.970; census toward 97 / AWAY 691 (floor 15). Picture: the
  world's shading collapsed to flat colours; the hero's HAT appears. REVERTED (exe restored by hash 39cdfe58;
  the 38 pairs back; the fresh ones kept in ngpu_cache_condexec1_20260925).
  ISOLATION: XenosRecomp now takes XR_CONDEXEC = bool | pred | both | none (build default "bool" for the test).
  Bool-only changes 13 cached shaders (the six skinned VS 1CF397A6 2D96AFB1 36F4DD57 82DF8BF6 A382DE5A E8F02904 -
  the b0 collapse clause - and PS 1D2667F2 3EA51176 9A193FEB B9B8BECA E33859F0 2D87AFE0).
  RESULT CONDEXEC2 (bool only): FAIL - ferns 1.038, ground 0.976, near 0.971 (unchanged), sky_R 0.537 (was 1.008),
  sky_CL 0.849, lake 0.659; census toward 118 / AWAY 204. Picture: the hat draws, the BODY STILL DOES NOT.
  REVERTED the same way (fresh ones in ngpu_cache_condexec2_20260925).
  READING: honouring the condition is right (the plugin's disassembly says so, and the hat - a skinned piece -
  appears the moment b0 is honoured), but the BOOL VALUES the native path hands the shader are wrong: the sky PS
  clauses are skipped (sky x0.54) and the body's draw evidently sees b0 SET. This is the long-open "boolean
  constants, source unknown" (the replay feeds blk[136..143] = g_ring.regs[0x4900..0x4907]; the code's own note:
  "neither source is known to be right"). Until now NO translated shader consumed a bool (every cexec condition
  was dropped and no CondJmp used one), so the wrong source was invisible. The pred form (CONDEXEC1 minus CONDEXEC2)
  is not yet isolated; it carried most of CONDEXEC1's damage and must be tested on its own AFTER the bools are right.
  NEXT: measure the bool source - per-draw 0x4900..0x4907 against the plugin's register file / the PM4 writes that
  set them - for the hero body draw (b0 must be 0) and the sky PS (its gated bits must be 1).
- BOOL SOURCE MEASURED AGAINST THE PLUGIN (claudecode-76's criterion: the plugin's per-draw values, not what the
  picture needs - otherwise a wrong VALUE and a wrong POLARITY predict the same darkening). New: BridgeDrawRec.bools[8]
  = the plugin's d->regs[0x4900..0x4907] whole at record time; ngpu_bool_trace_vs (VS or PS hash) prints them beside
  the words the replay hands the draw. BOOLTRACE2 (skinned VS 1CF397A6): 39 of 39 draws EQUAL; BOOLTRACE3 (sky PS
  E33859F0): 60 of 60 EQUAL. b0 = 0 on every skinned draw; the sky's word 4 = 0x110 (b132, b136 set; b137 clear).
  POLARITY on a PS too: the plugin's disassembly of the sky PS (shader_A17D8AEC3A817D45 - NOTE the 48-byte prefix
  also matches F6D98C7B, so the match is not unique) has "cexec b137" x2 and "cexec b136" x3; the bool-only
  translation emits if (b137 != 0) x2 and if (b136 != 0) x3 - same bits, same direction.
  => the bool SOURCE and the bool POLARITY are both right. The earlier "the bool values are wrong" reading
  (CONDEXEC2) is RETRACTED - it was inferred from the outcome, the thing this entry now forbids.
- RESULT CONDEXEC3 (pred only, XR_CONDEXEC=pred inherited by the JIT - verified in the emitted HLSL: no bool wraps,
  fern PS 13 predicate wraps vs 11): FAIL on the census - toward 120 / AWAY 103 (floor 15); ferns 1.077 (was 1.042,
  away 0.035), ground 0.978, near 0.971, sky_R 0.998, sky_CL 1.145 (away 0.051), LAKE 1.026 (was 1.306), hero 1.175
  (unchanged: it needs the bool form). Picture (shots/2026-09-25_CONDEXEC3_predonly_native.png): new pastel pink /
  violet / blue grass tufts and noisy far hills. REVERTED (exe hash 39cdfe58, cache restored; fresh ones kept in
  ngpu_cache_condexec3_20260925).
  THREE ARMS, ONE READING: honouring exec conditions is what the hardware and the plugin do, and each form fixes
  something (bool: the hero's skinned pieces un-collapse - the hat; pred: the lake to 1.026) while breaking something
  else. With the values and polarity verified, the remaining suspects are in HOW the translation composes with the
  new wraps: (1) state the old unconditional clauses left behind that later instructions came to depend on (a
  latent second defect masked by the first - e.g. a (p0) clause whose p0 comes from a SETP the recompiler
  mistranslates, which was harmless while the clause always ran); (2) the per-instruction predicate / push
  semantics (setp_*_push writes p0 AND a vector result; the recompiler emits p0 = src0 == 0 && src1 op 0 with
  multi-component operands). NEXT: pick ONE shader that regresses under pred-only (the pastel grass) and ONE that
  improves (the lake's), and diff their translation against the plugin's disassembly clause by clause.
  claudecode-76's other points: (b)'s +-0.05 and the hero's +-0.10 were NOT measured on identical REF1X legs - they
  are unsourced tolerances and are withdrawn; the census floor (15 of 858) stands, measured on identical legs.
  The "40 differ" count includes CFB18804 only by its non-determinism - it is a SAMPLE for that one container,
  a count for the other 39.
- CONDEXEC4 PRE-REGISTERED: bool-only XenosRecomp for the SIX SKINNED VS ONLY (1CF397A6 2D96AFB1 36F4DD57 82DF8BF6 A382DE5A E8F02904 - their one change is the b0 collapse clause; every PS keeps its old translation). PASS = the hero body drawn (picture, against the REF1X plugin frame) AND census away <= 15 (identical-leg floor). No per-region tolerance (the unsourced +-0.05 is withdrawn).
- INPUT TYPING DEFECT FOUND (SKINWALK1, CONDEXEC4 build): the giant sheets = VS 36F4DD57 (seq 643, +192,204 px rows 0-340). Its container names BLENDINDICES on the fetch at byte 16 that carries the blend WEIGHTS (plugin twin shader_D4D558DA6A82BDC8: Offset=3 8_8_8_8 NumFormat=integer = indices -> r2; Offset=4 8_8_8_8 normalised = weights -> r4; Offset=5 16_16_FLOAT = texcoord -> r5). MapVertexFormat forced every BLENDINDICES to R8G8B8A8_UINT, so the weights arrived 0..255: every vertex blended 255x - hidden while the b0 clause collapsed everything. FIX: BLENDINDICES is integer only when its fetch is (num_format != 0); FixHlsl retypes a uint4 declaration on a normalised 8/16-bit fetch to float4. Census of all cached layouts: 1 such input (36F4DD57); the 5 other BLENDINDICES are genuine integer fetches, unchanged.
- CONDEXEC5 PRE-REGISTERED: CONDEXEC4 deployment (six skinned VS bool-only) + the typing fix (36F4DD57 re-JIT). PASS = hero body drawn (picture vs REF1X plugin) AND census away <= 15.
- RESULT CONDEXEC5: (a) PASS - the hero drawn whole (hat, coat, legs, boots; picture shots/2026-09-25_CONDEXEC5_hero_native_vs_plugin.png); (c) FAIL AS WRITTEN - census toward 196 / AWAY 24 (floor 15). Regions: sky_R 0.998, sky_CL 1.033, lake 0.993 (REF1X 1.306), ferns 1.034, ground 0.976, near 0.971. 20 of the 24 away blocks sit in the LAKE BAND (scene rows 224-288), the region with a known run-to-run second-stage variance (ORDER1); the floor of 15 was measured on pre-REF1X legs. NEXT (pre-registered before running): CONDEXEC5b/5c, identical to CONDEXEC5 - the floor for THIS configuration is the per-block |ratio change| > 0.10 count between identical legs (n=3, report the spread); the FAIL above stands whatever they show, and whether the 24 exceed that floor is a separate, stated reading.
- FLOOR RE-MEASURED, AND THE LAKE IS TRI-STATE: CONDEXEC5 / 5b / 5c (identical): 5 vs 5b differ in 5 blocks; 5c landed
  with the lake at 0.371 and differs from both by 178 / 187 blocks. REF1X itself was a third state (lake 1.306).
  So a census against ONE REF1X leg measures the lake's state as much as any change. Old-shader baselines in matched
  states (old translations swapped back temporarily, then restored): REF1Xb lake 0.372, REF1Xc lake 0.993.
  STATE-MATCHED VERDICT: CONDEXEC5 vs REF1Xc toward 2 / away 2; CONDEXEC5b vs REF1Xc 2 / 7; CONDEXEC5c vs REF1Xb 2 / 2;
  identical-leg floor (5 vs 5b) 0 / 5. => OUTSIDE THE HERO THE FIX MOVES NOTHING BEYOND THE FLOOR. The pre-registered
  (c) FAILED as written against REF1X; that failure was the lake's state (instrument confound), and the matched
  comparison is the reading that stands. RETRACTED: "lake 1.306 -> 0.993" and "sky_CL 1.094 -> 1.033" as effects of
  the fix - both are the lake/sky state (REF1Xc shows the same numbers with the old shaders).
  Also re-read in this light: CONDEXEC3's census (pred-only, away 103) was scored against the tri-state REF1X too; its
  PICTURE artefacts (pastel grass) are real, its census figure is not interpretable.
  DEPLOYED (local): XenosRecomp default XR_CONDEXEC=bool (commit 2b4bd5a; exe 19b88b1d), the six skinned VS
  re-translated, 36F4DD57 re-typed. The predicate form stays OFF (open: the pastel-grass defect).
  RULE: any census from here pairs each leg with a baseline in the SAME lake state (read the lake ratio first).
- CORRECTIONS (claudecode-76) AND THE STATIC CHECKS:
  * The verdict above is restated: state-matched readings 2/2, 2/7, 2/2 and now 2/2 (CONDEXEC5d vs REF1Xb) - THREE OF
    FOUR AT THE FLOOR, ONE (5b vs REF1Xc, away 7) TWO BLOCKS ABOVE the 0/5 floor. Not "nothing beyond the floor".
  * Floors are WITHIN A LAKE STATE and each rests on n=2: lake ~0.99 state 5 blocks (5 vs 5b); lake 0.37 state 0
    blocks (5c vs 5d - CONDEXEC5d, run to take the floor to n=3, landed in the OTHER state). Cross-state 178-187.
  * The NEXT step's "improving subject = the lake under pred-only" is WITHDRAWN: CONDEXEC3's lake 1.026 sits inside the
    old-shader spread (REF1Xc 0.993). Only the pastel grass (a picture artefact) is a valid pred-only subject.
  * STATIC (no leg): push-SETP scalarisation - XenosRecomp picks the operand components by the WRITE MASK where the
    hardware tests src0.w and src1.x. A real flaw, LATENT here: over all 238 plugin dumps, 107 push-SETPs, 0 whose
    swizzles make the two rules disagree (all replicated); checker negative control flags a synthetic xyzw case.
    And: SETP inside a bool-gated clause feeding a later predicate clause - 0 (441 bool clauses in 72 dumps, 582
    predicate clauses, 0 SETPs inside bool clauses). Both "latent second defect" mechanisms are excluded for this corpus.
  * Weak (claudecode-76): ground 0.976 stable across legs may be the sRGB-vs-PWL gamma row, not a ground row.
- USER REPORT (live play, 2026-09-25, after the hero fix): "getting close - BEACH AREA TEXTURES MISSING, the HALO
  EFFECT ON THE CHARACTER MISSING, and FAR AWAY SOME TEXTURES ARE MISSING." Three new rows, unmeasured:
  (u1) beach textures; (u2) the hero's halo (a rim/glow pass - plausibly a PS whose bool/pred clause is now honoured,
  or the pred form still OFF); (u3) distant textures (open item "blocky distant trees" / impostor cards).
- BEACH (user row u1) TRACED: the lake PS E33859F0 takes its shallow-water alpha from slot 1 (plugin disasm instr 10-13: r4.w = 1 - sat(c141.x - tf1.x * c141.y) -> oC0.w, blend SRC_ALPHA/INV_SRC_ALPHA). Slot 1 = 64x64 k_8 SHORE MASKS, RESOLVED ONCE AT LOAD from an 80x80 target (FBB47000.. 9 of them). The plugin (1x) writes them to guest memory: texguest_1BB4A000 = a smooth shoreline mask (mean 0.46). The NATIVE resolve FBB49000 (same patch) is ALL ZERO in every channel => alpha 1 everywhere => opaque water covers the sand (ADAPTONLY1 shows the adaptive terrain DOES draw the sand). The native replay turns away ~280 draws/frame as "not an indexed draw" = auto-index prim1 67,560 / prim4 30,120 / prim8 18,120 per window; ngpu_replay_autoindex has been OFF since AUTOIDX1 (tested 2026-09-24 on the then-broken frame). PRE-REGISTERED SHOREAI1 (autoindex ON, one switch): native FBB49000 non-zero and shaped like the plugin mask; sand visible at the shore by picture; census read against a same-lake-state leg.
- BEACH CAUSE NAMED (SHORERES1 / SHORESKIP1): at frame 958 the 80x80 target 00800050 is resolved (8x16, 16x32, 32x32 ... the shore masks) with 0 native draws on it. Every draw aimed at it is skipped as "auto-index primitive not drawn": 26 RECT_LISTs (prim 8, count 3, VS 1B455640 inline, PS 1C6D4F80) and 34 single-point point lists. SHOREAI1 (ngpu_replay_autoindex on): FAIL - mask still all zero (the CPU rect expansion, ngpu_rect_list, fails for ~3,600 rect draws per window: stream shorter than the draw, and 11 shaders whose vertices carry no POSITION take a GUESSED diagonal). NEW: ngpu_rect_gs + src/ngpu_shaders/ngpu_rect_gs.hlsl (gs_6_0; tools/ngpu_shaders.cmd) - the plugin's method after the translated VS: longest clip-space xy edge = diagonal, fourth vertex = -a + b + c on EVERY output (the 42 cached VS share SV_Position, TEXCOORD0..15, COLOR0/1). PRE-REGISTERED RECTGS1 (ngpu_rect_gs on, one switch): FBB49000 non-zero and shaped like the plugin mask; sand at the shore by picture; census against the CONDEXEC5-family leg in the same lake state, floor quoted.
- RESULT RECTGS1: the GS loads and 1,882 rect draws per report go through it, but FAIL on the mechanism - FBB49000 still all zero, and SHORESKIP2 (rect GS on) shows what remains aimed at 00800050: 60/60 capped notes are single-point POINT LISTS (prim 1, count 1, PS 1C77EF80) - the shore mask is SPLATTED with point sprites. NEW: ngpu_point_gs + src/ngpu_shaders/ngpu_point_gs.hlsl (the plugin's sprite expansion: diameter PA_SU_POINT_SIZE x 2/16 px, NDC radius x 1/viewport extent, x W; zero-size dropped); the shared block grows to c48 (784 bytes) carrying those constants. Not yet: per-vertex size, point-sprite coordinates in param gen. PRE-REGISTERED POINTGS1 (rect GS + point GS on): FBB49000 non-zero and shaped like the plugin mask; sand at the shore (picture); the halo in the window capture; census vs a same-lake-state leg, floor quoted.
- POINTGS1 RESULT: point sprites issue (28,368 per report) but FAIL on the mechanism - FBB49000 still zero. SHORESKIP3 (tracer limited to frames < 1000): at load NO draw on 00800050 reaches the replay at all - only the resolves (frame 965, each x3). CAUSE: BridgeLogHandOff swapped the new frame over a pending ready frame the replay had not consumed; the resolves were carried (an earlier fix) but the DRAWS were discarded - a one-shot pass resolves an EMPTY target. NEW: ngpu_bridge_accumulate (append the new frame to the pending one; delta / patch / after_draws offset; cap ngpu_bridge_accumulate_cap 200000) + BRIDGE FRAMES counters (unconsumed / merged / lost). PRE-REGISTERED ACCUM1 (accumulate + rect GS + point GS): load-time SMALL RESOLVEs see > 0 native draws; FBB49000 non-zero and shaped like the plugin mask; sand at the shore (picture).
- ACCUM1 RESULT (accumulate + rect GS + point GS): the hand-off defect is REAL AND LARGE - "BRIDGE FRAMES: 106 handed
  over while the previous frame was still unconsumed - 106 MERGED, 0 LOST" (before the switch those 106 frames' draws
  were discarded every run). The load-time SMALL RESOLVEs now see 10-16 native draws. FBB49000 is NO LONGER ZERO
  (mean 0.374, max 0.759) but is a flat TRIANGLE, not the plugin's shoreline -> mechanism FAIL as registered (shape).
  Picture: the lake now shows the lake bed through a triangular region - worse than before; the switch stays OFF.
- MASK CHAIN TRACED (ACCUM2 / MASKSRC1, ngpu_slot_report_ps=1 = any PS on the surface): per mask the replay draws ONE
  rect (drawn) and skips the resolve packet itself (copy_draw = edram_mode COPY, correct). The load-time blit is PS
  05E3BDD0 (frame 960); its slot 0 = an 8x16 k_8 texture at fetch 1B4E2000 = the RESOLVE FB4E1000 of the previous
  step - a ping-pong chain (80x80 target -> 8x16 / 16x32 / 32x32 / ... resolves -> sampled by the next blit). Natively
  that texture was served as an UPLOAD FROM GUEST MEMORY (uploads 1, tick 962), not as the native resolve: at the
  draw, no native resolve for that address had been registered yet. So the chain reads stale guest bytes. NEXT: the
  ORDER of resolve vs consuming draw inside the replay for this chain (and whether a carried resolve with after_draws
  0 lands before or after its consumers) - a resolve-positioning question, not a shader one.
- ALSO SEEN: with the rect GS the full-screen composite stays whole (no half-frame diagonal) and the far tree canopy is
  visibly more complete (RECTGS1 window vs CONDEXEC5) - a lead on user row (u3) "far away textures missing".
- POINT SPRITES NEVER REACHED THE SCREEN: DROP SITES (ACCUM1) - DrawTranslated refused every non-indexed draw with count < 3 (368 a frame): a point list has ONE vertex per point, so every sprite the replay issued under ngpu_point_gs died there. POINTGS1's "no halo" is therefore UNTESTED, not negative. Fixed: count >= 1 for prim 1. Other drop sites per frame: 386 the hooked DrawVertices (off in these legs), 37 range refusals, 32 impostor skips. PRE-REGISTERED POINTGS2 (point GS + rect GS, accumulate OFF): the halo appears (window crop vs plugin); the count<3 drop site falls to ~0; census vs a same-lake-state baseline.
- POINTGS2 RESULT: the count<3 drop site is gone (drawn 2,077/frame vs 1,720). The DISTANT TREE CANOPY IS POINT SPRITES - now drawn, but as hard SQUARE blocks: the PS shapes each sprite with the point coordinates, which the translated param gen did not carry. No halo (not a plain sprite; row u2 stays open). NEW (XenosRecomp local, exe d39c76d9; old kept as XenosRecomp_pre_pcoord.exe): every VS writes out float4 oPCoord : PCOORD = 0, every PS reads iPCoord, and the param-gen prologue on iPCoord.z > 0.5 sets the point flag (Y sign bit) and ZW = saturate(sprite UV) as the SDK does; both GS carry PCOORD (point GS writes the corner UVs). Whole cache moved to ngpu_cache_pre_pcoord_20260925 (212 files) for a full re-JIT. PRE-REGISTERED PCOORD1 (point GS + rect GS + PCOORD): the far canopy sprites become shaped, not square (picture); the hero stays whole; census vs the same-lake-state CONDEXEC5-family leg, floor quoted.
- PCOORD1 RESULT: FAIL (picture) - the hero, ferns and grass tufts turn PASTEL lavender/blue (shots/2026-09-25_PCOORD1_
  pastel_native.png) - the same look as CONDEXEC3's pastel grass. 103 shaders re-JITed, 1 JIT failure. Adding one
  interpolant to every VS/PS broke input delivery for some pixel shaders. REVERTED: exe 19b88b1d (bool-only, no
  PCOORD), the 106 prior translations restored, the new ones kept in ngpu_cache_pcoord1_20260925; both GS sources and
  DXIL back to the pre-PCOORD versions. XenosRecomp PCOORD commit stays in its local history (exe kept as
  XenosRecomp_pcoord.exe) for the offline diagnosis: CONDEXEC3 and PCOORD1 share a symptom, so the pastel colour
  likely comes from ONE mechanism both changes touch (param gen / interpolant assignment) - to be diffed offline.
  STATE NOW: exe bool-only (hero fix), ngpu_rect_gs / ngpu_point_gs / ngpu_bridge_accumulate all default OFF.
- USER REPORT (2026-09-25, live): "the menus have issues and the load screen with animation still not showing. Text
  wrong colour, missing effect on the title and wrong text colour." New row (u4): FRONT END - none of the legs reach it
  (they load straight into Bower Lake); needs its own capture route.
- FRONT END (u4) CAPTURED (new D:\fable2_flash\gameplay\frontend_leg.sh: both windows every 2 s from launch; FRONT1): menu text is RED GLYPHS ON OPAQUE BLACK QUADS (plugin: cream/gold letters), the painted title background is missing (right half black), and the load screen is a dark-red frame with a thin shape. Cause of the text: the font is k_8 (R8), sampled .xyzw it gives (r, 0, 0, 1); the plugin's texture cache replicates narrow formats (RRRR / RGGG / RGBB per format; rexglue d3d12/texture_cache.cpp host_formats) and the native view did so only for DXT3A. NEW ngpu_host_swizzle (default OFF) with the full per-format table. PRE-REGISTERED FRONT2 (host swizzle on): menu text shows letter shapes in gold with no black quads (picture); the world census at f1500 vs the same-lake-state CONDEXEC5-family leg, floor quoted.
- FRONT2 RESULT: PASS - the menu letters are shaped and gold (shots/2026-09-25_FRONT2_menu_native_vs_plugin.png; slightly dimmer than the plugin); the user confirmed live ("menu looks better, correct letter colors now"). World census vs the same-lake-state leg CONDEXEC5b (lake 0.994 vs 0.992): toward 5 / away 1 (floor 5). ngpu_host_swizzle now ON by default. Still open in the front end: the painted title background is cut off (right part black) and the animated load screen does not appear.
- FRONT3 (rect GS + point GS on, host swizzle on): the FRONT END BREAKS - a solid green screen, then black where the menus and the save cards should be (shots/2026-09-25_FRONT3_strip_gs_on.png vs FRONT2_strip.png). The GS paths stay OFF by default; a full-screen rect or sprite draw in the front end is what they get wrong - to be attributed before they can ship. FRONT2 (default build) timeline: title painting OK; main menu with the right part black (the painting stops at a vertical edge); save-card fan OK; the load screen is black with "Loading Bower Lake" text only (the plugin's animated load screen absent); world OK.
- claudecode-76 REVIEW, ANSWERED:
  (1) HAND-OFF LOSS AT THE MEASUREMENT FRAME, accumulate OFF (FRONT2 BRIDGE FRAMES, cumulative): 18 frames lost by
      world load (~8,300 draws - the one-shot load passes), then 11 more over ~66 s in the world (~0.3% of frames).
      A lost frame is dropped WHOLE and every replayed frame records in full (first draw carries full state), so the
      f1500 dumps are COMPLETE frames: prior measurements stand. RETRACTED: my "~80% of guest frames never replayed"
      (an inference from two counters with different units, never measured).
  (5) HOST SWIZZLE TABLE checked by script against the producer, keyed by NAME (tools/native_gpu/host_swizzle_check.py;
      a positional compare misaligned by one entry on each side and invented 35 differences): 2 REAL ERRORS fixed -
      57 k_32_32_32_FLOAT is RGBB, 60 k_CTX1 is RGGG; 6_5_5 has no producer entry. Now 0 differences over 63 formats.
  (4) EARLIER NEGATIVES TAKEN WHILE NO POINT LIST COULD REACH THE SCREEN (the count<3 site): every leg before POINTGS2.
      Any conclusion of the form "X is not drawn / not the cause" where X could be a point sprite (halo, particles,
      the distant canopy, the shore-mask splats) is VOID, not negative. The drop census keys by NgpuDropAt source line;
      returns without a NgpuDropAt are not counted - an unnamed-exit bucket is owed.
  (3) The GS paths: the front end breaks with the GEOMETRY SHADER (post-VS), which does not use the CPU expansion's
      guessed diagonal - that guess is not the carrier here; attribution still owed (which front-end draw).
  (2) The pastel: diagnose on CONDEXEC3 (~13 shaders, grass only) - the minimal reproducer - before PCOORD1.
  STANDING PRE-REGISTRATION COLUMN from here: "UNMASKED A SECOND DEFECT?" - five correct-looking changes today each
  exposed another (cexec -> BLENDINDICES, accumulate -> resolve ordering, point GS -> count<3 -> PCOORD, PCOORD -> pastel).
- MENU BACKGROUND (u4): during the menu the replay turns away ~5,100 rect lists and ~28,600 point lists per 120-frame window; the CPU rect expansion handles ~2,400 (ALL on a guessed diagonal - no POSITION) and fails ~2,700 ("more vertices than the stream held"). FRONT3 had both GS paths on at once, so its green/black screens are not yet attributed. PRE-REGISTERED FRONT4 (rect GS only): menu background complete (no black right part), no green/black full screens (picture); world census vs the same-lake-state leg, floor quoted; column: unmasked a second defect?
- FRONT4 RESULT (rect GS for ALL rects): the MENU BACKGROUND IS COMPLETE and the "FABLE II" title card appears (picture PASS; FRONT3's green screens were the POINT GS). World census vs same-lake-state CONDEXEC5d (lake 0.37): toward 26 / AWAY 16 (floor 0) - all in rows 0-96: the far tree cards go BOXY (full rectangles) - neither native version matches the plugin's tree shapes. Unmasked a second defect: YES (the far cards' content/alpha). NEW: ngpu_rect_gs now = GS only as a FALLBACK for rects the CPU expansion cannot build; ngpu_rect_gs_all restores all-rects. PRE-REGISTERED FRONT5 (rect GS fallback): menu background complete (picture); world census within the floor vs the same-lake-state baseline, far canopy unchanged.
- FRONT5 RESULT (rect GS as fallback): world census vs same-lake-state CONDEXEC5b (lake 0.99): toward 7 / AWAY 80
  (floor 5) - FAIL; picture: the far pink canopy is overlaid with WHITE/GREY FLAKES. Unmasked a second defect: YES.
  RECTFAIL1 (new per-shader tally of rects the CPU expansion cannot build): the MENU population is DFB44E4B alone (the
  menu background - drawn right by the GS); the WORLD population is A165FECD (~2,880 / window) and 8A0AE541 (~480).
  RECTTRACE1: A165FECD is a GENERIC SCREEN-SPACE VS feeding many never-drawn passes - PS 7A850C75 (a 106-rect batch
  on the 640x360 target 0A000280), PS 2AC2D68D (320x180 target 05000140), PS A8EAA0D5 / 2D5EA8E8 on the scene,
  ACB18EE7 / 7D59AFCA / 122DE50E on 0A000280. These are post passes (haze / DOF / bloom family) the native path has
  NEVER DRAWN; drawn now, their inputs are not right yet -> the flakes. That is a new row (post chain), not a GS fault.
  DECISION (recorded): ngpu_rect_gs stays OFF by default - it would fix the menu background but bring half-built post
  passes into the world. NEXT for the menu: either the post chain rows first, or a principled scope (the GS only for
  draws whose target is the SWAP/front-end surface) - to be discussed, not slipped in.
- USER DECISION (2026-09-25): "Scope it" - the rect GS only outside the world. RECTPOP1 showed the menu's failing rects (DFB44E4B) draw to the 320x180 / 640x360 intermediates, not the swap surface, and the world's fallback hits the swap surface too - so the scope is the GAME SCENE (fable2::WorldCameraLive()), not a surface; ngpu_rect_gs_world lifts it. PRE-REGISTERED FRONT6 (rect GS on, scoped): menu background complete (picture); world census within the floor vs the same-lake-state baseline; the fallback population log has NO world-frame entries.
- FRONT6 RESULT: the scope HOLDS (fallback population log: front-end entries only, none in the world) but the menu background is cut again - in fallback mode the CPU-expandable menu rects keep the guessed diagonal. World census 19 toward / 22 away vs CONDEXEC5b although the GS never acted there: cause = my count<3 fix, which let INDEXED point lists draw as 1-pixel points with the sprite GS off (a default-build change I did not intend). FIXES: point lists pass the draw path only with ngpu_point_gs; in the front end the rect GS takes EVERY rect. PRE-REGISTERED FRONT7: menu background complete (picture); world census within the floor (5) vs the same-lake-state baseline.
- FRONT7 RESULT: PASS. Menu background complete + title card (shots/2026-09-25_FRONT7_strip.png). FRONT7 itself landed in
  the lake's THIRD state (1.361, no baseline) - so a BATCH was run and binned by state (claudecode-76's advice):
  FRONT7b / FRONT7c (rect GS) and BASEDEF1/2/3 (default). Floor BASEDEF1 vs BASEDEF2 (lake 0.37): 0/0. FRONT7b vs
  BASEDEF3 (0.99): 0 toward / 0 away. FRONT7c vs BASEDEF1 and vs BASEDEF2 (0.37): 0/0. The scoped rect GS leaves the
  world IDENTICAL. ngpu_rect_gs now ON by default (front end only; ngpu_rect_gs_world off). Points drawn only with
  ngpu_point_gs (off).
- USER REPORT (live, 2026-09-25): "textures on the other side of the lake looked perfect on the mountain, but after a
  few seconds some white textures replace them." Row (u3) restated: a TIME-DEPENDENT switch to white - the signature of
  a LOD/impostor swap (far trees/terrain switching to runtime-rendered impostor cards) whose impostor texture the
  native side renders wrong or never. Fits the white flakes seen when never-drawn passes were enabled.

## GLOBAL MIGRATION TO THE SDK DXBC TRANSLATOR - USER DECISION 2026-09-25
- DECISION (user, via claudecode-76, CONFIRMED in this window): "do the global migration - days, but we wont spent
  weeks finding bugs with shaders that are not working down the line." Constraint (user): "make sure the shaders we
  have get ported over to the full translation, if it works right now lets use all we have so we can just work on
  what is missing." STAGING (user, this window): TWO PHASES -
    PHASE A: the native draw path consumes the in-app SDK translator's DXBC, pair by pair; every pair is judged
      against TODAY'S WORKING RESULT (the snapshot below = the oracle), all-SDK or all-old per draw (no
      half-migrated binding), and which one is logged. Progress = pairs migrated + verified. The plugin keeps
      rendering (live reference) - so the census stays DUPLICATED through phase A by definition.
    PHASE B: the plugin stops rendering; subjects flip to emulated_dead (the user's metric) with RECORDED reference
      frames as the oracle.
- WHY THE METRIC CANNOT MOVE IN PHASE A (read from native_gpu_census.h, not assumed): emulated_dead is true only
  when the rexglue code a handler replaces no longer executes; "on the hybrid build, where the plugin still renders
  the main window, this is false for EVERY subject". Put to the user before any code; the two-phase staging is theirs.
- SNAPSHOT (oracle + rollback): D:\fable2_flash\migration_snapshot_20260925 - ngpu_cache (384 files), XenosRecomp.exe
  (19b88b1d, bool-only), fable2.exe, MANIFEST.sha256, GIT_HEAD 5b5f2aa, pictures (FRONT7 strip, CONDEXEC5 hero,
  FRONT2 menu) and two default-build f1500 frames, one per lake state (BASEDEF3 lake 0.99, BASEDEF1 lake 0.37).
- NON-REGRESSION LIST, EXTENDED (a-e from 09-24 stand):
    f. the HERO drawn whole (cexec b0 + BLENDINDICES typing)           - user has seen it
    g. the MENU TEXT gold and shaped (host swizzle)                    - user confirmed live
    h. the GROUND gamma (0.976 vs the unenhanced reference)
    i. the FERNS at parity (1.04 vs the unenhanced reference)
    j. the MENU BACKGROUND complete + title card (scoped rect GS)
- CONTRACT RE-SCORE (vs the 09-24 map, 16 elements): still 15 need work; partial native equivalents exist now for
  two of the ABSENT ones - built-in GSs (ngpu_rect_gs / ngpu_point_gs) and the texture model's per-format swizzle
  (host swizzle) - both would be REPLACED by the SDK's own model, not carried.
- PARKED AS MOOT-PENDING-MIGRATION (XenosRecomp-only; do not pick up by reflex): the PASTEL class (CONDEXEC3 /
  PCOORD1 - param gen / interpolant assignment; the SDK has PsParamGen), the push-SETP scalarisation fix, the pred-only
  cexec form, PCOORD. Kept alive only as needed to hold f-j during phase A.
- PHASE A FIRST STEPS: (1) the SDK shader binding contract, read from rexglue d3d12/command_processor.cpp +
  pipeline_cache.cpp (root signature, system constants, descriptor model, shared memory); (2) one PAIR end to end
  (PS 9A193FEB + VS BCC6E2DE, the healthy control) drawn from SDK DXBC and compared against the oracle.
- PHASE A STEP 1 DONE: the SDK binding contract is in docs/native_gpu/MIGRATION_SDK_CONTRACT_2026-09-25.md (root signatures bindless/bindful, the 464-byte system constants and their register sources, packed float constants, raw b2/b3, SV_VertexID + a 512 MB shared-memory mirror for vertex fetch, Texture2DArray + signed/unsigned SRV pairs, per-PAIR VS translation via the 64-bit Modification, generated GSs). Target for the first pair: BINDFUL + RTV. Largest single piece: the shared-memory mirror (vertex fetch without an input layout).
- PHASE A BUILD PLAN (feasibility checked: plume exposes ID3D12Device8* (D3D12Device::d3d) and the raw command list
  (D3D12CommandList::d3d), so the SDK path builds its OWN root signature + PSO in raw D3D12 on the same device and
  list; descriptors come from plume's heaps so the bound heaps stay valid). New module src/native_gpu_sdk_path.cpp,
  behind ngpu_sdk_path (default off) + a per-pair allow list, so the path is per draw ALL-SDK or ALL-OLD, logged:
    A1 shared-memory mirror: one 512 MB default-heap buffer = guest physical memory; per draw, upload the ranges the
       pair's vertex_fetch_bitmap names (+ the index buffer), reusing the page write-watch ticks for dirtiness.
    A2 pair translation: VS+PS through the in-app DxbcShaderTranslator (bindful, RTV) with the modifications the
       plugin computes (interpolator mask from the PS, param gen, dynamic register count); cache per pair+mod.
    A3 root signature (bindful layout, CP:483-696) + PSO from the draw's state (blend/depth/cull/topology/RT formats).
    A4 per-draw constants: b0 system (RTV subset of UpdateSystemConstantValues), b1 packed floats VS/PS, b2 raw 40
       dwords, b3 raw 192 dwords.
    A5 textures: the native texture cache's resources viewed as Texture2DArray, unsigned + signed SRVs, t1.. in the
       translation's binding order; samplers from the fetch constants.
    A6 the draw: IBV into the mirror (guest index buffer) or auto-index; SV_VertexID path.
  FIRST SUBJECT: the healthy control pair VS BCC6E2DE + PS 9A193FEB; judged against the snapshot frames (pixels it
  writes vs the old path's) before any second pair. Then widen by the differential: every pair whose SDK output
  matches the snapshot is DONE; the rest is the work list (the user's "use all we have").
- PHASE A2 BUILT: src/native_gpu_sdk_xlat.{h,cpp} - TranslatePair(vs, ps, regs): SDK DxbcShaderTranslator (bindful, RTV), VS/PS modifications computed exactly as PipelineCache::GetCurrent{Vertex,Pixel}ShaderModification (interpolator mask from the PS input mask, clip planes, vertex kill, point size, centroid pattern, param gen, early-Z hint; float24 modes n/a on D32 depth); keeps DXBC + texture/sampler bindings (bindful order) + packed float bitmap/count/dynamic flag + vertex fetch bindings + memexport; cached per (vs, ps, vmod, pmod). Compiles and links; not yet called. A1 (mirror) also inert.
- PHASE A3/A4 BUILT (compile + link): A3 = the plugin's bindful root signature (raw D3D12) + PSOs with the old path's state decode; A4 = src/native_gpu_sdk_consts.{h,cpp}: b0 SystemConstants filled in the translator's own struct from a RegisterFile copy of the draw's registers (UpdateSystemConstantValues, RTV path, ported line for line), viewport via GetHostViewportInfo, b1 packed floats per stage, b2 raw 40 dwords, b3 raw 192 dwords. Vendored from 0cb9040 for it (ORIGIN.txt): register_file.cpp, registers.cpp, and draw.cpp's GetNormalizedDepthControl + GetHostViewportInfo (half_pixel_offset read from the plugin's registered value). Nothing is called yet; the default build is unchanged. NEXT: A5 (texture SRVs as Texture2DArray, signed/unsigned, samplers) and A6 (the draw call), then the first pair.
- PHASE A5/A6 BUILT (f41a083): SdkDraw in the replay's issue_draw, behind ngpu_sdk_path (default off) + ngpu_sdk_pairs
  ('VVVVVVVV:PPPPPPPP,...', '*' = any). All-SDK or all-old per draw: every refusal happens BEFORE a command is recorded
  and is counted by reason in the "SDK PATH" line (census cadence). Textures: the native cache's RAW uploads
  (g_tex_raw: no sRGB twin, no SNORM, no exponent bake - the DXBC applies signs/gamma/exp itself), SRVs as
  Texture2DArray / TextureCube / Texture3D per binding dimension, component mapping = guest swizzle o host swizzle;
  samplers = GetSamplerParameters + WriteSampler ported (anisotropic_override read from the plugin's registered value).
  Descriptors in plume's shader-visible heaps (8192-view per-frame ring + 256 sampler-table cache). Vertices and indices
  from the mirror (IBV = mirror VA + guest index address; the shader swaps index endian). Viewport/scissor =
  GetHostViewportInfo / GetScissor (vendored verbatim). First-cut refusals: prims other than 4/6, strip reset,
  MSAA != 1x, memexport, a signed view that would be sampled (the native resources are typed UNORM).
  KNOWN LIMIT: the mirror is an upload heap written during recording, so every draw of a frame reads the frame's
  FINAL guest bytes (same as the old path's caches); a range rewritten mid-frame would differ from the plugin.
- SDK1 / SDK0 PRE-REGISTERED (first pair, VS BCC6E2DE + PS 9A193FEB, the healthy control: arches/rocks). Same binary
  f41a083; SDK1 = ngpu_sdk_path=true, ngpu_sdk_pairs=BCC6E2DE:9A193FEB; SDK0 = identical with ngpu_sdk_path=false
  (the control differs by the one switch). BASEDEF tune (bridge, hooked off, f1500 scene + plugin copy).
  P0 MECHANICS: SDK1's SDK PATH line shows drawn > 0 for BCC6E2DE:9A193FEB, translation failed 0, PSO failed 0, no
     device removal; SDK0's shows nothing drawn (path off).
  P1 PICTURE (tools/native_gpu/sdk_pair_diff.py SDK1 vs SDK0, f1500, 32x32 blocks, >10% luminance change): only
     judged if both legs land in ONE lake state (else re-run SDK0 until they do, n<=3). PASS = CHANGED <= the state's
     identical-leg floor (0.99: 5; 0.37: 0 -> allow 1). A FAIL lists the blocks and whether they move TOWARD the plugin.
  Unmasked a second defect? - to be read off the picture (anything the SDK draws that the old path did not).
- RESULT SDK1: P0 FAIL AS WRITTEN - 0 draws on the SDK path. The listed pair's draws were ALL refused as "reset":
  indexed triangle strips with PA_SU_SC_MODE_CNTL.multi_prim_ib_ena (491,003 refusals at the last report; also 4,326
  "primitive", 12,978 "msaa" - other draws of the pair). No crash, world reached. P1 not judged (nothing drawn).
  FIX: the plugin's kGuestDMA reset case ported (primitive_processor.cpp:606-660/:814): 16-bit indices, reset index
  in guest endian == 0xFFFF -> the guest buffer direct with a 0xFFFF strip cut in the PSO; other reset values (the
  plugin rewrites the buffer) and 32-bit reset (24-bit masking) stay refused, counted. 16-bit index endian now
  normalised as the plugin does (8-in-32 -> 8-in-16, 16-in-32 -> none). SDK2 = SDK1 re-run on that build, same
  pre-registration (P0/P1 unchanged).
- RESULT SDK2: P0 FAIL AS WRITTEN - 0 SDK draws again; the reset refusals went to 0 and the same draws are now refused
  as "msaa" (908,093): the scene surface is 2x MSAA (RB_SURFACE_INFO 0x14010500 -> msaa_samples 1), drawn by the native
  path on a 1x 1280x720 target. FIX: the SDK path draws guest-MSAA surfaces on the 1x native target with the system
  constants' sample_count_log2 = the HOST target's (0,0), which is what the plugin passes for its own (multisampled)
  host targets; in the RTV path the value only shapes alpha-to-mask coverage (dxbc_translator_om.cpp:1830); the
  viewport does not depend on it (GetHostViewportInfo). Counted, no longer refused. SDK3 = same pre-registration.
- RESULT SDK3: P0 FAIL AS WRITTEN - 0 SDK draws; the pair TRANSLATES (2 pair/modification entries, 0 failed) and
  every draw is refused as "signed view needed" (906,233): a texture of the pair carries two's-complement lanes.
  FIX: the signed binding gets a RAW SNORM TWIN (g_tex_raw_signed: the same untransformed texels uploaded in the
  format's SNORM type; floats serve as they are); a format with no SNORM twin refuses the draw (counted, "no SNORM
  twin"). A signed binding whose lanes are not sign 1 gets the unsigned view (never read signed). SDK4 = same
  pre-registration.
- USER ROW u5 (2026-09-25 ~10:28, user's words): "Loading bower lake screen has no background just title on top on a
  black screen". The Bower Lake LOAD screen: the title draws, the background image does not. Relation to u4 (the
  animated load screen not showing) not yet established - same screen, possibly the same missing pass. OPEN.
- RESULT SDK4: P0 FAIL AS WRITTEN - 0 SDK draws; the signed refusals went to 0, every draw now refused as "descriptors"
  (907,169). CAUSE (mine, ordering): HeapsReady() required the mirror buffer, which is created lazily by the first
  SdkMirrorRequest - called AFTER HeapsReady - so the descriptor blocks were never reserved and the one-shot 'tried'
  flag made that permanent. FIX: HeapsReady creates the mirror first and does not latch a failure. SDK5 = same
  pre-registration.
- RESULT SDK5 (control SDK0, same binary, path off): P0 PASS - 900,637 draws of BCC6E2DE:9A193FEB from SDK DXBC,
  translation/PSO/descriptor failures 0, textures 12.7M cache + 1.67M resolve binds, 0 placeholders, mirror 377 pages,
  no crash. P1 FAIL - lake states NOT matched (SDK5 lake 10.84, SDK0 0.993) and the picture shows why the number is
  meaningless: the ARCHES/ROCKS ARE GONE on the SDK path and the lake is white (shots in D:\fable2_flash\sdk_legs\
  sdk5_vs_sdk0.png). 302/880 blocks changed, 288 away. Unmasked a second defect? - the white lake follows the missing
  arches (the lake's passes read the scene); not separately assessed.
  CAUSE CANDIDATE (arrived on cue - to be CONFIRMED by SDK6, not assumed): every SDK draw logged viewport z 0.000..0.500
  - GetHostViewportInfo(full_float24_in_0_to_1 = true) squeezes D24FS8 depth into [0, 0.5) for the plugin's EDRAM
  transfers, while the old path's draws in the SAME native depth buffer write [0, 1]. FIX: false. SDK6 PREDICTION:
  arches drawn; if SDK6 lands in lake state 0.99, CHANGED vs SDK0 <= 5 (else re-run, n<=3).
- RESULT SDK6: the depth-squeeze candidate is REFUTED as the cause - arches still absent, lake white, 305/880 changed
  (292 away), lake 9.95. The fix stays (it is correct for a shared depth buffer) but explains nothing measured.
  NEXT, value-independent: SDKDBG7 = SDK6 + ngpu_sdk_debug=7 (SDK PSOs with no depth, no cull, no blend). Reading:
  arches appear ANYWHERE -> VS and fetch work, the state is wrong; nothing appears -> vertices/indices broken. The
  SDK DRAW lines now also print VGT_MIN/MAX_VTX_INDX, the offset and whether the replay's shadow ever CARRIED them
  (a register never carried reads 0 and the SDK VS clamps every index into [min, max]).
- RESULT SDKDBG7: NOTHING of the pair rasterises even with depth, cull and blend removed - the arches are absent
  everywhere. Index window read and carried (min 0, max FFFFFF, offset 0, endian 8-in-16, strip cut 0xFFFF) - that
  suspect is cleared. So the SDK draws produce no pixels independent of state: vertices off-screen / clipped, or
  the PS never runs. NEXT: SDKQ8 = SDKDBG7 + ngpu_sdk_query_frame=1500: D3D12 pipeline statistics per SDK draw
  (IA / VS / clipper in-out / PS) name the stage where the geometry dies.
- USER (watching the leg windows, ~10:50): "Seems we lost the structures like the bridge and house and the tree textures
  are on the lake" - the SDK legs' picture (the pair's structures absent, the lake white/tinted); NOT the default
  build (settings file carries no ngpu_ switch; BASEDEF3 control has them).
- RESULT SDKQ8 (pipeline statistics, frame 1500, first 64 SDK draws): the SDK draws RASTERISE - e.g. seq 682 on the
  scene target 14010500: IA 2951 verts / 2217 prims, VS 2470, clipper 2217 in / 2217 out, PS 64,521. So the vertices,
  indices and viewport put the arches on screen and the PS runs; what it WRITES is wrong (or is overwritten).
  SDKDBG7 was therefore NOT decisive (its depth-off draws are overdrawn by later old-path draws) - withdrawn as a reading.
  NEXT: SDKDUMP9 = normal state + per-draw scene dumps over seq 676-700 at frame 1500 (what the SDK draws put in the
  target, draw by draw).
- CHAIN SDKDUMP9 / DEPTHSDK10 / DEPTHOLD11 / SDKWIN12-13 / SDKZ14 (frame 1500, per-draw dumps):
  * the SDK draws paint the arches correctly at seq 682-684/694: same pixels, colour 0.136 and depth within 0.1% of
    the old path's (per-draw depth dumps, both legs). NOTE the dumper labels SDK draws with the PREVIOUS old draw's PS
    (g_last_ps_hash is set only by DrawTranslated) - a stale label, not a wrong pair.
  * they are erased LATE: the water (F22BC502 at seq 1534) and far tree cards (E6415E83 at 1426) paint over the spot.
  * because the arch DEPTH is gone before that: intact (median 0.00429) to seq ~1179, then 0.00124 from ~1259 while a
    SECOND PASS redraws the scene (BCC6E2DE, the hero 36F4DD57...) - Fable's predicated second TILE, whose draws carry
    PA_SC_WINDOW_OFFSET Y -512. The SDK path applied the plugin's EDRAM-window viewport/scissor, so on the full-screen
    native target the second tile's arches landed 512 rows up after that pass's depth clear. (The old path draws every
    tile pass unshifted: ngpu_replay_tile_band / _scissor are off by default.)
  FIX: PA_SC_WINDOW_OFFSET zeroed in the SDK draw's register copy (screen-space viewport + scissor; the scissor
  registers are screen coordinates, so each tile keeps its own band). ALSO REFUTED on the way: the "served white"
  depth-resolve bookkeeping (the replay never counts greater/less on either path).
  SDK7 PREDICTION (pre-registered): arches present; vs SDK0 in the same lake state, CHANGED <= floor (0.99: 5).
- RESULT SDK7: PREDICTION FAILED AS WRITTEN - arches still absent (313/880 changed, lake 16.4). OLDZ15 (the control I
  should have run with SDKZ14: same depth sampling, SDK off) shows the old path ALSO loses the arch depth at ~seq 1259
  (the second tile's depth clear, drawn unbanded) and RESTORES it by 1319 with that tile's full-screen arch redraw.
  With the offset zeroed, the SDK draws honoured the guest scissor (tile 2 = rows 512+) and so did not restore rows
  150-280. FIX: the SDK draw leaves the scissor as the replay set it (the old path's target model: unbanded unless
  ngpu_replay_tile_scissor). SDK8 = same prediction (arches present; CHANGED <= floor in a matched lake state).
- RESULT SDK8 / SDK8b (the first pair drawn from SDK DXBC, scissor + offset fixed): the ARCHES, HOUSE AND WATER ARE
  BACK (picture D:\fable2_flash\sdk_legs\sdk8_vs_sdk0.png). P0 PASS. P1 AS WRITTEN NOT JUDGEABLE: both SDK legs land in
  a lake state 0.845 that the control never showed (SDK8 vs SDK8b: CHANGED 0 - identical, floor 0 for this
  configuration, n=2); the pair draws into the lake's 280x280 reflection target too, so the SDK path plausibly CAUSES
  the state - a matched-state comparison may be impossible by construction. Recorded readings, not re-scored:
  vs SDK0 (0.993) CHANGED 100-103 (toward 29-32, away 71); vs BASEDEF1 (0.371) 200 (toward 141, away 59).
  REGION READINGS (native/plugin luminance, SDK8 vs SDK0): big arch 0.884 vs 1.060; arch shadow side 0.806 vs 0.969;
  house 0.938 vs 1.035 - the SDK-drawn surfaces are 10-20% DARKER than the plugin, most in shadow. OPEN: that deficit
  (candidates to test, not causes: the shadow-map / depth-resolve texture as the SDK DXBC samples it, the raw SNORM
  normal map, the 2x->1x sample count). Unmasked a second defect? - the lake state change (above).
- CAUSE CANDIDATE for the SDK8 dark deficit: the old binding loop's SLOT SUBSTITUTIONS were not carried - option A
  (ngpu_edram_depth: an unwritten depth resolve, here 12B1D000 in 9A193FEB's slot 8, is served WHITE), ngpu_white_base,
  and decline-4 black (ngpu_gpu_written_black). The SDK path sampled the real unwritten depth resolve. FIX: WriteSrv
  applies the same three tests in the same order.
- SDK9 / 9b / 9c PRE-REGISTERED (n=3, identical, SDK on, the fix above). Readings per SURFACE (native/plugin
  luminance), not a range: big arch (rows 100-280 cols 50-270), arch shadow side (180-280, 60-200), house (120-180,
  640-720). PASS = each within +-0.05 of SDK0's value (1.060 / 0.969 / 1.035) - SDK0 is one leg, so the tolerance is a
  stated choice, not a measured floor. Also reported: whether the deficit was multiplicative (per-surface SDK8/SDK0
  ratios 0.834 / 0.832 / 0.906 - NOT one factor), the SDK lake state per leg with its spread (n=3), and OWNED PIXELS:
  a region counts only if the pair's draws are the last writer there (per-draw dumps SDKWIN13: the water and far tree
  cards overdraw parts of the arch region late in the frame; the three rects above stay the arch's in SDK0's frame).
  Verdicts available: PASS / FAIL / UNJUDGEABLE (owned set empty).
- ROW (lake state under the SDK path): SDK8 and SDK8b both 0.845 where the old path is tri-state (0.37/0.99/1.31) -
  a difference in KIND (random -> repeatable over n=2, not yet called deterministic). Either the SDK path removes
  the lake's run-to-run source or does one wrong thing consistently; OPEN.
- COST PREDICTION (recorded BEFORE pair 2, per the peer's point): pair 1 surfaced five contract-level fixes (reset,
  MSAA 1x, SNORM twin, mirror/descriptor order, screen-space tile model) plus the slot substitutions - all shared. The
  prediction is that pair 2 needs FEW (0-1) new contract fixes. Reported either way.
- RESULT SDK9 / 9b / 9c: PASS as pre-registered. native/plugin per surface (SDK0 in brackets): big arch 1.042 x3
  (1.060), arch shadow side 0.960 x3 (0.969), house 1.030 / 1.030 / 1.023 (1.035) - all within +-0.05. The deficit is
  gone: it was the missing option-A substitution. Lake 0.981 / 0.982 / 0.981 (SDK0 0.993): the "new lake state" of
  SDK8 was a SYMPTOM of the same defect (the dark 12B1D000 term), not a separate row - that row is CLOSED.
  PAIR 1 (VS BCC6E2DE + PS 9A193FEB): DRAWN AND VERIFIED on the three owned surfaces. Count: 1 pair verified of ~77.
- USER REQUEST (2026-09-25 ~11:40): "remove the texture upscaling checkmark as it could potentially cause issues with
  the compare" - done: texture_ai=1 -> 0 in out/build/win-amd64-Release/fable2_settings.cfg (backup
  D:\fable2_flash\fable2_settings_backup_20260925_texture_ai1.cfg; new hash 9121e3bd3e04f31e). texture_pack=1 and
  texture_scale=2 left as they were (not asked). bridge_leg_ref1x.sh backs up whatever is current, so it persists.
- VERDICT RESTATED (peer, correctly): SDK9's "+-0.05" was the tolerance WITHDRAWN this morning as unsourced - it came
  back without re-derivation. Replaced by the measured spread: SDK9/9b/9c legs 0.000 / 0.000 / 0.007 (arch, shadow,
  house). Residuals vs SDK0: arch -0.018 (2.6x that floor), shadow -0.009 (1.3x), house -0.005 (inside) - three of
  three BELOW the old path. SDK0 is n=1, so the residual's significance waits on SDK0b/SDK0c (running). Reading:
  "VERIFIED on owned surfaces, with a reproducible ~-1.7% residual on the arch pending the control's own spread".
- SUBSTITUTION AUDIT (the old path's deliberate approximations / skips vs the SDK path), default-on switches read
  from the cvar table 2026-09-25:
    PORTED: option A unwritten-depth white (ngpu_edram_depth); ngpu_white_base; decline-4 black (ngpu_gpu_written_black);
      placeholder white/black for failed textures; host swizzle + DXT3A-as-R8 (via the XTexture view mapping);
      depth/blend/cull/mask decode (ngpu_state, _mask, depth_test, reverse_z, front_face_legacy); blend constant.
      ALPHA BLEND MAP (ngpu_blend_alpha_map): found MISSING by this audit, ported now (alpha factors through the
      reference's alpha table).
    NOT NEEDED (the DXBC does the real thing): tex_gamma / tex_signed / tex_exp_adjust bakes (raw uploads);
      param_gen; viewport_depth (GetHostViewportInfo, measured z within 0.1%); allow_fetchless; vs_textures.
    NOT YET PORTED -> REFUSED (counted): ngpu_skip_impostor (skinned VS stay on the old path); rect_gs / rect_list /
      quad_list / draw_points / tess (prims other than 4/6 refused).
    KNOWN DIVERGENCE, NOT A SUBSTITUTION: the old path DROPS a draw it cannot feed (stream pair / range / unreadable /
      upload-heap drop sites in DrawTranslated); the SDK path draws those from the mirror - more faithful, and a
      pair whose old twin was dropping draws will differ from SDK0 for that reason alone. Not yet counted per pair.
    BOOKKEEPING ONLY: ClaimDepthTiles (feeds a log line) - not ported, no effect on pixels.
- PAIR 2 PRE-REGISTERED: VS BCC6E2DE + PS 0B24BD42 (the ground: on the old path its seq-693 draw writes depth over
  642k px, z ~0.013). Legs P2a/P2b/P2c = ngpu_sdk_pairs "BCC6E2DE:9A193FEB,BCC6E2DE:0B24BD42" (pair 1 stays on:
  migration is cumulative), control = SDK9/9b/9c (pair 1 only). Surfaces (native/plugin luminance): ground (rows
  362-662, cols 0-500), near ground (562-720, 1-1280) - owned-pixel check by per-draw dump before scoring; if the
  pair is not the last writer there the verdict is UNJUDGEABLE for that surface.
  TOLERANCE (measured, not borrowed): PASS = |mean(P2) - mean(control)| <= 3 x max(leg spread of P2, leg spread of
  control), n=3 each; the residual and its sign are reported whatever the verdict.
  P0: drawn > 0 for the new pair, 0 translation/PSO failures, the new pair's refusals named and counted.
  COST: count the NEW contract-level fixes pair 2 needs (prediction 0-1).
- PAIR 1 RESIDUAL, on MEASURED spreads (SDK0/0b/0c n=3: arch 1.060/1.059/1.059, shadow 0.969/0.969/0.970, house
  1.035/1.032/1.033; SDK9 n=3 above): arch -0.018 (~17x the 0.001 control spread), shadow -0.009 (~9x), house -0.004
  (inside the 0.007 SDK spread). REAL and reproducible - and in the plugin's DIRECTION: native/plugin arch 1.060 old
  vs 1.042 SDK, shadow 0.969 vs 0.960 - the SDK draw is closer to the reference on the arch, marginally further on the
  shadow side. Verdict: pair 1 VERIFIED WITH A NAMED RESIDUAL - every owned surface within 4% of the plugin; the
  arch moved TOWARD the plugin (0.060 -> 0.042 over), the shadow side moved AWAY by 0.009 (0.031 -> 0.040 under,
  ~9x the control spread). The shadow-side residual is OPEN, small, reproducible; not re-scored.
- PAIR 2 AS FIRST REGISTERED DOES NOT EXIST: P2a/b/c drew nothing new, and P2DIAG's per-pair census shows
  BCC6E2DE:0B24BD42 with 0 ARRIVALS at SdkDraw. The name came from the per-draw dump label, which carries the LAST PS the
  old path bound (g_last_ps_hash): seq 693 is a depth-only draw that inherited the previous draw's PS. Pair chosen
  from a stale label - declared names are not identity, again. P2a/b/c are therefore three MORE pair-1-only legs
  (surfaces equal to C10 within spread). The census (SDK PATH unlisted arrivals) is now the ONLY source for pair names.
- PAIR 2 RE-REGISTERED from the census: DBF9A58A:0B24BD42 (163,216 arrivals over the P2DIAG run; same PS family).
  Legs P2d/e/f = pairs "BCC6E2DE:9A193FEB,DBF9A58A:0B24BD42"; control C10a/b/c (pair 1 only, same binary modulo the
  census-only logging). Floor MEASURED: C10 pairwise CHANGED 0/0/0 (n=3). PASS = in all three legs every CHANGED
  block moves TOWARD the plugin and none AWAY; FAIL = any reproducible AWAY block (listed); plus the six surfaces
  within 3x the larger leg spread. P0 = drawn > 0 for DBF9A58A:0B24BD42 with its refusals named by the per-pair census.
- RESULT PAIR 2 (DBF9A58A:0B24BD42), P2d/e/f vs C10a: P0 PASS - 162,586 drawn of 162,586 arrived, 0 refusals.
  P1 PASS as pre-registered - CHANGED 0 in all three legs (floor 0), surfaces within spread (arch 1.042 x3 vs 1.043,
  house 1.026/1.026/1.030 vs 1.023, ground/near ground identical).
  OWNED PIXELS (the peer's point - a zero is only a pass where the pair is the last writer): intervention DROP2b = old
  path with ngpu_drop_ps=0B24BD42 (4,998 draws removed in the window) vs SDK0b -> 15 blocks change (rows 288-352, the
  shoreline band; 10 toward / 5 away from the plugin when the pair is REMOVED). So the pair owns those 15 blocks, and
  the SDK legs reproduce the old path there exactly. (DROP2, the first attempt, was VOID: I converted the hash to
  decimal wrongly - 186956098 for 186957122 - and the instrument's own line "removed 0 draws ... NOT a control" said so.)
  COST PREDICTION: HELD - pair 2 needed 0 new contract-level fixes. What it did need was an identity fix to the
  PROCESS: pair names come from the SDK path's arrival census only.
  COUNT: 2 pairs verified of 86 distinct pairs arriving at SdkDraw (P2DIAG census, the denominator from here on).
- USER (12:40): "Let continue untill 100% parity with xenos, you are autonomous, make decision based on recommendations,
  will be back tonight". DECISION (recommended, recorded): widen by the DIFFERENTIAL instead of 3 legs x 84 pairs -
  ALL1 = every pair on the SDK path ("*:*") vs P2d/e/f (floor 0, n=3). Blocks that move AWAY from the plugin are
  failures and are BISECTED over the pair list (one leg per step, the final culprit confirmed n=3); blocks that move
  TOWARD are recorded as the SDK being more faithful. Then the refused categories (prims 13/8/1/18, skinned impostor
  skip) are implemented so the denominator becomes the whole frame.
- ALL2 (every pair, 44 drawn, 2.16M SDK draws, 6 blobs refused by the structural guard now shared with the census):
  arches/ground/ferns/hero/house right; the LAKE WHITE (1.512), sky and distance brighter. ALL3 (= ALL2 minus the
  water VS F22BC502): lake 0.986, house 0.987 - the water pairs carry most of ALL2's damage.
- THE ALL-PAIRS FLOOR IS NOT 0 (the peer's point, measured): ALL3 / 3b / 3c identical legs differ by 214 / 51 / 233
  blocks. ALL3's "11 changed" is VOID as a reading. One source identified: the per-frame view-descriptor ring (8192)
  OVERFLOWED at a different draw each run (refusals 1792 / 1800 / 3480 in identical legs), so a different set of
  draws fell back to the old path. Ring raised to 40,000 with its per-frame peak logged; floor to be re-measured (n=3)
  before any verdict. Bisection outcomes declared: SINGLE CULPRIT / MULTIPLE CONTRIBUTORS / NOT REPRODUCED ON SPLIT.
- USER (13:0x, watching the ALL legs): "2d assets such as the UI not showing as well", "2d menus also not showing" -
  on the SDK path with every pair on, the 2D / screen-space pairs (HUD, menus) draw nothing. Default build unaffected.
  OPEN row for the SDK path (candidates DFB44E4B:*, A165FECD:*, 8A0AE541:*, 037F92DA:*).
- ALL5/ALL6 x3: the "non-determinism" between identical all-pairs legs is (a) sampler-table cache exhaustion (the
  permanent 256-entry cache filled in first-use order: 8,298 refusals in ALL6b, 0 in its twins) - FIXED, per-frame
  sampler ring (512) with dedupe; view ring dedupe cut the peak from ~32k to 1,240-1,520 views/frame - and (b) the
  known TRI-STATE LAKE: ALL5/ALL6 lake 0.977/0.980 vs 5b/5c/6b/6c 1.38-1.41; within a state the legs agree (6b vs 6c
  CHANGED 0). Rule re-applied: pair by lake state.
- ALL6 (every pair except the water VS, lake 0.980) vs P2d (pairs 1+2, lake 0.981): CHANGED 9 - toward 7, away 2
  (row 32 col 800; row 224 col 0). n=1 in this state; confirmation pending. Denominator note: 44 pairs DRAWN on the
  SDK path in this scene (of 86 arriving; the rest refused by primitive type / skinned); VERIFIED count stays 2 until
  the all-pairs reading is confirmed n=3 per state.
- USER (13:1x-13:2x, their own gameplay test of the default build): "textures are correct, they load once you get
  close to them, but appear incorrect at a distance" and "even the beach appears once we get closer to it". ROW u6
  = u1: distance-dependent. Working hypothesis (from the Sep-24/25 beach work, NOT yet tested): distant terrain is
  drawn from textures the game COMPOSES at LOAD through resolve chains, and the bridge drops unconsumed frames at
  load (18), so those one-shot passes resolve empty targets.
- ACCUM3 PRE-REGISTERED (rows u1/u6): current default build + ngpu_bridge_accumulate=true (the rect GS now takes every
  rect before the world camera exists, i.e. at load - ACCUM1's flat TRIANGLE mask matches the half-rect symptom that
  FRONT6/7 later fixed). Control = SDK0b (default, same binary family, lake-state matched if possible).
  PASS = (a) BRIDGE FRAMES shows merged > 0 and lost 0; (b) the shore mask resolve FBB49000 is non-zero and shaped
  like the plugin's (not a triangle); (c) census vs a same-lake-state default leg: no reproducible AWAY block.
  Unmasked a second defect? - read off the picture.
- ACCUM3 RESULT: (a) PASS - 38 frames merged, 0 lost; (b) FAIL - shore mask FBB49000 mean 0.001, 1.5% texels
  non-zero; (c) not judgeable (lake state 0.671, no control in that state). Accumulate alone cannot fix u1: what is
  drawn INTO the mask target at load is single-point POINT LISTS (SHORESKIP2), which the default old path does not draw.
- NEW (the refused-category work): the plugin's GENERATED GEOMETRY SHADERS vendored (native_gpu_xlat/
  pipeline_gs_vendored.cpp = pipeline_cache.cpp:1632-2677 verbatim + GetGeometryShaderKey). SdkDraw now takes point
  lists (GS kPointList, POINTLIST), rect lists (GS kRectangleList, TRIANGLELIST), non-tessellated quad lists (GS
  kQuadList, LINELIST_ADJ) and line lists/strips, from the guest's own data (the old path's CPU expansions unused) -
  the plugin's D3D12 choices (InitializeCommon(true,false,false,true,true,true)). Still refused: fans, line loops,
  tessellation / patches - named per pair ("prim N").
- SHORE1 PRE-REGISTERED (u1/u6): pairs "*:*,!F22BC502:*" + ngpu_bridge_accumulate=true. PASS = shore mask FBB49000
  non-empty and SHORELINE-shaped (not a triangle, not blank); the load-time point draws appear as drawn in the pair
  census; the beach visible at the shore in the f1500 picture.
- SHORE1/SHORE2 RESULT (u1/u6 mechanism): PASS on (b) - the shore mask FBB49000 is now a soft SHORELINE (mean 0.436,
  58.5% non-zero; picture sdk_legs/shore1_mask.png) where every earlier leg had it blank or a triangle: the load-time
  point sprites drawn through the plugin's generated point GS + ngpu_bridge_accumulate. Picture vs the PLUGIN
  (sdk_legs/shore2_native_vs_plugin.png): sand at the near shore now present; lake DARKER teal than the plugin's pale
  blue (lake 0.381); far trees now drawn (sprite cones, plugin soft); white squares in the far canopy; the right-hand
  hillside still the BLACK WEDGE (plugin: a terrain slope) - present in every native leg incl. the old path.
  (SHORE1 had a too-broad tess gate refusing ordinary triangles - fixed: explicit major mode AND path_select.)
- SKIN1 (every pair incl. water + skinned, accumulate): the WATER on the SDK path now reads IDENTICAL to the old
  water (lake 0.381 both) - ALL2's white SDK water was the blank shore mask (water slot 0 = 1BB4A000, the mask).
  The hero draws on the SDK path; ngpu_skip_impostor ported (1CF397A6:D6F02D0B 43,330 drawn / 17,873 skipped, as the
  old path). HUD gold counter drawn by the SDK path in the world.
  REMAINING REFUSED: prim 18 tessellated terrain (6A56B867:81B999B2 838k, FFDBABD9:52FE118F 671k, 0EF7C66A:5FEF6617
  638k, FFDBABD9:4DF4ADDB 110k, 8A0AE541:52FE118F); a handful of blobs refused by the structural guard.
  NEXT: tessellation on the SDK path (the plugin's domain-shader VS + its hull shaders) - the black wedge and the
  distance rows are terrain.
- CORRECTIONS (peer, accepted): the SHORE2/SKIN1 "darker teal lake" is the lake's KNOWN 0.37 native state (ratio
  0.381), not a new row - PICTURES get the same lake-state gate as the census from now on. The BLACK WEDGE is
  PRE-EXISTING (every native leg incl. the old path): an old row the migration made visible, not a Phase A defect.
  ngpu_skip_impostor: "REPRODUCED on the SDK path" != "RESOLVED against the plugin" - 17,873 impostor draws/frame
  stay deliberately skipped on BOTH paths (a standing divergence from the console; u3 may live in it).
- TESSELLATION ON THE SDK PATH BUILT: the plugin's fixed tessellation VS + hull shaders vendored (12 bytecode headers,
  native_gpu_sdk_tess.cpp, selection = pipeline_cache.cpp:2733-2805); the guest VS translated with the domain host
  type (primitive_processor.cpp:285-340: explicit major mode AND path_select); DOMAIN visibility for the VS root
  parameters; patch topologies per command_processor.cpp:3325-3345; the record-time PATCH SNAPSHOT (g_bridge_patch)
  as the index buffer; the SDK call moved AHEAD of the old path's tess_skip. Tessellated strips/fans still refused.
- TESS1 PRE-REGISTERED (all pairs + accumulate, n=1 first, then n=3 if the picture passes). PREDICTIONS, written
  before the run: WILL close - the prim-18 refusals (the 5 terrain pairs drawn, 0 refused) and the BLACK WEDGE (a
  terrain slope where the plugin has one). MAY close - u6/u1 distance terrain (only if the far terrain is prim 18).
  WILL NOT close - u3 if it lives in the impostor skip; the far-tree sprite shape (point sprite PS, not terrain);
  u2 halo; u4/u5 load screen. Picture judged only with the lake state read in the SAME run.
  Residue shape (for the user): 5 pairs, but the HEAVIEST 5 (838k / 671k / 638k / 110k draws + 14).
- TESS1/TESS2 RESULT: every pair of the scene now draws on the SDK path (TESS1: 4.34M SDK draws, 0 primitive / reset /
  mirror / PSO / descriptor refusals; the 5 tessellated terrain pairs drawn in full, ~99% with the record-time patch
  snapshot). Ground 1.008 / near ground 1.012 vs the plugin (old path 0.984) - the near terrain now matches the
  plugin's pattern. TESS2 also let the SDK path take the draws the OLD path refuses before issue ("not an indexed
  draw" / "no index buffer", ~965/frame): 1,983,477 drawn, 201,217 refused by the SDK path too (cumulative).
  PREDICTION CHECK: "WILL close the black wedge" - FAILED as written (recorded surprise); prim-18 refusals closed - HELD.
- WEDGE TRACE (WEDGE1/2, DEPTHOFF1, DOFF_* x5, HMAP2): the wedge pixels are black from the FIRST colour-pass dump
  (seq 776) to the water (1690); the end depth there is NEAR (median 0.00207, 95th 0.0035 - a pre-pass wrote a hill);
  depth-off on the whole scene surface is UNINFORMATIVE (late full-screen passes cover everything - withdrawn as a
  reading); depth-off per PS: 52FE118F and 4DF4ADDB (both VS FFDBABD9, tessellated terrain) clear the black (0.122
  -> 0.0) but paint only the FAR slope - the plugin's near-right HILL is still absent. Heightmaps read plausible (far
  769x769 k_16: 7.6% zero, 46,580 distinct values). OPEN: the colour draw of the hill is missing or drawn outside the
  colour-mode dumps (the dumper filters edram_mode 4) - next instrument: a per-pixel LAST-WRITER census, not dumps.
- SIGNED VIEWS: a signed binding whose format has no signed host type (BC1-3...) now gets a NULL SRV (reads 0), as
  the plugin's null-SRV heap does, instead of refusing the draw (HMAP2: 11,424 refusals on DFB44E4B:2AC2D68D).
- WEDGE SPLIT INTO TWO ROWS (peer): WEDGE-FAR = drawn and DEPTH-REJECTED (52FE118F / 4DF4ADDB paint it with depth off)
  - the depth question; WEDGE-NEAR = the plugin's near-right hill, NO colour draw observed (needs a last-writer census,
  the dumper filters edram_mode 4). Both PRE-EXISTING: to be traced on the OLD path (unconfounded by the migration),
  then the fix confirmed on the SDK path.
- REFUSAL BREAKDOWN (HMAP2 per-pair census, cumulative over the run): arrived 4,221,983; drawn 4,137,680; impostor
  skipped (by design, as the old path) 62,865; signed view 20,784 (FIXED since: null SRV); guard-refused shaders 654.
  Non-design refusals 0.5% -> ~0.015% after the null-SRV change. The "201,217 refused by the SDK path too" line was
  MISLABELLED: it counted SDK-only draws never offered to SdkDraw (overlay / resolve COPY packets / other bins) - the
  counter now counts only offered draws.
- NON-REGRESSION LIST EXTENDED (peer): k beach sand at the near shore + shore mask shaped; l water identical on both
  paths (0.381/0.381 in the 0.37 state); m hero drawn on the SDK path; n HUD gold counter drawn by the SDK path.
  Against TESS2's picture: k present, l present (0.390 in the 0.37 state), m present, n present. (a-j as before.)
- FRONT END ON THE SDK PATH (the user's "2d menus not showing"): FRONTSDK1 = all pairs on SDK -> title AND menu BLACK
  natively (plugin fine); "Loading Bower Lake" text drawn. FRONTSDK2/3 (frame 900 queries): DCF17BF6:62772FA9 point
  sprites (1 px constant diameter, registers carried) rasterise 0 pixels - genuinely 1-px points, not the cause.
  FRONTB1 (all but DCF17BF6): title turns solid GREEN, menu still black, the world lake loses its water -> MULTIPLE
  CONTRIBUTORS. Now: "only X" legs per VS family (DFB44E4B, DCF17BF6, A165FECD, 4B84C6B5).
- FRONT END ON THE SDK PATH - SOLVED AS A PRESENTATION DEFECT. Chain: FRONTEMPTY (SDK on, nothing listed) was black
  too -> the SDK-only continuation ran RT-bind side effects for draws it then refused -> gated on the list
  (SdkWouldTake). Remaining black then bisected (FRONTP_*, FRONTQ_B1..B4: 4B84C6B5:C1B813C9, A165FECD:A8EAA0D5/
  2D5EA8E8, A165FECD:7D59AFCA/2AC2D68D); single-class prim exclusions all still black (not one prim class); FRONTDUMP1
  (frame 540): the swap RESOLVE FECFE000 holds the title (mean 155) while the swap RT is black at end of frame - the
  presenter blits the RT. FRONTPOST1 (SDK all pairs + ngpu_present_post = present what the XE_SWAP packet names, as the
  console does): title / menu match the plugin (colour included), the world is complete WITH the D-pad HUD icon that
  no native frame had shown before. FRONTPOST0 (old path + present_post): front end right, world BLACK except the HUD
  - the old path's composite into the swap surface never worked, which is why its default presenter shows a
  tonemapped scene target. So the console-faithful presentation needs the SDK path's composite.
  NB instrument caveat: the pair names used by the "only X" bisection come from RingPixelShader (address-keyed, can
  be stale) - the SDK draws are translated from the actual microcode, but list SELECTION may have been by stale name.
- NEW LAUNCHER tools/run_native.cmd: FABLE2_TUNE with shadow + bridge replay + SDK path "*:*" + accumulate +
  present_post/post_raw (the FRONTPOST1 configuration). Build DEFAULTS UNCHANGED (native window off by default).
  ngpu_sdk_world_only (default on) keeps front-end SWAP-surface draws on the old path for the default presenter; the
  launcher turns it off.
- OPEN ROWS NOW: u5 loading-screen background (plugin: map + quote panel; native: text only, both paths); wedge-near
  (right-shore hill never drawn in colour) and wedge-far (depth-rejected far slope); far trees as sprite cards /
  impostor squares; lake state variance (known); u2 halo untested.
- GUARD NOTE: ngpu_sdk_world_only (default ON) is what keeps the default presenter's front end right; turning it OFF
  without ngpu_present_post regresses the menus to black (FRONTSDK1/4). run_native.cmd turns both on together.
- CANOPY (far trees): SDK path per-pixel |diff| vs the plugin 0.45 vs the old path's 0.30 (ratio 1.117 vs 1.015);
  excluding point lists (0.451) or quad lists (0.457) changes nothing - not those prims. OPEN, parked.
- IDENTITY CENSUS (FRONTID1, every SDK draw of a front end + world run): pair names confirmed by the draw's own
  microcode 5,302,340; "stale" 6,251 - but EVERY flagged draw has the SAME name as its microcode's owner (e.g.
  3101DD0C:D6F02D0B both sides): two XShader objects registered for one microcode (the known "claimed by two
  containers" case), not a wrong NAME; unregistered microcode 654 (the guard-refused blobs). So no pair selection
  today (the only-X / FRONTP / FRONTQ families) was by a wrong name - those results stand.
- CANOPY ROW TRACED (world, f1500, far-canopy band rows 0-130 cols 300-1280, |per-pixel diff| vs the plugin):
  old path 0.267 (0.37 state) / 0.298 (0.99 state); SDK path P2d..ALL6 0.28-0.30, then SHORE1 0.335 -> SHORE2..TESS2
  0.45. Not points (0.451), not quads (0.457), not accumulate (0.499 without); RECT LISTS excluded -> 0.395 (and the
  lake moves to 0.695). Rect lists in the world = the full-screen POST passes (A165FECD...), which the old path mostly
  never drew (FRONT5's "never-drawn post passes"); the SDK path now draws them all through the plugin's rect GS.
  CANDIDATE (not yet tested): those passes sample DEPTH RESOLVES, and option A serves a depth resolve WHITE when its
  source had no native writer this frame - decided from greater/less writer counters that the REPLAY NEVER
  INCREMENTS (only the hooked path does), so in the replay EVERY depth resolve is served white. Right for the slot-8
  term (measured against the plugin), plausibly wrong for fog/DOF (white depth = maximum distance -> fogged canopy).
  The faithful fix is the plugin's own depth resolve (guest depth format bits), not a substitution - next.
- REFUTED (CANOPY_NOOPTA): option A off leaves the canopy unchanged (0.454; arch darkens to 0.806 as known) - the post passes' effect on the canopy is not the depth substitution.
- REFUTED (CANOPY_NULL): invalid texture fetch constants (type != texture, 579,096 bindings) now read ZERO on the SDK
  path (null SRV, as the plugin's null binding) - the switch stays ON for parity, but the canopy is unchanged (0.455):
  not the white boxes' cause. Also checked: native RT clears use alpha 0 (not an opaque-atlas source).
  CANOPY STATE: the far-tree cards sample the RESOLVED impostor atlas 12704000 (1024x512, k_1_5_5_5, 3 mips); the native
  resolve has ONE level and the card background shows where the plugin's alpha test cuts it. Next instrument: dump the
  native atlas (colour + alpha) against the plugin's (guest copy of 12704000), and the atlas's mip levels.
== DAY STATE (2026-09-25 evening) ==
  SDK PATH: every pair of the Bower Lake scene draws from the SDK DXBC (TESS1/2: 4.3-5.4M SDK draws per run; non-design
  refusals ~0.015% after the null-SRV fixes; impostor renders skipped as the old path). Pairs VERIFIED by pre-registered
  test: 2 (BCC6E2DE:9A193FEB, DBF9A58A:0B24BD42); the all-pairs configuration judged by picture + surfaces:
  near terrain now matches the plugin (ground 1.008 vs old 0.984), shore mask built (beach sand), water, hero, HUD.
  PRESENTATION: with the SDK path the console-faithful presenter (ngpu_present_post = the XE_SWAP buffer) is right in
  the front end AND the world, incl. the D-pad HUD icon; the old path cannot do this (its swap-surface composite never
  worked). tools/run_native.cmd = that configuration (defaults unchanged).
  OPEN: canopy/impostor atlas (SDK 0.45 vs old 0.27 per-pixel vs plugin - the SDK path's post passes amplify
  pre-existing blocky cards); wedge-near/far (pre-existing); u5 loading-screen background (both paths, no SDK draw
  paints it); u2 halo untested; u3 (time vs distance - question for the user).
- IMPOSTOR ATLAS / CANOPY / WEDGE - FOUND AND FIXED (2026-09-25 evening):
  ATLAS1: the plugin's atlas tile (guest memory at 12704000, raw dump ngpu_guest_raw_addr, untiled 16bpp 8-in-16,
  k_1_5_5_5) = one complete tree on black alpha 0 (alpha-1 13.1%); the native resolve of the same key = nested zoomed
  copies with WHITE ALPHA-1 background (31%); ATLAS_NOOPTA: all 12 native tiles IDENTICAL. WINOFF1: the impostors are
  drawn one after another at the same place (window offset 0, scissor 256x256, base 0); RESPOS1: resolve POSITIONS are
  right (each tile's resolves right after its tree draws); RESPOS2: each tile gets FOUR resolves - level 0 and three
  mips - to DIFFERENT destinations per RB_COPY_DEST_BASE/PITCH (12704000 256x256, 12724000 128x128, 1272C000 64x64,
  1272E000 32x32) from different source surfaces (pitch 320/160/80/80), while the RECORDED parameters (from the API
  call) name level 0 every time -> natively the mips OVERWROTE the tile (nested copies; the last mip's white clear).
  FIX: PerformReplayResolve takes destination / size / source surface from the copy + live registers captured at the
  marker when they differ (ngpu_resolve_from_regs, default ON, both paths); a target whose guessed height is too short
  for a resolve GROWS (grow-only: an exact-height rule thrashed 3,047 times between the 256-row atlas and the 180-row
  bloom sharing pitch 320).
  RESULT ATLAS2: 13 of 13 tiles distinct; opaque-white 0; tile 12704000 alpha-1 12.8% (plugin 13.1%); far-canopy
  mean |luminance diff| vs the plugin 0.192 (TESS2 0.453, old path 0.267-0.298); house 0.990 (was 1.271); THE BLACK
  WEDGE IS GONE - the right-shore hill draws (its terrain LOD textures are mip-chain resolves too). Confirmation n=3 +
  the old path with the fix (OLDREGS1): running.
- ATLAS3 CRASH -> FIXED: a grow-only rule that first LEARNED a full-width height shrank pitch 80 (80 -> 64), the next
  taller resolve recreated it mid-frame and the device was removed (0x887A0001 INVALID_CALL; the plugin shares the
  device, so every leg died incl. the old path). Final rule: the original exact full-width learning + a per-pitch
  GROW-ONLY FLOOR from taller resolves. ATLAS4a/b/c (n=3, SDK path) = frame |luminance diff| vs the plugin 0.183 /
  0.179 / 0.179 (old path SDK0 0.225, TESS2 0.256) - the best of the day; canopy 0.242 x3; wedge region 0.796.
  OLD PATH with the fix (OLDREGS2 vs OLDREGS0): frame 0.223 vs 0.236, canopy 0.244 vs 0.265, wedge 1.013 vs 0.767.
- LAKE AFTER THE FIX (0.75-0.78 state): a gold rectangle in the lake centre. Traced: the shore mask is RIGHT (MASK2:
  plugin 1BB4A000 vs native FBB49000 correlation 0.994, means 0.437/0.436); the water's reflection input F3402000 is
  WRONG (plugin bluish 0.14/0.17/0.22; native orange = leftover impostor-atlas pixels). Sequence (REFLPOS2): terrain
  into the 280x280 reflection target (seq 560-607) -> resolve F3582000 (609) -> RECT BLIT 8A0AE541:C998AD79 into the
  pitch-320 base-0 target (610) -> resolve F3402000 from that target (611). The blit rasterises NOTHING: rect GS emits
  2 triangles, clipper 2 in / 0 out - and with depth clip off still 0 (REFLCLIP1) -> clipped in X/Y/W = its VERTICES are
  wrong. Candidate (not yet tested): small blits read their vertices from a DYNAMIC ring the CPU has rewritten by replay
  time; the bridge snapshots tessellation patch data at record time for exactly that reason but NOT vertex streams.
  Pre-existing on both paths (the old path's lake was already wrong). NEXT: record-time snapshot of small vertex streams.
  (ngpu_resolve_regs_source=false is worse overall - frame 0.372 - so the register source stays ON.)

## 2026-09-25 evening/night (autonomous; user asleep) - the lake, inline shaders, the post chain, EDRAM clears
METRICS USED BELOW (tools: job tmp dmetric.py / surf.py; all at f1500, bowerlake stand, native F99DF000 vs the plugin's
guestscene 199E0000 = the HDR scene BEFORE the post chain): "lum |d|" = mean |luminance diff| of values CLIPPED to 0..1
(NOT the unclipped 0.18-scale figure of ATLAS4 above - different definition, do not compare across); "rgb |d|" = mean
per-channel |diff|, clipped; regions: canopy rows 0-130 x cols 300-1280, lake rows 222-292 x cols 700-1200. Lake state
was not separately gated per leg tonight; every comparison below is between legs of the same session and stand.
- LAKE ROOT CAUSE (the "vertex ring" candidate DIED): the blit VS read its vertices through vf0 while slot 0 held the
  PS's TEXTURE constant (82024802) - at record time too (VSNAP BLIT: guest bytes at 02024800 = 0). The VS was NOT the
  blit's: the plugin's hand-off (rexglue-src command_processor.cpp TrackShaders) never updated g_cur_vs on
  IM_LOAD_IMMEDIATE, so every INLINE-loaded shader was handed off as the previous address-loaded one (here the sky
  cube's 8A0AE541). 1.04 M draws per leg (VS+PS) arrive inline. The plugin's own F3402000 is a blurred copy of
  F3582000 (corr 0.954 at the same coordinates) - the blit does draw on the console.
  FIX (plugin, LOCAL commit f495e67 on rexglue-src main, never pushed): copy the packet's microcode (ring may wrap) and
  hand it off in fields appended to RexNgpuDraw (size-versioned). Native: record-time microcode snapshot by hash
  (UcodeSnapTake/Bytes), inline microcode used when present, older plugins' record size accepted.
  Legs use the patched plugin via NGPU_PLUGIN_DIR=/d/fable2_flash/sdk_legs/bin_main_head_imm (bridge_leg.sh now takes
  the override; default is still seam_record/bin_main_bridge). Control built from the same tree without the patch:
  sdk_legs/bin_main_head_ctl (runtime byte-identical; CTLHEAD2 = STDPAIR1 within 0.003 rgb |d| frame).
  RESULT IMM1/IMM2 vs controls: lake rgb |d| 0.087 vs 0.265-0.282; frame rgb |d| 0.075 vs 0.094-0.096. The gold
  rectangle is gone. UNMASKED A SECOND DEFECT: the far canopy washed white (canopy rgb 0.995/0.996/1.0 vs plugin
  0.948/0.924/0.992).
- SECOND DEFECT traced: inline VS B2D99AC2 runs the whole post chain (seq ~2270-2410 + composite CCDE0F8A); it had
  never drawn natively before. Its 640x360 mask F2C0C000 came out 1.0 everywhere (plugin: 0 but for a small blob;
  control: 0). Cause: that surface (0A000280, EDRAM base 0) is zeroed on the console by the SCENE's clearing resolve
  (F99DF000, RB_COPY_CONTROL 00100340, seq 2077); the game itself clears only the partial last tile row with a rect.
  Natively each target is its own texture and never received that clear.
  Also found+fixed on the way: vertex-snapshot overlays in the mirror were LAST-WRITER-WINS (the mirror is an upload
  heap read at EXECUTION) -> per-draw scratch after guest physical + fetch-constant redirect (kMirrorScratch); the
  snapshot budget 4 MB/frame -> 32 MB (0 drops since). Neither changed the mask (SCR1/SCR2) - kept as correct design.
- EDRAM COLOUR CLEAR ALIASING (EdramClearColorAliases): a colour-clearing resolve with RB_COLOR_CLEAR 0 marks colour
  targets wholly inside its tiles for a clear at their next bind. ECC1: mask F2C0C000 = 0 (matches), bloom F2DEC000
  0.034 vs plugin 0.033 (corr ~0.88), F2D74000 half-float 0.91/0.89/0.67 vs 0.89/0.85/0.61 (corr 0.8-0.9) - the post
  chain now matches pass by pass on the 3 passes decodable from guest memory. BUT the far TREES DISAPPEAR (image
  canopy_ecc_ab.png: CTL4 top / CTL5 middle / plugin bottom). Bisection (SK*, SKA/SKM, SKX, SKY, control plugin):
  the damage is the clear reaching 05000140 (the pitch-320 base-0 surface shared by the impostor atlas, the lake blit
  and post passes); skipping only it restores the control exactly (SKY1 = CTL4). Rules tried and REJECTED by
  measurement: cancel on any overlapping bind (mask loses its clear), cancel by covering bind (same), expire at the
  frame boundary (trees still gone), exempt targets already resolved this frame (mask 1.0 again via F2CFC000 + a white
  rectangle artefact).
  READING: the console/plugin do not "clear aliases" - Xenia's RT cache TRANSFERS the owner's pixels into a target that
  binds over tiles another target last wrote. Probably the atlas cards get their alpha from that transferred content
  (cleared = alpha 0 = alpha-tested away) - a HYPOTHESIS, untested. THE REAL FIX IS EDRAM OWNERSHIP TRANSFER
  (tile-granular copies across pitch/format/MSAA) - the next work item; clears alone cannot satisfy both the mask and
  the atlas.
  DEFAULTS NOW: ngpu_edram_clear_alias_color on, cancel mode 2 (never), exemption off - and ACTIVE ONLY when the plugin
  hands off inline microcode (on the standard plugin it hurt: CTL5/CTL8 lake 0.437 vs 0.266, frame lum 0.114 vs 0.094).
  n=3 confirmation of these defaults on both plugins: FINIa/b/c + FINSa/b/c (n=3 each: FINI a/b/c frame rgb 0.074 x3, lake 0.087 x3, canopy rgb 0.979/0.985 - trees absent; FINS a/b/c on the STANDARD plugin = the baseline, frame 0.093-0.095, lake 0.263-0.264, i.e. inert there).
- LEG FAILURES COUNTED (peer's point - "about 1 in 6" was an impression): of ~50 legs tonight, 2 ran at ~7 fps and
  never reached f1500 with no crash (REFLSRC1 with 21 GB of other VRAM resident; EB3, clean on rerun EB3b) and 2 were
  NOT flakes - SK1 x2: the value 04020118 went unquoted into the tuning TOML (leading-zero number = parse error), the
  config was dropped and the bridge never ran. Values that start with a digit must be prefixed (s04020118). leg.sh
  now retries once on "no scene dump" and PRINTS the retry; leg_once.sh is the single attempt.
- u3 PREDICTION (peer's point, written before the user answers): if far textures go white while STANDING STILL (time),
  a frame-to-frame leak (post output feeding the next frame) is the family; if only on APPROACH (distance), LOD /
  impostor. Tonight's white canopy is per-frame (post chain / EDRAM), not accumulating - it is present at f1500 in
  every leg of a configuration with spread < 0.002.
- COMMITS (local): rexglue-src f495e67; native-gpu 6c0b7f7, 888ae90 (+ earlier c2 of the day).
- ORACLE UNCHANGED (verified by claudecode-76 reading the diff, and confirmed here): rexglue-src f495e67 is +18/-1 in
  command_processor.cpp and touches ONLY what the plugin HANDS OVER (two new globals, the inline microcode copied in
  TrackShaders, four size-versioned fields appended to RexNgpuDraw; the one pre-existing line refactored is
  semantically identical). The plugin's render path is untouched, so no re-baselining: ATLAS4's "SDK 0.180 vs the old
  path's best 0.225" (UNCLIPPED luminance scale) still stands. Size-versioning is what makes the hand-off safe for older
  consumers and newer exes alike (the exe now accepts both record sizes).
- n=3 COMPLETE (not mid-flight): FINI a/b/c (inline plugin, colour clear aliasing ON) frame rgb|d| 0.074 x3 (spread
  0.000), lake 0.087 x3, canopy rgb 0.979/0.985/1.000 (trees absent); FINS a/b/c (standard plugin) = baseline, frame
  0.093/0.094/0.095, lake 0.263/0.264/0.263. All figures here are the CLIPPED per-channel metric.
- PER-TILE MODE (ngpu_edram_alias_mode=2) REJECTED: multi-rect ClearRenderTargetView FROZE the process after a clear of
  a full-screen target (TILE2/TILE3, reproducible; one rect per call works - TILE5); per tile it scored frame 0.170-0.177
  with a black hole in the foreground terrain (TILE5/TILE6, also with depth-tile claims). Code stays, default mode 1.
- THE FAR TREES, ROOT CAUSE (a THIRD defect, under the second): the impostor atlas (tiles F2703000.., rendered into the
  4xMSAA surface 05020140 at seq 25-28 each frame, resolved from 04000140 via the EDRAM owner) was EMPTY with the inline
  plugin: the foliage draw (VS 475EC9F7, 693 prims) ran 0 pixel shaders (pipeline stats: clipper 693 in / 693 out,
  PS 0; the trunk A5846836 ran ~19,800). Eliminated by intervention, one at a time: vertex snapshot (FOL8: both streams
  snapshotted, still 0), cull (debug 2), depth clip (debug 8), option A white depth (ATLW), culprit found by ngpu_sdk_debug
  bits: debug 1 (no depth/stencil) -> PS 2,389. It FAILS THE DEPTH TEST (GEQUAL, reverse-z) against stale atlas depth:
  on the console the scene's end-of-frame resolve (00100340, colour AND depth clear) zeroes those EDRAM depth tiles; the
  rect before each tree clears only the 16-px partial tile column. NOTE: ngpu_depth_off_surf did NOT reach SDK-path draws
  (ATLD read "not depth" - that reading is VOID; the instrument had no subject).
  The standard plugin's "orange tree" in the atlas was drawn by STALE microcode (the wrong PS), not the tree's own.
  FIX CANDIDATE: the existing depth clear aliasing ngpu_edram_clear_alias=true (default OFF since EDRAMCLR1/2/6). IMPD1:
  foliage PS 2,263, atlas tile 14.1% opaque (plugin 13.1% per ATLAS2), far trees VISIBLE again (impd1.png), frame 0.076,
  lake 0.088, canopy rgb 0.905/0.872/0.986 vs plugin 0.947/0.923/0.992 (the scalar canopy |d| 0.055 got WORSE than the
  white version's 0.041 - the metric rewarded a washed-out canopy; judged by the picture + atlas coverage). n=3 on both
  plugins (IMPD a/b/c, IMPS a/b/c) running before any default changes.
- n=3 DONE: IMPD a/b/c (inline plugin + depth clear aliasing) frame 0.076 x3, lake 0.087 x3, canopy rgb 0.906/0.873/
  0.986, house ratio 0.970-0.972 (FINI 1.166) - far trees back. IMPS a/b/c (standard plugin + depth aliasing) frame
  0.097 x3 vs FINS 0.093-0.095, lake 0.271-0.275 vs 0.263, house 0.918 vs 0.965 - a small regression there.
  DEFAULT (recommended option taken): ngpu_edram_clear_alias_auto = ON -> depth clear aliasing active only when the
  plugin hands off inline microcode (same gate as the colour clears). DEFI (defaults, inline plugin) = IMPD; DEFS
  (defaults, standard plugin) frame 0.096, lake 0.279 (n=1, inside tonight's standard-plugin range 0.263-0.282),
  canopy = FINS -> aliasing inactive there as intended.
  TO SEE TONIGHT'S STATE: stage D:\fable2_flash\sdk_legs\bin_main_head_imm\{rexgpu-xenos,rexruntime}.dll into
  out\build\win-amd64-Release (back up the two DLLs there first) and run tools\run_native.cmd. The default build folder
  still holds the v1.0.0 pair (bridge_leg.sh restores it after every leg).
- OPEN after tonight: the lake's animation/sparkle (the user watching the legs: "the lake is the correct side but no
  shader effect or animation"); u5 loading background; u2 halo; the u3 time-vs-distance question (prediction recorded
  above); canopy colour 0.906/0.873 vs 0.947/0.923 (denser/darker than the plugin); EDRAM ownership transfer proper.
- LAKE/FOLIAGE ANIMATION (the user, watching: "no shader effect or animation"): the water PS (F22BC502:B9B8BECA) and
  the foliage VS read two tiny lookup tables (1FAB0000-1FAB3FFF: 64x1 16_16_16_16_EXPAND + 256x1 8888). WATCH1
  (per-frame hash of those guest pages, frames 1400-1499): 66 of 100 frames change a page, triple-buffered - the game
  REWRITES them every frame. The native cache re-uploaded 1FAB3000 4 times in a whole leg: its settle rule (wait two
  quiet frames) never fires for a per-frame table (LUTTRACE1 had found this for traced bases only). FIX (default ON,
  ngpu_tex_nosettle_small_bytes 16384): textures of <= 16 KB re-upload on any write since their upload. LUT1: 285
  re-uploads by frame 1840 (was 4); single-frame metrics unchanged as expected (frame 0.076, lake 0.087); fps equal
  (12.6 vs IMPDa 12.4 in these logging legs); standard plugin LUTS = baseline (frame 0.094, lake 0.266).
  NOT the cause (checked, refuted): the DXN wave normal map 13E30000 - staged bytes decode as a correct subtle normal
  map (dxn_staged.png); my first reading of an older dump as "unswapped" was wrong.
  Motion itself is not measurable in a single-frame dump; the next check is a two-frame native dump of the lake.
- ATTRIBUTION (claudecode-76's audit of the per-leg artifact files): "S" legs (FINS*, IMPS*, ATLS*, DEFS, LUTS) use the
  seam_record bin_main_bridge PAIR (plugin b57537ba177c + runtime d190e2093473) - plugin AND runtime differ from the
  I legs (runtime 0dd6d3202392). S-vs-I says "that pair does X", never "the plugin change does X". The one-variable
  contrast is CTL* (bin_main_head_ctl: same runtime, unpatched plugin) vs I; the auto-gating decisions rest on CTL5/CTL8
  (colour) and on IMPS as a screen only (depth). DEFI/DEFS and LUT1/LUTS are n=1 pairs - screens, not cited figures.
- FRONT END with the inline plugin (FRONTI1 vs FRONTS1, sdk_legs/*_strip.png): main menu matches the plugin (buttons
  drawn; the standard pair missed them at the same moment); world = lake blue with reflections + far trees. Loading
  screen (u5) STILL black: the loading composite 15F75D1D reads the HDR scene F99DF000 + bloom, and both the native
  AND the plugin's guest copy of 199E0000 are all-zero at the loading frame (plugin read at guest f780) - the painting is
  not in the scene buffer. The 6-texture quads VS 7B22146D on 14000500/14010500 are clipped whole natively (clipper out
  0; their clip x/w about -1.8 / +5, z/w 1.03 from the recorded c0-c3) - either genuinely off-screen map pieces or the
  painting with a camera we mis-read; needs plugin-side instrumentation. The colour-format fold (ngpu_rt_decode_color,
  12->3) is already in force - not the cause.
- LAUNCHER: tools\run_native.cmd --inline-plugin stages bin_main_head_imm's DLLs (backup in
  out\build\win-amd64-Release\dll_before_inline_plugin, restored on exit). Verified by a dry run against a stub run.cmd
  (staging, argument passing incl. "a=b", restore, and the flagless path) - the game itself was NOT launched outside the
  leg harness.
- MEMEXPORT ON THE SDK PATH (ngpu_sdk_shm_gpu, default OFF, read at startup): a DEFAULT-heap copy of guest memory with a
  UAV; mirror stores are queued and copied into it before each SDK draw; memexport draws (VS 0D7251C2 / PS 60E8DEF9
  point lists, 45,138 a leg, previously refused) run with the UAV bound (kSysFlag_SharedMemoryIsUAV was already set),
  a UAV barrier after; their eA stream ranges (decoded as draw_util AddMemExportRanges) are loaded first and their pages
  become GPU-owned (no CPU re-upload, no vertex-snapshot overlay). MEX1: 43,074 memexport draws, every one with a valid
  range, no device removal, fps unchanged (12.2-12.5 in these logging legs), 1.65 GB copied over the leg. Single-frame
  metrics = DEFI, and MEX1-vs-DEFI pixel differences (29,808 px > 0.05) are within the leg-to-leg floor (IMPDa/b/c:
  21,773-28,542, all wind on foliage) - NO VISIBLE EFFECT at the Bower Lake stand. Kept OFF (no measured benefit, extra
  copies); the consumer of the exported data is unknown - look for it elsewhere (particles, towns) before enabling.
- OPTION A excluded for the canopy colour (OPTA0, option A off: canopy 0.908/0.873 vs DEFI 0.905/0.872) - the user's
  decision stands; the denser/darker far canopy (vs plugin 0.947/0.923) is still open.
- LEG RETRIES since the retry was added (~22:00, ~60 legs): 3 succeeded on retry (FOL2, FOL10, OPTA0; only FOL2
  followed a rebuild), 1 failed both attempts = a real defect (TILE1, the mode-2 multi-rect freeze). Earlier no-dump
  legs: REFLSRC1 (21 GB of other VRAM resident), EB3 (clean on rerun), SK1 x2 (TOML leading-zero value - a defect).
- CORRECTION to "memexport: NO VISIBLE EFFECT": the exported data IS consumed - by the TESSELLATED TERRAIN. MEX2 logged
  the export destinations: 1BB20000-1BB3FFFF, 36 x float4 per draw = the terrain's patch index / edge-factor data (the
  prim-18 draws' "ib 1BB35xxx"). The terrain read it from the record-time CPU snapshot of guest RAM, which never gets
  it (memexport readback off). MEX1 showed nothing because the IB still came from that snapshot. FIX (with
  ngpu_sdk_shm_gpu): an index buffer in GPU-written pages is read from the GPU copy, which this frame's own memexports
  filled earlier in the frame. MEX3: 104,410 index buffers from GPU-written pages; frame rgb|d| 0.068 (DEFI 0.076),
  lake 0.057 (0.088); THE RIGHT-SHORE HILL IS DRAWN (mex3.png) - WEDGE-NEAR and WEDGE-FAR close together, plus the
  dark line along the near shore. Found with a new instrument: the PIXEL PROBE (ngpu_pixprobe_x/_y/_rt at the query
  frame: colour+depth of one pixel after every SDK draw, changes logged) - at (1098,254) the far-terrain draw wrote
  BLACK; after the fix a terrain draw (C30A97D9) paints the hillside there.
  Still at the probe pixel: later draws (water B9B8BECA, far terrain 4DF4ADDB) overwrite it - the picture looks right
  at that spot, so this is to re-read with n=3 before claiming the pixel.
- GPU SHARED MEMORY CONFIRMED, SAME EXE (8a1fb2b99ebd) BOTH ARMS: OFF (NOSHMa/b) frame 0.076/0.076, lake 0.087/0.088;
  ON (SHMIa/b/c + DEF2I with the new default) frame 0.068/0.067/0.068/0.068, lake 0.057 x4, lake RATIO 0.986-0.987
  (DEFI 1.037), big arch 1.025-1.029 (1.049). Gain frame 0.008, lake 0.030; each arm's spread <= 0.001 -> both resolved.
  Standard pair (SHMS) and control plugin (SHMC) with it ON = their baselines (inert without the inline plugin).
  COST: fps 9.4-10.2 vs 10.6-10.7 on the same exe in these logging legs (~5-10%; ~1.6 GB of mirror->GPU copies a leg).
  DEFAULT NOW ON (ngpu_sdk_shm_gpu = true, recommended option taken). Optimisation candidate: copy once per frame /
  skip unchanged pages.
- STATE AT THE END OF THE NIGHT (FRONTI2_strip.png, all defaults + the inline plugin): main menu = plugin; world = plugin
  to the eye (lake colour + reflections, far trees, right-shore hill, arches); loading screen still dark (u5). A small
  yellow label near the island in one native frame - not yet examined. Try it: tools\run_native.cmd --inline-plugin.
  Numbers (clipped per-channel metric, same exe both arms where stated): frame 0.094 (start of the night, standard
  pair) -> 0.068 with the inline plugin + tonight's defaults; lake 0.265 -> 0.057, lake ratio 0.79 -> 0.986.
  OPEN, in the order I would take them: (1) loading-screen art - instrument the plugin side (what draws it); (2) fps cost
  of the GPU shared memory copies; (3) canopy density (1x alpha-to-coverage suspected); (4) EDRAM ownership transfer
  proper (the clear aliasing is a rule-of-thumb stand-in); (5) u2 halo, u3 time-vs-distance (prediction above).
  NOTHING PUSHED: rexglue-src f495e67 and every native-gpu commit are local.
- u5 LOADING SCREEN - FIXED (2026-09-26 night, peer re-task). Path: ngpu_sdk_trace_ps on the painting texture's PS
  (C1B813C9, binds 170A0000 1024x512 DXT) -> the painting IS drawn: 10 quads VS 7B22146D on the scene target 14010500
  at seq ~127-141, BEFORE the scene resolve (ORDER1: order correct, resolve copies 17 draws). Pipeline stats: clipper 2
  in / 2 out, PS 0 -> killed after clipping. Depth clip off (FRONTDC1): still black. Depth test off (FRONTD1, debug 1):
  the painting, map and quote panel APPEAR. Cause: the scene resolve's depth clear (RB_COPY_CONTROL 00100340) was
  applied with ClearZ's GUESS (0 or 1 from last frame's GREATER/LESS votes); the painting tests GEQUAL (reverse-z) and
  failed every fragment. FIX (ngpu_clear_depth_from_reg, default ON): the pending depth clear writes the resolve's own
  RB_DEPTH_CLEAR (captured at the marker). FRONTCZ1: loading screen = plugin (painting, map, quote panel, title);
  world CZIa/b frame 0.067/0.067 (DEF2I 0.068), lake 0.057/0.058, standard pair CZS unchanged (0.094).
- NOTE for the S-vs-runtime confound (peer): SHMS/SHMC/CZS being inert localises those fixes to fable2.exe; they do NOT
  address the S-pair confound (plugin + runtime both differ in S legs) - that remains as recorded above.
- OFF-state drift (peer): the no-GPU-shm frame figure moved 0.074 (exe ba506f0883ac) -> 0.076 (8a1fb2b99ebd) while
  the lake stayed 0.087; about twice the within-arm spread, unexamined - a row, not a bisect.
- COLOUR-CLEAR SIBLING (peer's point): the resolving target's own pending colour clear always wrote BLACK. Measured: the
  game's own-copy RB_COLOR_CLEAR is FFFFFFFF for the final composite resolve FECFE000 (14000500/00020000) - white, not
  black (CCR1 log); the marker's live set also showed D8060180 once (one copy ahead). Implemented
  ngpu_clear_color_from_reg (decode for 8888 / 2_10_10_10 / 7e3; else black, counted) - CCR1 metrics unchanged (frame
  0.067, lake 0.057; the composite overwrites that target), so kept OFF until a picture needs it; the channel order for a
  non-trivial value (D8060180) is unverified.
- FPS COST OF THE GPU SHARED MEMORY (peer: histogram, not mean): PERFON/PERFOFF same exe - p50 94 vs 69 ms with p99 near
  p50 in BOTH (shared ~430 ms spikes once each) = uniform work, not waiting. Cause: a copy + buffer transitions per SDK
  draw (1.23 M copies a leg). FIX ngpu_vsnap_preload (default ON): the frame's vertex snapshots go into scratch in ONE
  store at the start of the replay frame; draws only point into it. PERFON2: p50 ~76 ms (21,540 copies, same 1.6 GB),
  metrics unchanged (frame 0.067, lake 0.057). Remaining cost ~7 ms/frame vs OFF - next: skip unchanged bytes.
- SKIP TRACE cap now reports its suppressed count (peer's point: a raised cap is still a cap).
- METHOD TEMPLATE (peer asked it be recorded verbatim): "Pre-registered for FRONTCZ1 (ngpu_clear_depth_from_reg=true,
  nothing else changed): the loading painting appears, and the world views are unchanged." - the prediction, the one
  switch, and the negative control, written BEFORE the run.
- PERF, CORRECTED (supersedes "36% cost" and "preload 94->76 ms"): interleaved ABAB on ONE exe (4ff25b4db589, same lake
  state) - ON p50 105 / 81 ms, OFF 96-97 / 98 ms. OFF is stable, ON swings 24 ms run to run; no cost is resolved. The
  earlier single-run figures crossed builds, and OFF itself drifted 69 -> 97 ms over the night on largely unchanged code
  (environment; GPU idle, CPU 4% when checked afterwards). The preload saves ~3 ms back to back (PERFONNP 106 vs
  PERFON3b 103). The earlier "waiting" hypothesis was withdrawn by the peer (tail p99-p50 comparable: 7-10 ms both arms).
  NEXT for perf: n>=3 per arm, interleaved, with the machine's other load logged per leg.
- SECOND POPULATION - BOWERSTONE MARKET (save card 2; bridge_leg.sh now takes NGPU_SAVE / NGPU_REGION): runs ~4.7 fps in
  logging legs, so it never reaches f1500 - dumped at f1140 (the plugin probe fires on multiples of 60). REPRODUCIBLE:
  plugin-vs-plugin 0.002-0.011, native-vs-native 0.003-0.011 over 5 legs (the peer's control) - so its frame rgb|d| of
  0.30 is REAL, not mistiming. Geometry matches; the LIGHTING does not: native sunlit/warm (stone pixel 2.93/1.54/0.56),
  plugin in shadow/cool (0.23/0.28/0.34). Identical on the inline and the standard plugin -> not tonight's changes.
  Excluded by one leg each: option A (TOWNA0), accumulate (TOWNNA), the small-texture re-upload (TOWNNS). Pixel probe
  (420,520): the arch material pass (PS 9A193FEB) writes the bright value; draw #0 of the frame already holds it.
  Working hypothesis (untested): SHADOWS are not applied natively in town (at the lake the stand is sunlit, so it never
  showed) - next: dump the native shadow-map depth resolves (1024x1024 F2C02000/F3002000) in town and see whether
  casters are drawn, then which pass consumes them.
- TOWN LIGHTING - MECHANISM FOUND, NOT YET FIXED. The over-lit town comes from F2B1C000, the full-screen depth resolve
  bound in the lighting shaders' slot 8 (the "12B1D000" option A serves WHITE). Natively its source (14000500 depth key
  2D0) has no writer, so it reads 1.0 whether option A is on or off - TOWNA0 could NOT discriminate (white == 1.0).
  DIAGNOSTIC ngpu_depth_resolve_borrow (default OFF): an unwritten depth resolve copies the same-size depth written this
  frame (the scene's, 14010500). TOWN (option A off): frame rgb|d| 0.301 -> 0.085, stone pixel 2.93/1.54/0.56 ->
  0.30/0.32/0.32 (plugin 0.23/0.28/0.34), the arch goes into shadow as in the plugin (tbor1.png) - some over-shadowing
  remains. LAKE (option A off): frame 0.068 -> 0.225, ground ratio 0.265 - badly WORSE. With option A on it has no
  effect (TBOR2 = TOWNI). So neither rule is right, and the layout says why (DEPTH RESOLVE REGS, identical in both
  scenes): the scene depth is 2xMSAA 1280x720 at EDRAM base 0x400 (1440 tiles: 1024-2047 then wrapping 0-415); F2B1C000
  reads a 1x 1280x720 depth at base 0x2D0 = tiles 720-1439. On the console F2B1C000 is therefore a REINTERPRETATION:
  its tiles 1024-1439 hold the scene's 2x samples read as 1x (each 1x tile row = one 2x tile row stretched 2x
  vertically), tiles 720-1023 hold whatever last wrote them - scene-dependent, which is why the lake reads "white" and
  the town "real depth". THE FIX IS THE EDRAM TILE MODEL (open item 4): a tile-level depth transfer (a small compute
  pass mapping 1x tile rows onto 2x sample rows) for a depth resolve whose source tiles another surface owns. The
  per-tile depth owner table exists (g_edram_depth_owner) but records no claims (0) - wire it first.
  Option A stays as the user chose it; nothing here changes a default.
- EDRAM DEPTH TILE MAP (ngpu_depth_tilemap, diagnostic, default OFF): reproduces the reinterpretation row by row where
  another native depth covers the resolve's tiles (same pitch in tiles): F2B1C000 (14000500 key 2D0, base 720, 1x) <-
  14010500 key C00 (base 1024, 2xMSAA, 1440 tiles): 416 pixel rows (tiles 1024-1439 -> rows 304-719). TOWN (TMAPT,
  option A off): 0.301 -> 0.150 - the lower screen goes into shadow, the top 304 rows (outside the overlap) stay bright.
  LAKE (TMAPL, option A off): ground ratio 0.25, frame 0.217 - as bad as the full borrow. So at the lake the console
  does NOT hold the scene depth in those tiles at the copy (seq ~997), in town it does - it depends on what WROTE or
  CLEARED them earlier in each scene's frame, not on the static layout. NEXT: wire g_edram_depth_owner (0 claims today)
  so every depth-tested draw and depth clear records the tiles it touched and when; the resolve then copies from the
  real last writer of each tile (or keeps the clear value). Everything here is behind switches that default OFF;
  option A (the user's decision) is unchanged.
- USER REPORT (2026-09-26 ~05:00, watching my leg windows): "effects at the title screen and menus are off; the game
  loading screen seems to have a background now". Burst capture (burst_front.sh TITLEB1: 15 frames 0.2 s apart, both
  windows at the same instant): the title logo's dissolve + glow particles DO render natively (particle quads VS
  29B6506F / PS 3001AAF2 run 35-78k pixels each, QUERY armed by the new ngpu_sdk_query_ps), but the native window LAGS
  the plugin by ~0.3-0.5 s through the front end (b01: native logo already dissolved while the plugin shows the menu;
  b03: plugin on the save cards, native still on the menu) and presents ~37 fps against the plugin's 60 in these
  instrumented legs - so the effects look partial/late. Not a missing effect as far as measured; excluded on the way:
  3D-texture white stand-in (the logo/particle textures are 2D cache textures), per-frame texture rewrite (WATCHT:
  14C00000 changed on 2 of ~85 frames). OPEN: the native window's front-end frame rate/latency (partly my instrumentation:
  census + logging are on in every leg; run_native.cmd has neither) - measure with the launcher's own settings.
- The user confirmed the in-game movement during the TOWN_R*/LAKE_R* legs was in MY windows: native-vs-plugin figures
  within those legs stay valid; cross-leg comparisons from that window are not.
- Tools added: ngpu_sdk_query_ps (arm the per-draw query at the first draw of a PS), burst_front.sh (job tmp).

## 2026-09-26 morning - ASYNC REPLAY (commit 057bc12, ngpu_async_replay default ON)

- WHY: the user saw slowdown "even with the xenos version". Measured (COST1, sync): the native replay ran on the guest
  present path (ShadowPresent -> EndFrame), ~70 ms of every ~75 ms frame at Bower Lake - the game ran at 13.6-14.7 fps.
- WHAT: the guest thread only pumps the window and signals; a native thread runs InitDraws/BeginFrame/EndFrame on the
  latest handed-off frame. Guest draw hooks return early (they are DEAD in bridge mode anyway); NoteResolveDest still
  QUEUES (under g_bridge_resolve_lock) - it is where the replay's resolve parameters come from. ResolveNative records
  g_resolve_pages / g_resolve_hist on the replay thread. ASYNC WATCHDOG logs a phase stuck > 3 s.
- Three bugs on the way, each a lesson: (1) no InitDraws on the worker -> every draw "pipeline", 0 issued; (2) the
  resolve hook turned off -> every resolve lost, no f1500 scene dump; (3) uncapped accumulate snowballed once the game
  outran the replay (130k-176k draws per replay, 5-10 s each - looked like a stall at replay frame ~1000). Fix:
  ngpu_async_accumulate_cap 4000 (a lake frame is ~2500-3500 draws: gameplay replays the LATEST frame, small load-time
  frames still merge).
- NUMBERS (same binary, lake save, report as a PAIR): game 60 fps; replay 80-115 ms per frame = ~9-12 native frames/s.
  Picture native-vs-plugin within each leg, f1500: async 0.060/0.060/0.061 frame, lake 0.039 x3 (ASYNCM4, ASYNCM5,
  ASYNCD1 = defaults); sync 0.067/0.067, lake 0.058 x2 (SYNCC1, SYNCC2).
- REGIME CHANGE (claudecode-76's framing guard): async f1500 is a LATER game time than sync f1500 (the replay counts
  replays, not guest frames) - the scene differs (haze, canopy). Every picture figure before 057bc12 was sync; tables
  must not mix the two. Async numbers are not claimed as a picture improvement.
- NEXT: the replay's own cost (~30 us per draw on the worker) is now the native window's frame rate - profile EndFrame.

## 2026-09-26 morning - REPLAY COST 115 -> 14 ms (commit 7bbfbfe), measured with xperf

- METHOD (reusable, job tmp): prof_leg.sh TAG runs an instrumentation-off lake leg and takes a 20 s xperf sample
  (PROFILE + stackwalk, 1 kHz) once the replay reaches 1200 replays; `xperf -i X.etl -symbols -o X.csv`
  (_NT_SYMBOL_PATH = the build dir); prof_agg.py X.csv TID = exclusive/inclusive by function for the replay thread
  (TID = the [tNNN] on the "ASYNC REPLAY: mean" line); lines.py X.csv TID BASE fn... maps each hot function's CALLER
  return address to file:line through dbghelp (BASE = fable2.exe's I-DCStart BaseAddr in the CSV).
- STEPS (lake, ms per replayed frame): 115 (PROF1: 79% PageReadable->VirtualQuery on the microcode SNAPSHOT, a heap
  copy the page cache never covers) -> 34 (PROF2: tessellation note snprintf + WC reads, query label, texture
  create+retire per LUT re-upload) -> 27 (PROF3: 80 KB register-file memcpy per draw in BuildDrawConstants, string
  keyed SRV dedupe, per-draw container synthesis) -> 24 (PROF4: push_back constant packing, per-sampler cvar lookup
  by name) -> 14 (PROF5, PERFM1/2: 13.7-15.9 ms).
- RESULT: the native window replays EVERY guest frame in steady state (0 coalesced); game 57-60 fps. Picture f1500
  0.061/0.061, lake 0.039 (async before: 0.060-0.061 / 0.039).
- FRONT END (TITLEB2, burst): replay 2-4 ms per frame through title/menus, nothing coalesced - the 0.3-0.5 s lag of
  TITLEB1 was the sync/slow replay. REMAINING LAG: the load into gameplay coalesces ~170 frames (~3 s) - first-use
  shader translation (23 "FAILED in ~160-270 ms" DXIL builds in the log) runs on the replay thread. NEXT: translate
  new pairs on a worker (or pre-warm from the pair census) so the transition does not stall the window.
- Remaining profile (PROF4): SdkDraw 68% of which D3D12/driver ~33%, WriteSrv/GetTexture ~17%, BuildDrawConstants
  now small. EndFrame still waits on its own fence every frame (no CPU/GPU overlap) - a second frame in flight is
  the next structural win if more headroom is wanted.
- LOAD->GAMEPLAY LAG, mostly fixed (commit after d4d515c): it was NOT shader translation (those "FAILED in ~270 ms"
  builds are the old path's jit thread) - PROF6 showed VirtualQuery again, re-asking about pages that had answered
  "not readable". PageReadable now caches whole regions and 100 ms negative answers: coalesced at the transition
  150 -> 12 frames (PROF7). Picture PERFM3 0.060 / lake 0.039.

## 2026-09-26 morning - TOWN LIGHTING: the plugin's EDRAM ownership chain, traced (commits 759a0bd, rexglue-src 23ace0b)

- INSTRUMENT: rexglue-src ChangeOwnership logs every ownership CHANGE of one EDRAM tile (env REX_OWNER_TRACE_TILE,
  REX_OWNER_TRACE_AFTER_S; unset = no cost). Build: rexglue-src\build_texpack.cmd -> D:\fable2_flash\sdk_legs\
  bin_owner_trace (the known-good bin_main_head_imm is untouched). Legs OTRACEL (lake) / OTRACET (town), tile 1100.
  Key fields: msaa 0/1/2 = 1x/2x/4x; pitch = tiles at 32bpp.
- FINDING (identical cycle every frame, lake AND town, 160 frames each; no resolve clear changes this tile's owner):
    shadow/small 4x targets (len 2048 - their height estimate is unbounded, they sweep the whole EDRAM)
    -> scene depth 2x base 1024 (len 1440)
    -> 4x surface 0A020280 depth base 0 fmt0 -> 0A020280 depth base 720 fmt0   (the half-res 640-pitch 4x pass)
    -> 14000500 depth base 720 fmt0 1x (len 720)  = F2B1C000's source
    -> 0A020280 base 720 again -> scene base 1024 (len 1024) ...
  So on the console F2B1C000 = the scene depth's EDRAM bits carried through the half-res 4x pass's targets (same
  absolute tiles, same 16-tile pitch), overwritten wherever that pass writes depth - which is why the lake reads
  "white" and the town "real depth". Every transfer is raw EDRAM samples, MSAA-reinterpreted (2x/4x/1x sample
  layouts differ), and the plugin keeps a separate HOST DEPTH store (host_depth_store_*msaa_cs) so a target that
  gets its own tiles back recovers its float depth.
- ngpu_depth_owner_rule=3 (the plugin's claim rule + a pitch-matched row-copy transfer, default OFF): town 0.286 ->
  0.345, lake 0.060 -> 0.301 (a transfer blacked out the scene's top 208 rows). Row copies cannot model 4x<->1x
  sample layouts or the host-depth restore. Rejected as a default.
- RECOMMENDATION: port the SDK's render-target cache (pipeline/render_target/cache.cpp 1,419 lines + d3d12/
  render_target_cache.cpp 5,799 lines, transfer + host-depth-store shaders) as the native EDRAM model - the same
  move as the SDK DXBC migration the user chose on 2026-09-25. Everything smaller is another approximation that will
  fix one scene and break another (four so far: borrow, tilemap, owner rules 1/2, rule 3).

## 2026-09-26 midday - BACKEND TRANSPLANT (user: "yes lets do it, lets get this to 100%", then "work autonomously until the goal is reached")

- WHAT: the plugin's own D3D12 backend is vendored in-app (src/native_gpu_xlat/rtc_d3d12, rexglue-src 23ace0b, tool
  job-tmp vendor_rtc_d3d12.py, ORIGIN.txt): command processor (draw/copy/swap) + base, pipeline cache, primitive
  processors, render-target cache (EDRAM), texture cache (incl. the texture pack), shared memory, D3D12 shader. Native:
  facade.{h,cpp} (provider on plume's device/queue), graphics_system_standin.h (no presenter), native_gpu_backend.{h,cpp}
  (the replay driver = a friend of the command processor doing what the PM4 parser would).
- SAFETY: every readback cvar FORCED off and the readback landing / guest-memory data providers PATCHED out - the
  native backend never writes guest memory (the plugin, still running, owns it). "NATIVE PATCH" marks each site.
- DRIVE: ngpu_backend (default OFF) + ngpu_backend_lockstep (default ON): the backend is fed INSIDE the plugin's draw
  and swap callbacks (same thread, same instant) - register diff over 0x2000-0x23FF + constants, microcode from guest
  memory or the packet's inline copy (with the packet's OWN length), IssueDraw; at the swap fetch constant 0 + the
  plugin's gamma ramp (new plugin export RexNgpuGetGammaRamp, rexglue-src 86001af) and IssueSwap into a native guest
  output that the window presents (with ngpu_present_post + ngpu_post_raw, as run_native.cmd sets). The replay-driven
  mode (lockstep off) read CPU-written resolve rectangles a frame late: 39k empty resolves + garbage in town (BET1).
- PLUGIN: legs use D:\fable2_flash\sdk_legs\bin_backend (rexglue-src 86001af: inline microcode + gamma export + the
  env-gated owner trace). bin_main_head_imm is unchanged.
- RESULTS (window captures at the same instant, client area from geometry, overlay masked; job-tmp windiff.py):
    lake  lockstep BEL5/6/7   mean|d| 0.0042 0.0042 0.0043, pixels >0.1: 0.0%   game 45 fps, 0 draw failures
    town  lockstep BET4/5/6   mean|d| 0.0027 x3,               pixels >0.1: 0.2-0.3%  game 19-24 fps, 0 failures
    CONTROL old native path   PERFM1 0.0887, SYNCC1 0.0939 (lake), pixels >0.1 ~53%
  Front end (TITLEBE1 burst): 10 of 15 captures 0.002-0.02; the rest are fast transitions (logo dissolve, menu fade,
  a dialog in one window only) where the two captures are ms apart - needs a frame-exact check.
- WHAT THE NUMBERS MEAN (claudecode-76's point): with the plugin's own backend transplanted, "native matches plugin"
  is a WIRING check for this path, not independent evidence of correct rendering. The independent checks left: the
  old native path (kept behind its switches) as a control, and the endgame - the picture with the PLUGIN'S rendering
  switched off.
- COST: both renderers now run on the plugin's GPU thread (lake 45 fps, town ~23 fps; the plugin renders at 2x
  resolution scale and so does the transplant). NEXT (the endgame): plugin rendering OFF - the plugin keeps only its
  PM4 parser + the bridge, the native backend becomes the GPU (its readbacks then enabled: it must land resolves /
  memexport in guest memory as the plugin does today).
- T3 OFFLOAD (b8bde51, rexglue-src 17309f4, plugin pair D:\fable2_flash\sdk_legs\bin_offload): gpu_offload_to_native
  (plugin cvar, default off) makes the plugin skip its own IssueDraw / IssueCopy / IssueSwap - it keeps the PM4
  parser and the bridge. The native backend is then the ONLY GPU and owns guest memory: readback cvars take the
  plugin's values, the runtime data-provider / physical-heap API is bound at run time (fork runtime only; the exe
  still starts with the older pair). The plugin's window stays BLACK - expected (user noticed it while watching).
  Measured: lake OFF1 59.8-60.0 fps, native vs plugin-capture 0.0046; town OFF3 33 fps, 0.0029; 0 draw failures.
- WINDOW THREAD (user report: clicking the native window -> Not Responding): the window is now created by and pumped on
  its own thread; resize is a flag the render thread applies. Probe over OFF3: 88/88 WM_NULL answered, 0 hung.
- LAUNCHER: tools\run_native.cmd --backend (stages bin_offload, sets ngpu_backend + gpu_offload_to_native).
- OPEN: frame-exact front-end check (the burst captures are ms apart); coverage beyond lake / town / front end
  (combat, cutscenes, videos, other regions); long-run stability; readback-dependent features in offload mode.
- MOVEMENT (2026-09-26 afternoon): tools/native_gpu/ab_untile_leg.sh takes PAD_ROUTE (a pad-script route played instead
  of the stand's wait; job-tmp walk_series.sh wraps it with captures). NB: a 'release' line CLEARS the pad queue when
  PARSED - a route must not end with it (WALKL3 queued 12, executed none). WALKL1's movement was the USER'S input.
  Results (Bower Lake route: walk, camera sweeps, swim, forest, a bandit fight):
    lockstep WALKL5 (0.5 s bursts): best-match lag 0 on 42/49 pairs (+-1-2 on 6, one outlier) - native and plugin in
      step; same-pair differences (median 0.08) track motion (r 0.48) because grab_windows captures the two windows
      SEQUENTIALLY (GDI + a WGC session each, a few hundred ms apart) - moving comparisons need an in-process,
      frame-exact capture (not built yet). No missing / late geometry in any pair.
    offload WALKO1: 0 crashes, 0 of 20.8M draws failed, game 55-60 fps while walking (one 5 s window 48.6), every
      native frame complete (swim, landmarks, forest, combat).
  Present-at-swap (06f32f8) removed the frames-behind lag the user saw (WALKL1).
    offload WALKT1 (Bowerstone Market, same route): 0 crashes, 0 of 27.8M draws failed, game 32.7-36.4 fps, every
      native frame complete (docks, water, market, NPCs, arch district).
  NOT YET: a region transition while walking (loading screen between regions), cutscenes, videos, a frame-exact
  moving comparison; a plugin-alone fps baseline for the town (the optimisation phase needs it).
- PERFORMANCE BASELINE (plugin alone, no native: ngpu_shadow=false, bin_offload pair, offload off): town BASET1
  42.4-43.2 fps, lake BASEL1 60.0. Native backend as the only GPU: town ~33 (OFF3 standing, WALKT1 walking 33-36),
  lake 60. The same rendering code runs ~22% slower natively in town - the overhead is the plumbing around it (per-draw
  register diff + WriteRegister, bridge hooks, native present). First target of the optimisation phase (user roadmap:
  parity first, then "amazing parity in visuals and performance").

## 2026-09-26 afternoon - OPTIMISATION (user: "Not only close the town gap but surpass it ... 60+ lock no dips"; then "fully autonomous, select the recommended option")

- TARGET: town (Bowerstone Market, save card 2) 60+ fps LOCKED in offload (--backend). Start: native 31-33 fps.
- METHOD: tmp prof_town.sh TAG [EXTRA] (the --backend tune, instrumentation off, xperf 20 s at +25 s; NOPROF=1 =
  fps only), sym.sh, threads.py (busy % per thread), prof_multi.py (excl/incl per thread, one pass), callers2.py
  (attribution of module-only leaves), abfps.py (steady-state windows: mean/median/spread, p50 ms). Compare arms
  INTERLEAVED A/B/A/B only (peer review: a third party's GPU load came and went - 96%/599 W during PROFT1..OPT2,
  0-1% from BASET2 on; check nvidia-smi before each leg).
- PROFT1: three threads at 99%: the guest render thread (61% spinning in sub_82BA1FA8 = waiting for the GPU), a
  guest thread in ntdll, and the plugin GPU thread (97%) - the GPU thread is the limiter. On it:
  BackendLockstepSync 22% (scalar diff of 3,368 registers x 5,200 draws/frame), plugin D3D12 WriteRegister 13.9%
  (binding/texture invalidation for a backend that draws nothing), backend IssueDraw 40%, swap/submit 10%.
- FIXES (committed): SSE2 block diff (-> 12.4%, the constant blocks change on most draws), then the plugin's DIRTY
  BITMAP (rexglue-src 341becb RexNgpuDirtyRegs; native a44eedd) with a self-check against the full diff and a
  full-scan fallback on any miss; offload skips the plugin's invalidation work; unknown-register lookup only with
  GPU debug logging. OPT2 41-43 fps (from 31-33). BASET2 (plugin alone, same plugin binary, same hour) 42.7 mean but
  spread 11.4 -> parity NOT resolved by that pair.
- CHECK1 (correctness, timing-invalid, ngpu_backend_selfcheck_every=1, plugin with the type-0 range path 3044156):
  30,892,033 draws, 0 mismatches. REGISTER COVERAGE (whole run, dirty bits outside 0x2000-0x23FF/0x4000-0x4927):
  17 registers, all CP/sync/display (045E, 0578-057D, 05C8, 0A2F-0A31 COHER, 0D00, 0F01, 1841/1844 D1GRPH, 1930
  DC_LUT - carried by the gamma export - 5000-5002); no draw state is dropped. CENSUS under --backend (f7d7b28):
  register 1164/1164 REPLACED, texture 64, vertex 16, colour RT 12, depth RT 2, endian 4, primitive 20 REPLACED -
  by construction (the vendored backend IS the plugin's code; the plugin copy returns early under offload); PM4
  opcodes and shader ISA stay DUPLICATED (the plugin still parses PM4; the shader census keeps its own ledger).
- PROFT3 (after the bitmap) GPU thread: IssueDraw 52%, plugin type-0 register packets ~13%, submission (deferred
  list Execute + driver) 12.4%, readback landings memcpy 5.7% (13,400 landings / 5 s, ~1.7 ms/frame),
  SharedMemory::RequestRanges 17% (vertex uploads + a mutex).
- RESULTS (interleaved pairs; ms beside fps because fps deltas do not compose):
    type-0 runs via WriteRegisterRangeFromRing (rexglue 3044156, bin_offload4): 42.3->46.9, 43.2->48.2 fps
      (p50 -2.6 / -2.45 ms).
    ASYNC SUBMIT (7187719, ngpu_backend_async_submit default on; queue-order sites drain it; the native frame waits
      for the swap's submission) + 1 MB+ landings over the copy pool: 48.5->54.4, 47.0->54.7 fps (p50 -2.1/-3.1 ms).
      Picture: moving lockstep 0.0034/0.0036 vs 0.0027 off (capture timing); PAUSED scene (pad 'start') PSA1/PSB1 on
      0.0030/0.0030 = PSA0/PSB0 off 0.0030/0.0030, 0.0% >0.1 -> no static rendering difference.
    Plugin marks only CHANGED registers (8309c00, bin_offload6; CHECK3 self-check every draw: 21,880,833 draws, 0
      mismatches) and lock-free all-valid / no-scaled-resolve checks + cheap texpack clock (20d1067,
      ngpu_backend_fast_valid): clean windows v5 54.75/53.46 -> v6 55.72/54.42 -> +fast 56.59/55.14 fps
      (~0.3 + ~0.25 ms; the 9% profile share converted to ~1.7% of frame time).
    emulated_dead MEASURED (eacc906 + 3cac8bd): plugin backend bodies under offload 0 runs vs 19,816,504 gated draws,
      3,611 swaps, 218,579,803 register hooks -> REPLACED.
- MEASURING UNDER A THIRD-PARTY LOAD: Ollama llama-server instances load the GPU on and off (not ours, untouched).
  Every leg samples `nvidia-smi pmon -o T` (TAG.pmon.txt); abclean.py keeps only 5 s windows with no other process
  on the GPU. Contaminated windows run ~45-49 fps where clean ones run 55-57.
- UPLOADS: town uploads ~6 GB / 5 s (~24 MB/frame; plugin alone the same per frame). CHURN1
  (shared_memory_upload_churn): 86% of the volume is bytes the GPU already had. shared_memory_upload_skip_unchanged
  with its byte-wise FNV hash: uploads halved (3.4 GB) but 30 fps (hashing > copying). Switched to XXH3 per page
  (native shared_memory.cpp) - A/B in flight.

## 2026-09-26 afternoon - REVEAL HOLD (user: "the game loads partially polygons then shows the full loaded screen within less than a second, can we hide the partial load")

- CAUSE: draws skipped while their pipeline compiles (async_shader_compilation -> draw_census_.pipeline_not_ready)
  plus the stage's first frames. Recorded with tmp reveal_leg.sh / reveal_rec.py (WGC: every PRESENTED frame of the
  window, from "is loading" for 40 s) and reveal_an.py (distance of each frame to the settled one).
- FIX: after the scene goes loading -> world (patch_hooks SetScene counts it; fable2::WorldEntriesFromLoading), the
  window keeps its last frame (the game's fade) until: no skipped draw, nothing compiling, draw count within 3% for 6
  swaps; cap 2.5 s. Native: ngpu_reveal_hold / _frames / _max_ms (84fad7d). Plugin: reveal_hold / reveal_hold_frames
  / reveal_hold_max_ms, triggered by the app through RexNgpuRevealAfterLoad (rexglue 8b4a025, app e27678f).
- RESULT (Bowerstone Market load): native RV0 off = fade -> fog+specks -> arch with holes -> stage; RV1 on = fade ->
  finished stage (0.033), held 14 swaps / 1,062 ms. Plugin RP0 off = two partial frames (0.194, 0.051); RP1 on =
  finished stage first (0.027), held 11 frames / 859 ms.
- ALSO: ngpu_backend_upload_skip default ON (XXH3 page hash): D4 59.78 fps spread 0.6 vs D0 57.95 spread 4.5; town now
  59.6-60.0 in RV legs. GPU-cost diagnostics (gpucost.py, pmon sm% x frame time): scale 1x barely changes GPU time
  (16.08 vs 16.28 ms) -> the GPU cost is copies/dispatch overhead, not pixels; readback none -> 14.12 ms and 60 locked
  (diagnostic only - "some" is the hero/dog black-texture fix, a user setting).

## 2026-09-26 late afternoon - GPU COST (user: "minimize system requirements ... while maintaining performance")

- INSTRUMENTS: GPU TIME (timestamp pair per backend submission, logged every 5 s), GPU PROF (ngpu_gpu_prof: a
  timestamp at every category change - inflates GPU time ~50%, use for SHARES only), BARRIERS / OTHER TRANSITIONS
  census, HOIST count. Clocks during a town leg: 3,030 MHz, ~260 W, 95-98% util on the RTX 5090 - the GPU really was
  busy for ~16 ms a frame, and render scale 1x did not change it (G1) -> overhead, not pixels.
- CAUSE: the 512 MB shared-memory buffer changed state copy-dest <-> shader-resource ~1,000 times a frame (one per
  upload between draws), each a GPU drain (BAR1).
- FIX: UPLOAD HOISTING (fb3fb71; default ON after the HA A/B): uploads into pages no GPU work of the open submission
  touched go into a prologue list, executed first under one transition pair. Touched = RequestRanges reads,
  RangeWrittenByGpu, resolve mirror copies (SharedMemory::TouchRange; reset per submission). HO1: GPU TIME 16.03 ->
  11.93 ms/frame; copy-dest transitions 1,002 -> 116. Picture: paused town PH1/PH2 = PH0/PH3 0.0030, lake LH1 = LH0
  0.0029, moving MH1 0.0028 vs MH0 0.0030; walking offload WALKH1 60 fps, 0 draw failures.
- NOTE for readers of logs: 'shared_memory_upload_skip_unchanged = false' is the PLUGIN registry value; the native
  backend ORs in ngpu_backend_upload_skip (default true since 84fad7d).
- NOTE: PH*/PS*/LH* legs are PAUSED-scene picture legs - never count their hitches as play.

## 2026-09-26 evening - PARITY: NEW GAME intro, cutscenes, prologue (lockstep, native vs plugin windows)

- HOW TO REACH IT (read off the screen, NG4): title A -> main menu A (New Game) -> "maximum saves" dialog A = Continue
  WITHOUT SAVING (nothing written) -> boy/girl cards need a direction first (left) -> A. tmp newgame_leg.sh now uses
  that sequence; ngdiff.py scores each 3 s capture (native = the narrower window).
- NG4 (200 captures, 10 min): menus while stepping (high, transitions), intro cutscene s030-s049 median 0.085/0.032 =
  a moving camera captured ms apart (side by side: same content and lighting, NPCs displaced -
  D:\fable2_flash\gameplay\ng4_pair.png), prologue gameplay s060-s199 steady 0.0104-0.0111 (snow particles are
  random - that is the floor there). Cutscenes run at the game's 30 fps.
- NG1-NG3 stalled at the character cards (A alone does not pick a card): FRONT END ONLY - never read as play.
- These are LOCKSTEP legs (gpu_offload_to_native=false): picture-valid, NOT timing for the shipping configuration.
- INCIDENT: editing newgame_leg.sh while NG4 ran broke its cleanup (sh resumes at old offsets): the game, the staged
  pair and the saves were restored by hand (PID 118000 closed, pair 2e4ec3f16be3, saves 52/52 verified).

## 2026-09-26 night - CLOSE-OUT: ngpu_backend.dll, the NG2 kit, clear_memory_page_state, final state

- ngpu_backend.dll (012fcf3): the transplanted backend + lockstep glue as a GAME-AGNOSTIC module, C API
  src/ngpu_backend_dll/ngpu_backend_api.h (ABI 1): self-contained (own device/queue/window, registers the plugin
  callbacks) or manual (host forwards OnDraw/OnSwap, presents GuestOutput); reveal hold (host calls
  NgpuBackendRevealAfterLoad); PresentMode; Get/SetSetting over all 104 vendored accessor settings (table generated by
  tools/native_gpu/gen_setting_table.py). Fable II proves it with ngpu_backend_dll=true (tools\run_native.cmd --dll):
  town 60 fps, reveal 12 frames / 1,172 ms, 0 self-check misses, picture vs plugin 0.0032-0.0035 (motion only),
  interleaved vs the in-exe backend GPU ~12 ms both, CPU 13.4-14.1 (DLL) vs 13.8-15.1 ms.
- PUBLISHED VALUES: under offload the backend's own REXCVAR_SET writes (guest_fps_x10, gpu_frame_draws, texture
  pack/warm progress, ng2_uw_mode, ng2_fov_k) land in its accessor copies, not the registry - copied into the registry
  at each swap now (both paths). Before this, the app's HUD fps and texture-warming progress went stale under offload.
- clear_memory_page_state=false in fable2_tuning.h: the released build ran it off (CHANGELOG), the engine fork defaults
  it on. CM A/B: CPU 13.2-14.5 vs 15.7-16.3 ms/frame, uploads 1.3 vs ~7 GB per 5 s.
- run_native.cmd: --backend uses D:\fable2_flash\sdk_legs\bin_native_final (rexglue-src 8b4a025; sha256
  2e2a70d0.../0dd6d320...); --dll runs through ngpu_backend.dll.
- NG2 HANDOFF KIT: C:\users\renoi\claudecode\NATIVE_GPU_MIGRATION_KIT (MIGRATION_GUIDE.md, engine patches 0001-0015,
  vendored backend + scripts, app glue extract, the DLL + header, tools, histories). NG2 re-bases its engine on
  rexglue-src 8b4a025: DO NOT rewrite (amend/rebase/squash) any commit from e9834c4 onward in rexglue-src, or this branch.
- PARITY SWEEP, final: front end (TITLEBE2 frame-exact), new-game intro video + cutscene + prologue (NG4), lake,
  town standing/walking/paused, loads with the reveal hold (native + plugin recordings), offload n=3 (0.0028-0.0033
  vs a 0.0010 floor). Region transitions are covered as LOADS (market, lake, prologue); an on-foot boundary crossing
  was attempted (WALKR1: the hero stopped at a bridge railing) and NOT achieved - untested.
- KNOWN, NOT EXERCISED (peer review): CommandProcessor::InitializeRingBuffer resets only the read pointer (NG2 resets
  both - their attract-demo freeze). Fable II re-inits the ring only in its first ~420 ms (WALKH1, D0, D4, NG4: 3 inits
  each, none after rendering began), so the trigger does not occur in this title. RunSafeFns is declared but never
  defined; its only off-thread caller (ResolveDataProvider) would stall 3 s and log - the log line never appears.
- FINAL NUMBERS (town, native offload): 59.6-60.0 fps (the game's own cap), GPU ~11.7-12 ms/frame, plugin GPU thread
  ~13.2-14.5 ms CPU/frame with clear_memory_page_state off; start of the day 31-33 fps.

## 2026-09-26 night - ADDENDUM: the same layer on Ninja Gaiden II (for the next Fable optimisation pass)

- NG2 runs this backend (ngpu_backend.dll cc620728, lockstep and offload) on Chapter 1: CPU ~4.8 ms at ~2,700
  draws (1.78 us/draw), GPU ~1.05 ms/frame. Fable town: CPU ~14.0 ms at ~3,400 draws (4.12 us/draw), GPU ~11.8 ms.
  Same code, so Fable's remaining cost is TITLE-SPECIFIC, not the layer's. Candidates, with the instruments that
  measure them (TEXTURES / BARRIERS / GPU PROF / CPU COST lines): Fable renders at draw_resolution_scale 2x; reloads
  ~82 textures / 36.8 MB per frame, 60 of them scaled-resolve targets after the GPU wrote them; runs
  readback_resolve=some (the hero/dog black-texture fix - a user setting, keep) with ~17k landings per 5 s; ~900
  barrier batches per frame remain. Run the same lines on NG2's Chapter 1 and diff the two before changing anything.
- clear_memory_page_state (CM pairs, pmon-screened): CPU -2 ms replicated in two rounds; the stutter reduction first
  read from round 1 did NOT survive screening (round 1 CM11 11/15 windows under third-party GPU load; round 2
  29 vs 34 hitches, both contaminated) - no dip claim is made from today's runs.

## 2026-09-26 evening - THE "NO DIPS" MEASUREMENT (first clean one): native offload vs plugin alone, town walk

- DESIGN: same build, interleaved DN1/DP1/DN2/DP2, 170 s walking route each (PAD_ROUTE through prof_town.sh), pair
  bin_native_final, clear_memory_page_state=false (title tuning, both arms), pmon through every leg; scored with tmp
  dipbands.py on SCREENED windows only (no other process on the GPU), per-window worst frame in bands.
- RESULT (pooled): native (DN) 39 screened windows: >20 ms 31, >25 26, >30 20, >40 3, worst 54.0, hitches 24.
  plugin alone (DP) 25 screened windows: >20 19, >25 7, >30 2, >40 1, worst 41.6, hitches 2.
  Mean fps equal: DN 59.5-59.6, DP 59.3-59.4, p50 16.6 ms both.
- READING: with the page-state refresh off, the PLUGIN ALONE also runs the town walk at the 60 cap (it ran 42-45 with
  it on). Native offload matches the picture and the mean frame rate but has MORE single-frame spikes of 25-33 ms
  (double frames on a CPU thread at ~84% of budget). The "no dips" half of the goal is NOT met by the native path
  today; the plugin alone currently meets it better. Next pass: CPU per draw is 4.1 us on Fable vs 1.8 us on NG2
  (same code) - the texture-reload volume (~82 loads / 36.8 MB per frame, 60 of them resolve targets) is the lead.
- CCR1 (03:15, ngpu_clear_color_from_reg on the OLD native path) is superseded: the transplanted backend performs
  resolve clears with the plugin's own code. Closed.
- CORRECTION (peer review, same evening): "the plugin alone also runs the town walk at the 60 cap" is true of the MEAN
  only. On the "no dips" axis the plugin reference FAILS TOO: worst frame 41.6 ms (2.5x the 16.67 ms budget), 7 of 25
  screened windows over 25 ms. So closing the native-to-plugin gap would still not deliver "60+ locked, no dips" - the
  plugin is not the target. Also: p99 is 33.7/38.6 (native) vs 36.2/38.5 (plugin), fully overlapping - the whole
  native penalty lives past the 99th percentile (worst frame, hitch count); a p99 reading shows no difference.
- WHAT BLOWS THE SPIKES - OPEN, and one clock to NOT use: the log's [perf] line is fable2::PerfFrameTick, ticked once per
  UI PAINT of the runtime presenter (144 Hz here - its 6.9 ms p50 is 1/144 s); under offload that window is the
  plugin's black one and never waits for a guest frame. It says nothing about the renderer. The renderer's work runs
  on the PLUGIN GPU THREAD (CPU COST line: ~13.5-14.5 ms/frame), which the guest waits on through its fences. A peer
  pairing [perf] against guest [swap] concluded "not renderer-bound"; that inference does not hold on that clock.
  DECISIVE NEXT TEST: a PER-FRAME record of GPU-thread time (not the 5 s mean) aligned to the guest's hitch
  timestamps, in both arms. CANDIDATE from the same review, still valid: synchronous resolve readbacks stall the GUEST
  directly and cluster on the heavy frames (8 on region-load frames, 0-2 elsewhere) - count them per hitch frame.
- [WITHDRAWN 19:20 BY ITS SENDER - kept as a tombstone so it cannot be re-promoted: the NG2 index line was present in
  every leg; the "never indexes" reading came from a tail -8 window filled by the left-alone counter, and the bisect
  inherited the same window. An all-on control on NG2 indexed 24,670 files with hoist live; trace tables: DLL 752 vs
  plugin 745 distinct replacements. NOTHING IS BROKEN IN THE HOIST PATH. The "0 enhanced" was the counter below.]
  ORIGINAL ENTRY: OPEN AGAINST UPLOAD HOISTING (from NG2, 2026-09-26 night): on Ninja Gaiden II with ngpu_backend_hoist_uploads ON the
  texture pack never indexes (no "[texpack] ... hashed files indexed", 0 enhanced textures; bisect: hoist off alone ->
  indexes). NOT reproduced on Fable II: every hoist-on leg indexes 55,848 files (in-exe HA11/HA12, DLL DLL1/DLLP1/DLLP2/
  DLL3/DL1/DL2/CM01, and with clear_memory_page_state on: CM11/CM12), and paused lockstep native vs plugin (pack on both
  sides) reads 0.0030. So the defect sits in a path Fable does not exercise; NG2 pins hoist off meanwhile. Suspect:
  the pack index is built lazily on the first ApplyTexpackResolve that passes AnyPageGpuWritten - check those first
  loads on NG2's title screen.

- TEXTURE-PACK COUNTER (found by NG2's trace, fixed here): texture_pack_replaced was set only in the superseding-
  replacement branch, which texture_pack_resolve_at_load (the default) skips - so Fable's settings menu
  (fable2_menu.cpp:596) and texture notice (fable2_texnotify.cpp:122) read 0 "in the pack" while the pack worked, on the
  plugin path too. Now set in ApplyTexpackResolve's resolve-at-load block: rexglue-src (plugin, pair
  D:\fable2_flash\sdk_legs\bin_native_final2) and the vendored copy (exe + ngpu_backend.dll). PublishStatCvars carries it
  to the registry under offload. NOTE: the released v1.0.1 plugin likely has the same hole (not checked in its source).
- RELEASED v1.0.1 HAS THE PACK-COUNTER BUG (confirmed): tonight's legs show 55,848 files indexed and >=1,000
  "upscaled textures resolved at load" while texture_pack_replaced reads 0; the shipped rexgpu-xenos.dll
  (fable2recomp/out/build/win-amd64-Release, 2026-09-24 10:00) contains texture_pack_resolve_at_load and none of the
  bridge exports - it was built from rexglue-src branch hotfix-0.2.13-plugin, tip c4ff4bc (09-24 09:26, the attract-
  video seam fix). The fix febe384 CHERRY-PICKS CLEANLY onto c4ff4bc (tested in a throwaway worktree: rc 0, one file,
  src/graphics/d3d12/texture_cache.cpp; worktree removed) and carries no native-GPU work. Shipping it = the user's
  push/release decision (Fable is public; the release gate applies). Not done.
- RELEASE-LINE EVIDENCE, tightened (peer, same night): the shipped DLL LACKS strings unique to the rival branches
  (ng2-144fps "[gpu] fence waits #"; ng2-native-gpu "[ngpu-draw]    VS=" / "IB guest=0x") and HAS
  texture_pack_resolve_at_load; c4ff4bc (a pure logic change, no strings) is the attract-video seam fix recorded as shipped
  in v1.0.1. So: built on hotfix-0.2.13-plugin at or after c4ff4bc - NOT provably from c4ff4bc exactly (an unrecorded tree
  matching all four strings is not excluded). The cherry-pick target is that branch either way.
- RULE (two incidents today, one class): never trust a tool to pass content through unchanged. A tail -8 window hid a
  log line and produced a false "never indexes"; backslash paths written through heredocs / Python strings became
  formfeed and backspace in this file. Read the WHOLE record (grep -c, not tail); write backslash text with a file
  editor, then check it (grep for the path, count control characters).

## 2026-09-26 night - FRAME TRACE: who owns the native path's long frames (the decisive test named above)

- INSTRUMENT: cvar ngpu_backend_frame_trace (default off). It writes one 48-byte record per backend swap, on the plugin GPU
  thread, to ngpu_frames.bin: QPC, rdtsc, QueryThreadCycleTime (busy), cumulative time inside the backend callbacks
  (draws + sync + swap), cumulative NoteFenceWait microseconds, draws, and synchronous resolve readbacks (the 3
  g_frame_sync_readbacks sites). Flushed in blocks of 256; failed writes are counted as DROPPED. Analyzer: job tmp framea.py.
- LEGS: FT1/FT2, native offload, pair bin_native_final2, the same 170 s town walking route as DN/DP. 11,776 | 11,520 records (file size, header 32 bytes),
  0 dropped. SANITY: median interval 16.59/16.58 ms (the game's 60, not the 144 Hz paint clock); median busy
  12.6/13.3 ms (matches the CPU COST line).
- GAMEPLAY FRAMES ONLY (draws > 3000; region-load frames of 227-743 ms excluded), FT1 | FT2:
    normal (<=20 ms)          n=10135 | 10076  busy 12.8 | 13.3  backend 10.1 | 10.6  fence 0.00 | 0.00  sync readbacks ~0
    long WITH a sync readback n=   70 |    93  interval 29.7 | 32.2  busy 14.9 | 16.8  backend 27.1 | 29.2  fence 12.9 | 13.5 ms
    long, NO sync readback    n=   39 |    64  interval 29.6 | 27.6  busy 19.4 | 19.6  backend 26.3 | 24.3  fence 1.7 | 1.6 ms
  Of the gameplay frames that did any sync readback (FT1 78, FT2 93): >25 ms 70 | 93, 20-25 ms 6 | 0, <=20 ms 2 | 0. So
  76 of 78 and 93 of 93 ran over 20 ms. The 20-25 ms band is a THIRD population that my two thresholds left unnamed
  (75 gameplay frames in FT1, 201 in FT2); the peer found it because the counts did not add up. Outside gameplay,
  3 | 3 readback frames had <=3000 draws and 3 | 2 were region loads of >=200 ms.
- READING - two classes:
  (A) ~60% of long frames: a SYNCHRONOUS RESOLVE READBACK. The GPU thread is not CPU-busy (15-17 ms, near normal); it
      sits ~13 ms in the fence wait for the GPU to finish the resolve so the guest-memory copy can be read back. This
      confirms the peer's candidate. The readbacks come from readback_resolve "some", the hero/dog black-texture fix.
      That is a user-facing correctness setting and has NOT been changed. The fix candidates (async readback with a
      one-frame lag, or narrowing which resolves are read back) need the user's go-ahead because they touch that fix.
  (B) ~40% of long frames: CPU-heavier frames (busy 19.5 vs ~13 ms: +6.5 ms, a DIFFERENCE, so it holds
      whatever busy spans; draws +500-750). The owner is not identified yet; the texture-reload volume remains the lead.
      No "uncounted wait" figure is claimed. My earlier "6-8 ms" was backend minus busy, and those two counters are not
      nested (next bullet).
- COUNTER SPANS (peer review): busy = QueryThreadCycleTime of the whole plugin GPU thread from swap to swap. That
  includes the PM4 parse and everything outside the backend. backend = QPC wall time inside the backend callbacks only
  (BackendLockstepDraw in OnBridgeDraw, and Sync+Swap in OnBridgeSwap). fence = NoteFenceWait microseconds, which fall
  inside backend. Busy is NOT inside backend. On normal frames busy exceeds backend by 2.7 ms in both legs, which is
  the parse outside the backend. So busy + fence = backend is not a valid model, and class A matching it to 0.7-1.1 ms
  is coincidence, not confirmation. Class A rests on the fence counter alone: 0 | 1.6-1.7 | 12.9-13.5 ms for normal,
  B, A.
- CORRECTION to my own earlier reading: "fence-wait median 0, so the waits are uncounted" was wrong. The all-frame
  median is 0 because normal frames never wait; the class-A frames carry 13 ms of COUNTED fence wait each.
- CORRECTIONS (peer re-parse): records are 48 bytes, not 56. The legs hold 11,776 | 11,520 records, not 8,192.
  The peer reproduced every class count exactly.
- THE WORST FRAMES (>35 ms) ARE A THIRD CLASS, C = the WORLD-ENTRY SETTLE. The peer's threshold sweep showed FT1's
  readback share falling to 0 of 6 above 40 ms while FT2's held. Located by frame index: the only >=200 ms frames in
  both legs are the load into town (FT1 frames 1058-1101, FT2 1042-1084: 10 s load, then 743/227/300 ms first world
  frames). Frames over 35 ms that fall within 300 frames (~5 s) after them: FT1 9, FT2 11. NONE of them did a readback;
  fence 0; busy 19-32 ms; backend 32-68 ms (worst 70.8 ms, FT2). Frames over 35 ms elsewhere in the walk: FT1 4 (3 with a
  readback), FT2 28 (28 with a readback). So 31 of the 32 worst frames of steady walking are class A, and the legs agree
  once the settle is separated. FT1's worst six are all settle frames, which is why its readback share vanished.
  The 25-35 ms band is class A outside the settle as well: 67 of 87 | 65 of 110.
  READING: class C is CPU work (busy up to 32 ms, no fence wait) in the ~5 s after the world first shows. That is past
  the reveal hold (steady draw count for 6 frames, 2.5 s cap): the hold releases while the renderer is still
  settling. Candidates are pipeline creation and texture/resolve uploads of the newly entered area; not yet measured.
  Fix candidates: extend the hold's readiness test (e.g. require busy back under the budget), or pre-warm. Both are
  inside the reveal-hold design the user already approved, so they need no new decision; measure first.
- WHAT THE PLAYER SEES OF CLASS C. The reveal released after 21 | 16 held swaps (log "REVEAL: showing the stage").
  Frame offsets are counted from the first gameplay frame, which approximates the hold's start to within a few frames.
  Settle frames over 35 ms: FT1 at 4, 5, 9, 15, 19 | 31 | 201, 202, 205; FT2 at 4, 5, 7, 11, 16 | 19, 22, 30, 31 |
  227, 228. The first group is HIDDEN by the hold, including the 55.8 and 70.8 ms worst frames. The second group is
  VISIBLE, 1 | 4 frames of 36-47 ms in the half-second after the reveal. So the hold releases about 10-15 frames early.
  The third group, ~3.5 s in, recurs in both legs at the same route position, so it is a route event, not settle.
  NEXT (no decision needed): make the hold also wait for N frames under budget. Unmeasured: what the 201/227 event is.
- DONE - REVEAL PACE TEST (exe cvar ngpu_reveal_hold_pace_ms, default 20; the DLL has the same code with a fixed 20).
  The hold's N=6 complete swaps must now also be ON PACE: the last 6 swap intervals are all <=20 ms (ABSOLUTE), OR the
  slowest is within 1.25x the fastest (RELATIVE). The relative term exists so a slower PC is judged by its own steady
  rate and cannot degrade into a fixed cap-length hold (peer review). It is a window, not "the fastest frame so far",
  because early in the hold the fastest frame is itself a settle frame. The log names the releasing term and keeps
  run totals ("N complete, M at the CAP").
  Peer check against FT1/FT2 (6-frame windows, settle = first 60 frames vs steady walking): absolute passes 29|38% of
  settle windows vs 94|88% of steady ones; relative 27|11% vs 75|70%. Steady window max/min ratio: median 1.193, p90
  1.343, so 1.25 is tight but on the correct side.
  RESULT, legs RP1/RP2 (same route, pair, frame trace): released after 45 | 72 swaps / 1594 | 2063 ms (was 21 | 16 /
  1047 | 1000 ms), both "pace absolute", 0 at the cap. VISIBLE settle frames >35 ms in the first 100 frames: 0 | 0
  (FT1/FT2: 1 | 5). The route event at ~3.5 s remains (RP1 frame 215 at 62.4 ms, RP2 222 at 41.1 ms; readback-free).
  The cost is 0.5-1.1 s more of the held loading frame. 2063 ms is 83% of the 2500 cap. If the cap starts firing,
  that falls back to today's behaviour (benign). RAISING THE CAP makes loading screens longer, which is a USER decision,
  not a tuning step.
  DLL PATH: HOLD BEHAVIOUR confirmed (leg DR1, ngpu_backend.dll): 44 held frames / 1594 ms, "pace absolute", 0 at the
  cap. PACING on the DLL path is UNMEASURED: the DLL records no frame trace, so DR1 says nothing about class A/B there.
  Partial evidence of parity: RP1 in-exe released at 45 frames / 1594 ms, DR1 at 44 / 1594 ms, on the same route and
  pair. The release needs 6 consecutive intervals on pace, so the settle's frame times were closely alike on both paths. The kit's
  bin/ngpu_backend_dll is updated (the previous DLL is kept in previous_2026-09-26_1930).
  Peer re-parse of RP1/RP2: the escaped FT settle frames are now inside the hold. Steady walking is unchanged (class A
  85.4|72.8% of >25 ms).
  CLASS-B NOISE FLOOR (peer): steady no-readback >25 ms frames per 1000 steady frames: FT1 2.10, FT2 4.44, RP1 1.44,
  RP2 2.99. That is 2.1x between IDENTICAL legs, as large as any between-condition difference. Any class-B A/B needs
  the floor measured from same-config pairs first, and legs sized to the expected effect.
- FRAME TRACE v2 (header version 2, 80-byte records; analyzer job tmp fta2.py; v1 files still read with framea.py).
  Added: cumulative pipeline-state creations and their wall time (CreateGraphicsPipelineState, any thread), texture
  loads and bytes, and THIS swap's reveal verdict (1 holding, 2 complete, 4 pace window passed, 8 released). The
  recorder now runs after RevealHold so the flags belong to the same swap.
- LEG V1 (same route/pair, v2 trace): released after 49 swaps / 1828 ms, "pace absolute", 0 caps.
  THE GATE'S TWO HALVES: over the 49 held swaps, completeness passed 34, pace 5, both 5. Completeness alone formed runs
  of 5, 5, 8 and 4 between frames of 26-58 ms; under the old rule (6 complete) it would have released at ~offset 26,
  inside the settle. Pace passed only at offsets 44-48. So in this leg the PACE half does the discriminating.
  Completeness runs were broken mostly by 1-4 late pipeline creations (0.1-0.3 ms each).
  THE ROUTE EVENT (offsets 197-201, 32-53 ms, same place as every leg). NO pipeline creation (0 in each of those
  frames) and no readback (fence 0). Texture loads are ordinary (81-106 per frame, ~34-35 MB, same as its
  neighbours). Busy is 23-26 ms vs ~12-13 normal. So the cost is ~10+ ms of GPU-thread CPU work that is neither PSO
  creation nor texture-load volume. PSO creation is RULED OUT; the cause is still unknown. Also seen: texture loads run
  ~85-90 per frame through the whole first ~300 frames vs a steady median of 55 - the area streams in for ~5 s.
  NEXT for the event (cheap, not urgent - one visible frame per leg): the peer's round-trip route (place vs first
  time), or a CPU sample (xperf) timed to offset ~200.
- CLASS B HAS AN OWNER: TEXTURE RESOURCE CREATION BLOCKING IN THE KERNEL.
  (1) The peer's bound: busy (whole thread) contains CPU-inside-backend, so backend - busy is a LOWER BOUND on non-CPU
      time inside the backend (normal frames -2.4 = the parse outside it, the sanity check). Class A: +11.9 vs fence
      12.45, fully explained. Class B: busy +6.2 AND at least +6.4 ms non-CPU. The route event has the same shape.
  (2) WHERE IT BLOCKS - walking xperf leg XW1 (CSwitch stacks; analyzer job tmp waits2.py derives each wait from
      switch-out/switch-in, since xperf's WaitTime column is 0 in these dumps). Plugin GPU thread, 24.4 s, waits >=1 ms
      outside the parser's WAIT_REG_MEM: readback fence waits 13 = 148.8 ms, max 14.4 (the POSITIVE CONTROL: class A seen
      as it should be). WrResource in dxgkrnl/nvlddmkm under D3D12TextureCache::CreateTexture: 5 = 35.2 ms, max 9.1.
      Under ApplyTexpackResolve: 3 = 6.4 ms. Under the readback path's own resource work: 4 = 17.3 ms. dxgmms2 (video
      memory manager, kernel-only stacks): 9 = 21.1 ms, max 5.6. No preemption >= 1 ms.
  (3) PER FRAME - frame trace v3 (header 3, 96-byte records, analyzer fta3.py): every texture-cache
      CreateCommittedResource is counted and timed (textures, pack replacements, upload buffers). Legs V3A/V3B, same route.
      Steady walking, bands covering every frame:
        <=20 ms: 4.7 | 4.5% of frames create anything, mean 0.06 | 0.05 ms
        20-25  : 75 | 68%, mean 3.23 | 1.54 ms
        A      : 25 | 21%, mean 0.45 | 0.24 ms
        B      : 79 | 82%, mean 5.29 | 3.11 ms, max 11.5 | 9.0 ms
      Reverse direction: frames with >= 3 ms of creation, 78 of 94 | 19 of 22 ran over 20 ms.
      CLASS B SPLITS: backend >= 16 ms, 39 | 17 frames, 94 | 82% create, mean 6.37 | 3.11 ms. Backend < 16 ms (the renderer
      was normal and the interval was long elsewhere - the guest), 8 | 0 frames: a small separate population, not owned.
  READING: creating D3D12 texture resources on the GPU thread blocks it in the kernel's resource lock (WrResource). That
  owns most of class B and the 20-25 ms band; the timed creation is 3-6 ms of B's ~10 ms excess. The rest is the
  first-use work of those new textures (loads 68-74 vs 59 per frame) - not yet split. Fix directions (engine work,
  relayed to the user with the texture-reload go-ahead): a pool of recycled texture resources, placed resources in
  pre-allocated heaps (no per-texture kernel allocation), or creation ahead of use on a worker thread.
- USER DECISION (2026-09-26 22:10): GO on the texture work (item 4). Also: "no dips" = NORMAL PLAY ONLY; the reveal
  hold stays as it is (no cap change); try the readback change only if the black textures do not come back; ship
  febe384 (Fable v1.0.2).
- TEXTURE CHURN CENSUS (texture_cache_base.cpp + a TEXTURE CHURN line every 5 s): created / evicted by the budget /
  re-created after an eviction / cache MB. Leg CH1 (town walk): 0 EVICTED and 0 RE-CREATED in every window. The cache
  grew from 75 to 309 MB against Fable's limits of 4168 soft / 8264 hard MB (the title tuning already raised the
  SDK's 384/768 defaults). So every creation is a NEW texture. Recycling resources or raising the budget cannot help;
  the fix has to make creation itself cheap (placed resources in pre-allocated heaps) or move it off the frame.
- TEXTURE HEAPS (cvar ngpu_backend_texture_heaps, default OFF; the DLL returns false): guest textures PLACED in 256 MB
  default heaps (non-RT/DS), first-fit with coalescing, blocks reused only after their last submission completes, and
  the next heap created on a worker thread (log: "TEXTURE HEAPS: heap N adopted ... placed, fallbacks, waits").
  A/B: interleaved legs HPA3/HPB3/HPA4/HPB4 in a window reserved with claudecode-79, with a machine CPU sampler
  (typeperf 1 s; tmp cpuscreen.py). Other load was flat (1.85-1.99 cores median every leg). 499-506 textures placed,
  0 fallbacks.
  RESULT - NO WIN: mean creation time on class-B frames, heaps off 2.47 | 1.85 ms vs on 2.88 | 3.40 ms. Med-of-nonzero
  on 20-25 ms frames: off 1.64 | 1.70, on 1.64 | 1.14. Class B counts off 26 | 15, on 13 | 25 (inside the 2x floor).
  CreatePlacedResource still costs ~0.3-0.5 ms per texture in the driver, so the per-frame cost of 5-7 new textures
  stays. KEPT OFF, and kept in the code as a measured negative. (Earlier HPB2 "+3 ms busy": per-leg drift - HPA3/HPB4
  13.1 vs HPB3/HPA4 11.6 on normal frames, independent of the arm - plus claudecode-79's plugin build at 22:46.)
  NEXT: move creation OFF the GPU thread - spare resources of the common shapes, pre-created by a worker. First a
  shape census, to see whether new textures repeat a small set of shapes.
- SHAPE CENSUS (SC1-SC3, every texture-cache creation keyed by format/size/mips/dimension; log "SHAPE CENSUS" and a
  per-window "SHAPE POOL SIM"): at 1,000 creations 162|163|162 distinct shapes, 83.8|83.7|83.8% repeats, top 16 = 61%.
  Steady walking window (after the entry burst): worst frame 15 creations; a pool of spares per known shape refilled
  between frames hits 80.3% (depth 1), 88.7% (2), 90.6% (4), 91.4% (8) and leaves 7-8 misses in the worst frame (new
  shapes). Peer: bursts up to 39 creations in a frame (SC1 frame trace), creation ~0.44-0.70 ms each either way.
  NG2 (claudecode-79): a worker creating D3D resources CONCURRENTLY doubled the render thread's own creation cost;
  their creation hold fixed it - fill any pool only while the render thread is not creating (e.g. during the hold).
- USER QUESTION (2026-09-26 23:45): "If the cached textures get reloaded... display them from cache rather than
  re-generating them again when being streamed? Wasn't this the whole reason for caching?" and "When enhanced
  textures are not selected, can we pre-cache the game textures as well at level load?"
  WHAT THE PRE-CACHE DOES: plugin StageWarm reads the stage list's PACK files into the OS page cache at a stage change
  (Fable: "warmed stage 6: 1443 files, 2588 MB"; NG2 tonight stage 1 2,865 files). It never builds GPU textures, so
  every replacement is still built on the render thread (NG2: 413 in one frame = 391 ms). Relayed to NG2 as the user's
  direction; claudecode-79 is building a GPU-ready prebuild of the stage list during the load.
- CONTENT CENSUS (SC4, cvar ngpu_backend_tex_content_census; XXH3 of every load of CPU-written art, keyed with the
  texture key): 276,000 loads in the town walk, 26,431 distinct contents, 90.4% REPEATS of content already loaded
  (93.0% of the bytes) - and EVERY repeat was at the SAME address. So the waste is not streaming into reused memory:
  the SAME texture is re-decoded with byte-identical content, because a write watch fires on any CPU write to its
  pages even when its own bytes do not change.
- FIX: SKIP UNCHANGED (cvar ngpu_backend_tex_skip_unchanged, default off until measured; DLL false). In
  TextureCache::CommitPreparedTextureLoad: hash the requested parts (base/mips) of non-scaled, non-GPU-written
  textures; if every part equals what this texture was last loaded from, skip the decode + upload and just
  MakeUpToDateAndWatch. A cache clear (pack switch, dump start) makes new Texture objects with no hash; pack
  replacements deferred by the budget retry from g_texpack_pending, not from reloads. Log: "SKIP UNCHANGED this
  window". Frame trace v4 (120-byte records, fta4.py) adds pack-path time/calls and the longest single creation.
- SKIP UNCHANGED A/B (SKA1/SKB1/SKA2/SKB2, window reserved with claudecode-79, other CPU flat 2.03-2.14 cores): skips
  ~90% of hashed art loads (~7,200 per 5 s, ~85 MB / 5 s not decoded). Pack-path calls 56|53 -> 32|32 per frame (a
  skipped load never re-runs ApplyTexpackResolve). Class B 17|40 (off) -> 8|17 (on): halves in BOTH pairs, but inside the
  2.1x same-config floor, so it is a direction, not a proven size. Normal-frame busy 13.6|12.8 vs 12.4|12.9 (drift).
  PICTURE (lockstep native vs plugin, the plugin never skips): paused PS1/PS2 on 0.0030 = PS0/PS3 off 0.0030, 0.0%
  >0.1 (PS1 skipped 2,071 loads before the pause); moving MS1 on 0.0034 (85,013 of 90,982 loads skipped) vs MS0 off
  0.0031 - the moving spread seen before (0.0028-0.0036). -> DEFAULT ON in the exe; the DLL keeps it off until NG2
  measures it on its own route.
- NEW OWNER OF CLASS B (frame trace v4): the PACK path, not guest creation. On class-B frames ApplyTexpackResolve costs
  8.6-11.1 ms per frame (146-416 ms per leg) vs 41-96 ms for ALL texture-cache creations (pack creations included in
  both). It runs ~32-56 times per frame even on normal frames (0.1-0.2 ms total there): the expensive calls are the
  few whose content matches a pack file (file read, 4x resource + upload buffer, copy, mips). That is the user's
  "GPU-ready pre-cache" target: prebuild the stage list's replacements during the load/hold, keyed by id+hash
  (claudecode-79 did it for NG2: 1,836 of 3,106 stage-1 files in 4.4 s; 251 of 413 arrival replacements hit;
  arrival frame 286 -> 212 ms).
- LONGEST SINGLE CREATION (v4, exact per frame, not inferred): 13.5-15.5 ms single creations occur (SKA2, SKB1,
  SKB2): the peer's "cumulative artefact" alternative is ruled out; the kernel lock does sometimes hold ~15 ms.
  CLOSED CANDIDATE - do not re-raise the frame-boundary artefact.
- PEER RE-PARSE (claudecode-76, same four legs, class B = >25 ms no readback): pack path 11.3-13.9 ms per class-B frame
  vs creations 2.4-3.6 ms - the pack path is ~4x the creation cost and essentially the whole class-B excess. The
  earlier "class B is owned by texture creation" was a real mechanism but only ~a quarter of the cost.
  PER-CALL, NOT RATE: the same 32-56 pack calls cost 0.10-0.23 ms on a normal frame and 11-14 ms on a class-B frame -
  call count +12-18%, per-call cost 50-100x. A few calls are catastrophically expensive (the ones that match a file:
  read + two creations + upload + mips). Throttling the call rate cannot fix it; only making those calls cheap or moving
  them off the frame (the prebuild) can.
- FROZEN CVAR MIRRORS (found by a void A/B): every vendored FLAGS_*_storage_ is a STARTUP SNAPSHOT of the plugin
  registry. texture_pack_chapter read 0 forever on the native path: no stage change, no warming, no StageNote lists,
  no prebuild - the "warmed stage 6" lines came from the PLUGIN's own copy. PBA1/PBB1/PBA2/PBB2 (prebuild "on") were
  therefore VOID (no PREBUILD line in either on-leg). Fixed: xlat_support RefreshInt/Bool/String re-read
  texture_pack_chapter, texture_dump(_path), texture_pack_path, vsync every 250 ms. The pack-path cost figures above
  stand (the chapter feeds only the stage list / warming / prebuild; SK legs indexed 55,848 files and resolved >1,000).
  RULE: a feature that reads a value cached at startup cannot be measured until the value is shown to CHANGE live
  (here: the "PREBUILD stage 6" line and hits > 0).
- PACK PATH BY OUTCOME (OC1, per 5 s window; stages: ineligible / gpu-written / same-content / no-file / prebuilt-hit /
  header-deferred / create-failed / BUILT): the time is in BUILT - 5-13 ms per replacement built from its file during
  play. Everything else is cheap (thousands of ineligible / gpu-written calls cost ~0.3-2 ms per window).
- TEXPACK PREBUILD (cvar ngpu_backend_texpack_prebuild, off by default pending the long-frame count below). Budget = 30%
  of the DXGI local video-memory budget, clamped 512-4096 MB (here 4096 MB: all 1,386-1,388 stage-6 replacements,
  3.5 GB, GPU-ready in 3.6 s during the load, 0 left out; a fixed 1 GB left 394 out).
  A/B PBA5/PBB5/PBA6/PBB6 (other CPU flat 1.65-1.84 cores):
    pack cost on class-B frames  off 114.9 | 52.3 ms   on 0.1 | 0.1 ms
    class B (>25, no readback)   off 10 | 5            on 2 | 7   (the on-leg ones carry no pack cost)
    20-25 ms frames              off 55 | 51           on 15 | 18
    replacements BUILT mid-walk  off 401 | 467         on 4 | 19; prebuilt hits 622 | 797
    town load (menu->world)      off 19.2 s            on 14.3 | 14.3 s
  THE FREEZE (4 GB budget): ONE 545-596 ms frame mid-walk (a resolve-readback fence wait, "11 x 71.3 ms" /
  "9 x 27.3 ms") in 6 of 8 prebuild-on legs vs 0 of 9 off (peer Fisher P = 0.0023). REVERSED PAIRS (PBB9 then PBA9,
  PBB10 then PBA10) froze on the ON leg in both positions - by position 2/9 vs 4/8, P = 0.335 - so it follows the
  prebuild, not the leg order. Not VRAM exhaustion (27.5 of 32 GB free), not a stage change. (PBB2's 556 ms "void"
  freeze came right after a region load it walked into - not clean evidence either way.) Mechanism unexplained;
  best guess residency/eviction pressure from 3.5 GB of extra committed resources hitting the readback targets.
  FIX: budget 15% of the local video-memory budget, capped at 1.5 GB, built SMALLEST FIRST (1.5 GB = 85% of the stage's
  1,415 files; 1,187-1,191 built in ~2.9 s). Legs PBS1/PBS2/PBS3 (on) vs PBC1 (off; this batch ran with other CPU at
  ~3.4 cores, flat across all four): 0 of 3 froze (with the 1 GB legs OC1/PBB3: 0 of 5 at <= 1.5 GB). 20-25 ms frames
  29|64|28 vs 97; class B 11|10|6 vs 18; pack cost on class-B frames 4.8|7.8|2.8 vs 6.8 ms (smaller win than 4 GB:
  the ~230 largest files, the costliest builds, are out). PICTURE: paused PP1 0.0030, moving MP1 0.0033 (= PS1/MS1
  without prebuild), 1,191 prebuilt served. -> DEFAULT ON in the exe (the DLL keeps it off).
  NEXT for the rest: the largest files (out of budget) could be built async on the worker when first needed (NG2's
  async pack path) rather than in-frame.
  NOTE for trace readers: frames.bin has no arm label; the arm of a leg is in TAG.log ("Tuning override" lines).
  STATISTICAL BOUND (peer): 0 freezes in 5 legs bounds the rate only below 45% (95%); 10 clean legs -> <26%, 28 -> <10%.
  The pre-create legs (GPR0/GPA1/GPB1/GPB2/GPA2) all run with the prebuild at its new default and add to the count.
  RELEASE CAVEAT: the prebuild default reaches players only through a release, and the user approved "the texture work",
  not this default with a known half-second failure mode at 4 GB. It must be a NAMED line in the next release decision.
  MECHANISM (working hypothesis): several long resolve-readback fence waits inside one frame, scaling with the VOLUME of
  prebuilt resident resources (4 GB: 6/8; <= 1.5 GB: 0/5) - the readback targets competing for residency, not the
  building work (the worker is idle after the load). It predicts the same failure for any other large resident
  allocation added later. (PBB2's freeze does not bear on it: the worker never started in that void leg.)
  COUNT UPDATE: the 7 pre-create legs below all ran the 1.5 GB prebuild (the new default): 0 freezes -> 0 of 12 at
  <= 1.5 GB (95% bound ~22%). Keep counting in every leg that runs the default.
- GAME TEXTURE PRE-CREATE (user 2026-09-27: "When enhanced textures are not selected, can we pre-cache the game
  textures as well at level load"; cvar ngpu_backend_game_tex_precreate, DEFAULT OFF). Per stage, the shapes the game
  creates are recorded to %LOCALAPPDATA%/fable2/shapes/stageNN.txt (max count per shape over visits, cap 64); at the
  next load of the stage a worker pre-creates that many resources per shape (512 MB, smallest first, parking while the
  GPU thread creates; the old stage's unused spares are released on the worker). CreateTexture takes a matching one.
  Legs (other CPU high this batch, 3.3-4.1 cores, flat): GPR0 (records), GPA1/GPB1/GPB2/GPA2 (off/on/on/off), GXB/GXA.
  Works: 809-924 made per load, 500+ taken during the walk; creation time on 20-25 ms frames about halves (on 0.59-1.16
  ms vs off 1.13-1.40). Slow-frame COUNTS do not move (class B on 4|13|10 vs off 9|8|7; 20-25 ms 46|60|47 vs
  36|44|34) - after skip-unchanged + prebuild, creation is only ~1-2 ms of a slow frame. The loading hold is shorter
  (on 1,109-1,125 ms vs off 1,219-1,515 ms). Kept OFF: small gain, and 512 MB more resident memory on top of the
  prebuild when the freeze mechanism scales with resident volume. User decision if wanted.
  READ THIS AS CORROBORATION, NOT "pre-creation does not work": creation was only ~25% of the class-B excess (2.4-3.6
  of 11-14 ms; the pack path held the rest). Halving that quarter and seeing no change in slow-frame counts is what the
  ownership measurement predicted. The pool was always the smaller half; the prebuild was the larger one.
  NOT TESTED: the pack-OFF case the user asked about - FABLE2_TUNE "texture_pack_path=" does not turn the pack off (the
  app pushes the path live from its settings; GXB/GXA ran with the pack on). A pack-off leg needs a settings file with
  texture_pack=0.
  ASYMMETRY (tune toward it): releasing too early falls back to today's behaviour (visible settle frames); holding too
  long adds up to the cap per load. The remaining error of this design is on the benign side.
  UNMEASURED: the relative term on a slower machine (on this one the absolute term fires first); how much of the settle
  the completeness half of the gate rejects alone. The frame trace does not record that verdict; adding it to the record
  would let the combined gate be measured.
- RULES: (1) an aggregate over all frames cannot answer a question about the tail: read the counter per class, never as
  an all-frame median. (2) Before adding or subtracting two counters, write down each one's start and end call sites.
  Only nested spans subtract. (3) Classify frames into bands that cover every frame, and print each band's count, so
  that no population is classified by exclusion.

## 2026-09-27 early morning - NATIVE PICTURE IN THE GAME WINDOW, release-line merge, native portable test build

- USER (2026-09-27 ~03:55): "Once everything is tested and ready, lets publish a fully pc native version of the game
  under the portable release, all autonomous ... select the best recommended option." RECOMMENDED READING (matches the
  NG2 session's reading of the same request): build and test a native PORTABLE release locally tonight
  (D:/Fable 2 Portable Native); push/publish only after the user's own test, because RELEASE_GATE.md clause C (new game
  to credits) needs a player and cannot pass tonight.
- PRESENTATION (the blocker for any native release: the native path drew into a SECOND window, the game window black).
  Plugin (rexglue-src a24b62d + fa06262, LOCAL): export RexNgpuSetNativeFrame(shared texture, ready fence, consumed
  fence, value, w, h, is_8bpc); under gpu_offload_to_native, IssueSwap calls PresentNativeFrame: opens the shared
  handles once, queue->Wait(ready, value), RefreshGuestOutput(copy shared -> guest output), EndSubmission, Signal
  (consumed, value). App: backend::PublishToPluginPresenter - a ring of three SHARED textures + shared fences on the
  native device; the native worker (not the GPU thread) waits for the swap submission, GPU-waits on "consumed" before
  reusing a slot, copies the guest output in, signals "ready". cvar ngpu_present_in_game_window (default on); the
  native window stays hidden (shown only if the plugin lacks the export). Pair: D:/fable2_flash/sdk_legs/bin_native_final4.
  PICTURE: GW1 (publish on the GPU thread) game window vs a plugin-rendered game window at the same paused moment
  0.0015 mean|d|, 0.0% > 0.1 (plugin vs plugin 0.0001). GW2 (publish on the worker) 0.0042, 0.0% > 0.1: the difference
  is uniform grain over the whole picture, i.e. the game window shows the newest FINISHED native frame, one frame behind
  the plugin's frame index (1 frame of latency); no element is stale or missing.
  PERF: VM1 (publish on the GPU thread) - the GPU thread waited 1.8-2.1 ms per frame for the swap submission, 20-25 ms
  frames 194. VM2 (publish on the worker) - 20-25 ms 28, class B 8, 59.64 fps mean (native-window control GWN1 59.2).
  NOTE: the "waited for the swap submission" counter measures the GPU THREAD in VM1 and the PUBLISHING WORKER in VM2
  (label fixed after VM2) - different quantities. Peer: median win real (p50 17.10 -> 16.60 ms), the TAIL did not move
  (worst 43.8 -> 41.5 ms, hitches 11 -> 17, n=1 per arm - a flag, not a finding). "No dips" is still not met.
- MERGE: tu1 (v0.2.13..v1.0.3, 14 commits: TU validation/install in setup, update-loop + damaged-save fixes, ultrawide
  in setup, F10 additions and truthfulness) merged into native-gpu (38f2d14); conflicts in the setup TU text (took
  tu1) and patch_hooks (kept both). The native branch now carries everything the release line has.
- DEFAULTS FLIPPED TONIGHT (each must be a NAMED line in any release decision): skip-unchanged (571e736), texpack
  prebuild 1.5 GB smallest-first (49f652c), texpack async build (e659833), native frame in the game window (d083cb1).
  The native path itself stays OFF in the code defaults; the portable launcher sets EXACTLY the leg TUNE (read from a
  leg log: ngpu_shadow/native_draws/use_sdk_untile/bridge/bridge_draws/sdk_path/sdk_pairs=*:*/bridge_accumulate/
  present_post/post_raw/backend/gpu_offload_to_native ...), so the build under test is the configuration measured.
- NATIVE PORTABLE (D:/Fable 2 Portable Native, 1.1.0 test build, d7b538d): fable2.exe + pair bin_native_final4 + VC
  runtime; game/ and textures/ SHARED with D:/Fable 2 Portable via absolute paths in fable2_settings.cfg; user/ = its own
  copy of the saves (verified identical to the release portable's); update_check=0. Launchers: "Play Fable II
  (native).cmd" (the leg TUNE) and "Play Fable II (Xenos plugin, for comparison).cmd" (same folder, no TUNE).
- BOTH BANDS (peer audit of VM1/VM2 traces, gameplay after the last >=200 ms cluster): >=20 ms 307 -> 134, 20-25 ms
  210 -> 37, but 25-33 ms 77 -> 71 (UNCHANGED), p99 ~25 ms flat, worst 129.9 -> 117.6. The publish move drained the
  SHALLOW band only. The deep band is the readback class (A: ~12 ms fence waits on resolve readbacks) - the next
  target, under the user's decision "try changing [the readback] if the black texture don't come back".
- READBACK CHANGE (user: "try changing if the black texture don't come back"): readback_resolve_sync_budget=0 +
  readback_resolve_on_demand=true (plugin cvars; the vendored copy reads them at start). A/B RBA1 (default) vs
  RBB1/RBB2: class A (>25 ms WITH a sync readback) 74 -> 0 / 0 - the deep band's owner is gone; class B 4 | 5 | 7;
  20-25 ms 28 | 19 | 28. (RBA2 did not start.) PICTURE: moving captures RV0 (default) vs RV1 (change), native in the game
  window: the HERO is fully textured in both; RV1 vs RV0 mean|d| 0.0010, 38 pixels (0.016%) much darker = moving
  villagers. The DOG was not in view - UNVERIFIED. Adopted in the native portable launcher only, flagged for the user.
- PACKAGING TRAP (native portable, first smoke): the native path loads ngpu_{vs,ps,ps_xs,blit_vs,rect_gs,point_gs}.dxil
  and ngpu_pairs.txt from beside the exe; without them every native draw is skipped. Copied; make_release.py does not
  know about them - any native release package must include them.
- DOG CHECK (the second half of the user's condition): walking legs DG1 (readback change) / DG0 (default), native
  window, ngpu_shot_every=300, the shot copied every 5 s: 68 shots each. The HERO is fully textured in every DG1 shot.
  The DOG appears in NO shot of EITHER leg - the test save (Crucible Champion, post-game) does not have it following the
  hero on this route (story state or position unknown). So the dog is UNVERIFIABLE with this save, not verified. The
  condition stays half-met; the change is in the native portable launcher only, flagged in README-NATIVE-TEST.txt for
  the user to check with a save where the dog follows.
- DEEP RESIDUE after the readback change (RBB1/RBB2, >=25 ms after the settle: 5 | 7 frames, all 25-37 ms), by
  signature: (1) pipeline-compile bursts, 3 frames (4-10 new pipelines + ~10 resource creations in the frame);
  (2) non-readback fence waits, 4 frames (5.1-10.4 ms, sync 0 - suspected on-demand landing, NOT proven);
  (3) texture-load bursts, ~5 frames (60-61 loads vs a median of 35); (4) creation-lock stalls (rcmax 6.9-9.7 ms),
  overlapping (1). The SETTLE frames excluded by the cut are DETERMINISTIC: RBB1 idx 1167-1188 and RBB2 1124-1142 have
  identical draw counts at matched positions (5999, 12466, 7784, 7717) - a fixed target if the settle comes into scope.
- PIPELINE STORAGE (cause of (1)): nothing ever called the native command processor's InitializeShaderStorage, so the
  native path compiled every pipeline on first use in every session. cvar ngpu_backend_shader_storage (DEFAULT ON after
  this A/B; DLL off) starts the vendored storage (plugin format) blocking at backend init, in its own folder
  %LOCALAPPDATA%/fable2/ngpu_cache, title id from the kernel. Legs (all with the readback change): SS0 populate; SSA1/
  SSA2 off: 89 | 89 frames creating 136 | 135 pipelines mid-walk; SSB1/SSB2 on: 19 (26) | 5 (5). 25-33 ms off 4 | 2,
  on 2 | 0; >=33 ms 0 everywhere; worst off 27.1 | 27.8, on 27.8 | 24.5. Storage init 1 ms (empty) / 26-34 ms (warm).
  RELEASE-NOTE WORDING: compile stalls are fixed FROM THE SECOND SESSION ON; a first session compiles once (cold cache).
  Unexplained: SS0 and SSB1 took ~20 s from menu to world vs ~15 s in the other three legs.
- CORRECTION to the pipeline-storage entry and to c87faed's message (peer, same traces): "25-33 ms 4|2 -> 2|0" is NOT
  an effect - the arms overlap (the value 2 is in both; the peer's settle-inclusive parse gives off 8|4, on 6|2, i.e.
  the on value 6 lies between the off values). At n=2 per arm the band shows nothing. The justification for the default
  is the MECHANISM: pipelines compiled mid-walk 136|135 -> 26 -> 5.
  The ~20 s vs ~15 s menu-to-world gap is a POSITION effect, not storage: summed >=200 ms frames SS0 15.8, SSB1 15.9,
  SSA1 10.9, SSB2 10.8, SSA2 10.9 s - SSB1 vs SSB2 is the SAME arm 5 s apart, so the arm cannot cause it (the first two
  runs of a chain load slower; OS file cache is the ordinary explanation). The init timer (1 ms / 23-34 ms) is a
  separate, correct measurement of the storage load itself.
- SIGNATURE (2) SETTLED - the non-readback fence waits are NOT a cost of the readback change. Every v4 trace on disk
  (job tmp *.frames.bin), steady walking, frames with fence > 2 ms and 0 sync readbacks, split by the arm read from
  each leg's log (readback_resolve_on_demand): DEFAULT 46 legs, 424,390 frames: 327 (7.71 per 10k), >=25 ms 159 (3.75
  per 10k). ON-DEMAND 8 legs, 78,468 frames: 37 (4.72 per 10k), >=25 ms 12 (1.53 per 10k). They exist in every
  default leg, so they are older than the change, and the change makes them RARER. Caveat: the default legs span
  several builds of the night (skip-unchanged, prebuild, async, game window). My "on-demand landing" guess is dead.
- CORRECTION to the signature-(2) entry and 660d1b7's subject: "rarer with the readback change (7.71 -> 4.72 per
  10k)" is NOT supported - that compared legs spanning the night's builds (the on-demand legs also carry prebuild,
  async, skip-unchanged, game-window, storage). Same-binary pairs (peer): OFF RBA1 3.96, DG0 7.89 per 10k; ON RBB1
  1.99, RBB2 3.95, DG1 5.91 - the arms overlap completely, and the two OFF legs alone span the whole corpus gap. The
  defensible statement: the waits PREDATE the change (existence in every default leg) and are UNAFFECTED by it.
