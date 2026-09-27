# Night of 2026-09-22 — the porting phase, first results (claudecode-4c)

User direction (verbatim, relayed by claudecode-76): "We made a census of all the
code from xenos graphical layer that needs to be ported, we need to continue work
in that direction and replace emulation from the rexglue with native pc code we
can optimize." Autonomous all night; the user runs nothing; every test is run
here under `~/.game-test-lock` with a real `until=`; nothing pushed.

The method, as applied: one census subject at a time, a native handler for it,
differential-tested against the rexglue piece it replaces (the oracle sits in the
same tree), and TWO more columns per subject: which rexglue code it replaces and
whether that code is now DEAD on the native path. **On the hybrid build nothing
is dead**: the plugin still parses PM4, applies every register, decodes every
texture and renders the main window. The census therefore reports every ported
subject as DUPLICATED, and the number that has to move is REPLACED.

## MORNING 2026-09-23: item 1 DONE - provenance and binding are both excluded

Method: the live census now dumps every used shader's container
(`ngpu_shader_census/<ucode>_<v|p>.xvu`) and the SDK's binding map
(`.bindings.txt`); `xr_translate.py` runs XenosRecomp on each container
OFFLINE under a 60 s timeout and a 64 MB output cap (the runaway class fails
that by construction); `binding_diff.py` compares, per shader, the fetch
slots XenosRecomp's HLSL samples (its samplers are named by the shader's
constant table, the slot encoded in the cbuffer packoffset) against the SDK's
`texture_bindings`, and the layout's vertex streams against
`vertex_bindings` (the SDK numbers vertex fetch constants 95 downward; D3D9
stream s is fetch constant 95-s).
- **Runaway class: 0 of 126.** Every container the game used in Bowerstone
  translates in under a second to a normal-sized HLSL with the bounded
  XenosRecomp build. The striped material is not a runaway translation.
- **Binding map: vertex 52 of 52 MATCH; pixel 74 of 74 identical slot sets.**
  No off-by-one anywhere. The only difference, in 26 pixel shaders: the SDK
  says slots 1/4/5 are 1D fetches (dimension 0) and XenosRecomp samples them
  as 2D - the game's 1D lookup textures, which GetTexture uploads as Nx1 2D
  by design ("the translated shaders fetch them through the 2D heap at
  (x, 0.5)"). A convention, consistent on both sides of the native path, not
  a divergence.
- **So the red/green striped branches are NOT a binding error and NOT a
  runaway.** What remains: (a) the fetch-constant interpretation for that
  material's textures (format 49 sign/exp/swizzle, sampler clamp/mip state -
  aliasing stripes along a thin branch are what a wrong wrap/LOD looks like
  too), (b) XenosRecomp's ALU/sampler translation of the shader body (the
  slot map is right; the arithmetic is unverified), (c) the vertex shader's
  texcoord output (the VS maps match; the body is unverified). None of these
  can be settled by text comparison; the semantic oracle is M5 - render the
  same draws with the SDK's DXBC (verified faithful) and see whether the
  stripes go. That makes M5 the discriminator as well as the deliverable.
- Population caveat: 126 shaders, 2 min of Bowerstone; the 111 shaders the
  plugin translates that the feed never sees are not in this comparison.

## MORNING 2026-09-23, second stretch: the cache is accounted for, the subjects are named

**Check 1 (coordinator): does a fresh translation reproduce the live cache
DXIL byte for byte?** FixHlsl (C++ only, present.cpp ~3320-3453; no
fix_hlsl.py existed) ported step for step to `scratchpad/fixhlsl_compare.py`,
applied to the fresh XenosRecomp HLSL of the stability run, compiled with the
JIT's exact dxc and flags, byte-compared against `ngpu_cache/<container>_{v,p}.dxil`.
- Current header: 5/110 (the five dated 09-22). Every 09-16 and 09-21 entry
  differed by 60-70 bytes. Cause NAMED: XenosRecomp inlines
  `fable2_shader_common.h` verbatim as the HLSL prefix (checked: prefix ==
  header bytes) and the header gained `g_TessFactor`/`g_TessOffset` in the
  shared cbuffer on 09-21 14:45 (git diff vs efd9535). Splicing the committed
  09-16 header in: 09-16 71/71, 09-21 27/34. Best-of-two headers: **103/110
  reproduced from a FRESH translation by today's bounded translator.**
- The other 7 (all 09-21 vertex shaders: E6415E8304306082 FFDBABD9EAA1C48C
  5B2969152D12141F 6A56B867FFF6EF46 3FFFB321E586C82C B939B920F28D9D40
  0EF7C66A830A4BA8): the JIT's kept HLSL is dated 09-17 02:18-09:54, before
  the current XenosRecomp.exe (09-17 19:26), with the older output shape (a
  `g_Consts(INDEX)` macro where today's build emits named constant defines).
  That kept HLSL through the ported FixHlsl + dxc matches the cache **7/7**.
  So they verify the COMPILE step only; whether the old and new programs
  compute the same thing is UNTESTED. Residual, not part of a clean sweep.
  Four of them (0EF7C66A, 6A56B867, E6415E83, FFDBABD9) also have `_q`
  (tessellated) variants - ground shaders - but none of the 7 appeared on any
  listed frame of leg M.
- Denominator: 127 unique (kind, container) this run; 110 with a cache DXIL;
  17 without (16 VS + 1 PS), none with any ngpu_jit file, so the JIT never
  enqueued them - they never reached the hooked draw path on a listed frame.
  The earlier "146/170" counted OBJECTS (one container at several addresses).
- The coordinator's cbuffer-shift risk: ruled out by inspection (every member
  has an explicit packoffset; the new pair took free slots c33.z/w).
- The coordinator's "stale header starves the tessellated ground" hypothesis:
  closed by inspection with dates. The prologue that reads g_TessFactor lives
  only in the `_q` variant (FixHlsl kind 'q', enqueued and cached separately);
  a plain `_v` never references the field. All 9 `_q.dxil` are dated 09-21
  16:00-20:21 (after 14:45) and every `_q.hlsl` carries the field in header
  and prologue - one without it could not have compiled. No leg spent.

**Leg L's per-draw list came out EMPTY**: `ngpu_list_draws` existed only on
the replay path; the hooked path (the one that draws by default) had no
listing. Added there (after the issue, with the pipeline's own identity so a
flat pipeline prints `flat`, plus pixel fetch slots 0-3: address, size,
format, swizzle, tiled/linear). Leg M: 762/773/773 draws listed on frames
1800/2700/3600, no flat pipeline on any of them.

**Subjects (leg M, scene surface 14010500, mask F), to be confirmed by leg N
(ngpu_drop_ps / ngpu_skip_vs, disjoint draw sets):**
- canopy candidate: PS `A92D4C1721CA7E98` (360 draws, 5.8 M verts, alpha
  test GREATER 0.502, DXT1 diffuse + DXN in slot 2) with VS
  `6B21026765ABCD10` (510 draws). PS cache 09-16 06:17 (reproduced fresh under
  the 09-16 header), VS 09-21 19:18 (reproduced fresh).
- ground candidate: VS `2D96AFB187B6B7F6`, the only tessellated VS (`_q`
  exists) drawing to the scene (72 draws, no alpha test), with PS
  `B950F34C5012D6F7`. Leg M's shot shows the red jagged slabs across the
  whole ground AND the lavender canopies in one frame (camera down the road).
- Format names for the fetch slots (xenos.h): 6 = 8_8_8_8, 18 = DXT1, 20 =
  DXT4_5, **49 = DXN** (the normal-map slot of nearly every material), 50 =
  8_8_8_8_AS_16_16_16_16.

## MORNING 2026-09-23, third stretch: THE NATIVE SCENE TARGET NEVER CLEARS - every picture so far was an accumulation

**The measurement (legs O and Q, 07:11-07:20).** A frame-gated discriminator
schedule (`ngpu_drop_sched`, per-entry windows of 120 host frames, pre shot at
F-60 and post at F+2, two null entries, pose signature logged per shot, reader
`scratchpad/sched_read.py`) dropped, one at a time, the pixel shaders
A92D4C17 9EA07BD8 CB1A78E6 0B24BD42 A2D80DB2 5CDC928B B950F34C 3DC80BD0
EDBD9566 and the whole vertex-shader families DBF9A58A (216 draws in window)
6B210267 (255) 2D96AFB1 (102). Counters and the per-draw list agree the drops
took (every draw of each shader removed, no substitute pipeline). Result: null
floor 0.0-0.1% changed pixels over 62 frames, and EVERY entry also 0.0%,
13/14 pairs accepted by the pose gate; the scene RT dumps inside the windows
drift 2.3 -> 4.15% over the whole run with no step at any window. Leg Q,
same config with `ngpu_hooked_draws=false`: the shot and every RT dump are
100% black (1 distinct colour). Together: the hooked draws paint the whole
picture, and removing any of them changes nothing - a target that is never
cleared. Confirmed in the instrument: `ngpu_rt_clear` is default OFF by
design (the console clears when the guest asks), the stand-in is the epoch
clear on RB_COPY_CONTROL bits 8/9 at a resolve, and it fired ZERO times in
leg O ("targets cleared because a resolve asked for it" never printed). The
XDK resolve hook's 60 samples all carry flags 0x100 and RB_COPY_CONTROL
without the clear bits: Fable does not clear through its resolves. The
bit-identical pose hash across 4,000 frames was the tell.

**Leg R (`ngpu_rt_clear=true`, same schedule): a DIAGNOSTIC, not a fix.**
Null floor 54.5% and 63.9% changed pixels, black class -38/+33pp between the
two nulls: clearing on the first bind of each HOST frame wipes the target in
the middle of a guest frame whose draws straddle two host presents, so
alternate shots are mostly black. Floor-limited by construction; no
per-entry delta is reported from it. It proves the picture does change once
cleared (the accumulation story is complete) and that the host present is
not the guest's frame boundary. `ngpu_rt_clear` stays default off.

**RETIRED by this (not merely retracted - exclusions carried forward would
be worse than wrong claims):**
- "A92D4C1721CA7E98 is NOT what paints the canopy" (leg N) - void; back on
  the list at full weight.
- the 4.7pp top-right "partial effect" and the +1.8pp black corroboration
  (L vs N) - void both ways; they were two runs of one accumulation.
- every `ngpu_skip_ps` / `ngpu_drop_ps` / `ngpu_skip_vs` reading taken off a
  shot, and the L/M/N shot readings.
- the night's user-facing "first native image: geometry matches, shading
  does not", and the canopy/ground/stripe diagnoses including "a two-channel
  DXN normal map used as colour": all descriptions of an accumulation. The
  shading defect is NOT established as a shading defect.
- the SUBJECT of every shot before native resolves ran: with no swap surface
  ever learned, the presented target was chosen by the busiest-target
  FALLBACK (the code's own count: 22 of 119 shots landed on the scene
  surface), so each shot was a plausible picture of whichever target was
  busiest. Shots from the first leg with resolves live (leg X) start a
  fresh picture baseline; nothing earlier is comparable with them. A
  fourth, independent reason beside accumulation, missing resolves and
  refused draws.
- the retracted never-cleared-depth root cause of 2026-09-22 was SAMPLED
  (painted and stale read identically); the colour accumulation here is
  measured by removal of draws - a different instrument. It inherits neither
  the old claim's credibility nor its discredit.

**SURVIVING, kept apart (byte-, counter- or text-based, none from a
picture):** DXBC differential 406/406; fetch-order 8,1,2,0,4,5,10 identical
instruction by instruction (no slot permutation); cache provenance 103 + 7;
untile byte-identity; the device-shadow decode; the ring arithmetic.

**What the console does instead, and what to port:** Fable clears through
`D3DDevice_Clear`/`ClearF` (a RECT_LIST draw written to the ring by the XDK),
which is not one of the DrawIndexedVertices / DrawVerticesUP entry points
the hook covers; no hooked draw has prim 8. The trace module has 304 XDK
entry points by address (`native_gpu_trace.cpp`, `ngpu_trace`); the clear is
one of the unnamed "ring" writers. Leg S (trace census) identifies it by
call rate and argument shape; then the hook applies the guest's clear
(colour, Z, stencil, flags, rects) to the bound native target - the
console's semantics, not a per-frame wipe.

**Reconciliation, one leg (T, frames 3600-4200), so the buckets sum:**
RingAdvance calls (= hooked draw entries) 2,138-2,171 per frame; native
draws counted 2,069-2,083 with 0 skipped; issued through translated shaders
2,068-2,072; the ~70 between entries and native draws are pre-count skips
(no render target, impostor). Leg O, same arithmetic: entries 2,125-2,151 =
native 1,375-1,420 + skipped 641-696 (all "draw: range") + ~60 pre-count.
The ring reader's DRAW-packet count (610-1,314 per frame) is SMALLER than
the hooked count: a floor on what the reader parses, not an external total.
The earlier "1,363 packets the hook never sees" was the wrong reading of it;
the gap is reader coverage. Whether the clears are in the ring at all is
therefore unmeasured, not excluded.

**Resolves: a dead path by conjunction of defaults.** `ngpu_replay_resolves`
(default true) makes NoteResolveDest queue every resolve for the REPLAY and
return; `ngpu_bridge_draws` (default false) means the replay never runs. So
ResolveNative - the only code that copies a native target to its destination
texture and the only reader of the clear bits - was called ZERO times in the
default hooked configuration, for every picture ever judged. The XDK resolve
hook itself ran (60 sampled calls, all flags 0x100, no D3DRESOLVE_CLEAR
bits). Task: a LIVE/DEAD line at startup per subsystem naming the cvars that
decide it (resolve routing first).

**THE LARGEST UNDRAWN POPULATION: "draw: range" refusals.** The translated
path refuses any draw whose (base_vertex + max_index + 1) x stride exceeds
the fetch constant's declared size: 641-1,377 draws PER FRAME (legs O and S)
against ~1,400 drawn, i.e. 30-50% of every frame never issued. The code
comment beside the site says "3.2% of Fable's recorded draws land here" - a
different population (recorded REPLAY draws), quoted as if it were this one.
Leg T (`ngpu_range_clamp=true`, negative prediction written first): 2,068
draws issue, refusals 0, clamped 879-992 per frame, and the shot grows large
dark triangles fanning across the canopy and cream sheets at the hero's feet
- stretched geometry from zero-filled vertices. So the clamp is NOT the
faithful port; the refusal was hiding a DECODE defect: either the fetch
constant's size is decoded short (the console reads real vertices there) or
the draw's range is mis-derived (max_index from the index buffer, base
vertex, stride). Next: the refusal histogram by (VS, stream, surface, mean
overshoot bytes), printed every 300 frames (built 07:35): a row or two says
size decode; megabytes say index decode. Zero-fill semantics for the record:
specified on D3D12 (IA reads past the bound view return 0, the runtime's
debug text says so), a feature on other Plume backends (Vulkan
robustBufferAccess); the vendor-independent form is the in-shader bounds
mask the SDK's DXBC emits for every vfetch (M5).

**THE RANGE TERM NAMED (legs U and V, 07:34-07:41): a STALE RING RECORD
decodes the index buffer of every hooked draw.** Histogram (leg U, clamp
off, 300-frame windows): ~975 refusals per frame, mean overshoot in
MEGABYTES, two clusters by surface - 1.3-2.4 MB on the depth pre-pass
surface 10000410 (E4D8ABB1, BD71193F, 3F5ADB0F) and 14-67 MB on the scene
14010500 (BCC6E2DE 480/frame, C27115F5, 2A5207A8, 2D96AFB1 at 65 MB); the
same vertex shaders also ISSUE draws (BCC6E2DE 59/frame), so per draw, not
per shader. "Every refusal on stream 0" is an artefact of traversal order
(early return past cached streams) and is struck. Samples (leg V, 90): all
idx32 0 and endian 2 (a 32-bit swap mode on a 16-bit buffer, so the record
is not this draw's); 87 have max_index exactly 65535 - the RESTART INDEX
read as a vertex - with declared sizes of 5-82 KB, so overshoot ~ need and
need/declared varies 16-250x (an absolute term, not a factor); 3 are
byte-swapped small indices (768 = 0x0300 for 3): an endian term. The
mechanism: the hooked path took idx32 (DRAW initiator bit 11), the endian
(VGT_DMA_SIZE bits 30-31) and, through the ring shadow, the restart mode
bit and index from "the most recent DRAW record" of a ring reader that
parses 610-1,314 DRAW packets per frame against ~2,150 hooked draws (it does
not descend indirect buffers) - so for about half the draws the record is a
stale one, AND THE SAME RECORD DECODES THE INDICES OF THE DRAWS THAT ARE
ISSUED (CachedIB), so "geometry matches, shading does not" is not safe to
carry either. Retired-as-measurement ring parse, still wired into a live
path: strictly worse than a discredited instrument. Consumers audited in
the hooked draw path: idx32, endian, restart (fixed below); VS/PS constants
from the ring (replay-only / behind ngpu_ps_const_ring, not live); the UP
path's stream-0 fetch override and its `recent[q & 31]` lookup (UP draws
are off by default; the `& 31` indexed a 256-entry array stored by
`% 256`, so it answered "no packet" instead of failing - fixed).
Fix, built 07:45 (`ngpu_ib_fresh_only`, default on): a record is FRESH only
when it names this draw's index buffer; a stale one falls back to 16-bit
8in16; restart mode and index come from the DEVICE shadow (Reg(dev, ...));
a per-300-frame tally prints fresh/stale counts, the (width, endian) shapes
of the fresh records, and the index-buffer OBJECT's Common and Size words
beside them, so the object replaces the ring as the source once its bits are
read off the fresh correlation.
**Leg W (07:45) - what it tested was NOT the freshness fix.** The tally read
fresh 0 / stale 612,723 (100%): the comparison `rp.ib == ib_phys` never
held because the object's word is a guest VIRTUAL address while the packet's
DMA base is PHYSICAL and includes the start offset. So every draw was
decoded by the fallback, and the result is "IGNORE THE RING AND ASSUME
16-bit 8in16 PLUS DEVICE RESTART STATE beats the stale record": range
refusals went from ~975 per frame to ZERO in every window, both clusters
and the third (04000140), 2,046-2,062 draws per frame issued, 0 skipped.
The fresh-record mechanism executed zero times and is UNVALIDATED; the
object route still has to be proven. And zero refusals is not evidence the
decode is right: a 32-bit buffer read as 16-bit yields indices too SMALL
to trip the check and draws garbage silently, so the next build prints the
need/declared distribution of the ACCEPTED draws per surface (a population
far below 1 = a decode silently too small). Also in that build: the
comparison redone in the packet's terms (physical, with and without the
start offset, counted apart), the stratified sampler (three per surface per
window plus three above 4 MB), a counter for the no-device restart path, and
the RESOLVE ROUTING FIX (queue for the replay only when the replay runs;
otherwise the hooked path performs the resolve itself - correctly
positioned, since the XDK's Resolve call sits between the hooked draws on
the same guest thread). Native resolves become LIVE; the epoch clear still
cannot fire for Fable (its resolves carry no clear flags), so the targets
still accumulate and the picture stays unjudged.

**Leg R recomputed from its RT dumps** (coordinator's request): not readable
per entry (900-frame cadence; consecutive undropped dumps differ 6-16% from
time alone, no pair inside a window). Under per-present clearing every dump
is a complete single frame (9-10% black, all seven) and the red slabs across
the ground and the pale canopy are in every one of them - so **the defects
are PER-FRAME, not only an artefact of accumulation.** That is the whole
claim. It does NOT reinstate "the shading is wrong": leg R predates the
clamp, so its frames are complete pictures of an INCOMPLETE frame missing
641-696 refused draws (30-50%), and slabs or a pale canopy may be what a
scene looks like with those draws absent. Also, the colour-class figures
(red 15-22%, lavender 8-13%) came from a classifier built for tonemapped
shots applied to the HDR target before tonemapping; they are withdrawn as
colour statements and kept only as "the picture changed / did not". The
picture assessment is re-run after the decode defect is fixed, on the
tonemapped output, not before. The RT dump still outranks the shot as the
picture instrument (seven dumps internally consistent, the shots at the same
presents 12-17% black on some and 45-65% on others), and from the next build
the scheduled discriminator windows also record RT dumps at F-1 and F+2.

## TOMORROW, IN THIS ORDER (the three things that decide the next step)

1. **Striped-shader PROVENANCE (cheapest, may close the shading bug alone).**
   The tree branches render as red/green stripes (section "Vision"). The
   native path renders with XenosRecomp's HLSL - an UNVERIFIED translator with
   a proven defect class: five shaders once came off an unbounded control-flow
   walk at 32-34 GB each (list at
   `D:\ng2_frameinterp\work\xenosrecomp_runaway_output.txt`), and a walk that
   TERMINATES still emits wrong blocks at a plausible size with no alarm. 435
   of the 664 entries in `ngpu_cache` predate the bound (dated 2026-09-16).
   Find the striped draw's pixel shader hash (`ngpu_tex_slots` whitening one
   slot at a time, judged on the renderer's own `ngpu_shot_every` BMP), then:
   is it on the runaway list / pre-bound? If yes, re-translate and look again.
   If not: compare that PS's tfetch->fetch-constant map between XenosRecomp's
   HLSL and the SDK's analysis (`Shader::texture_bindings()` - in-app now; an
   unverified translator differentialled against a verified one).
2. **The COHERENCE question that decides whether M5 is a lift or a re-import.**
   M5 = make the native draw path consume the SDK's DXBC (verified faithful,
   406/406) and retire XenosRecomp. The SDK shader's vertex fetch reads a
   ByteAddressBuffer that MIRRORS guest physical memory (address = fetch
   constant base + index*stride). The SHADER requires: a buffer whose offsets
   equal guest addresses and whose contents are current for the ranges the
   draw reads. The PLUGIN satisfies that with SharedMemory = page-watching -
   the machinery the native path exists to remove. The native path can instead
   predict-and-upload at the draw hook (it already knows every VB/IB range
   from the fetch constants + index range and uploads exactly those bytes with
   a content hash) IF AND ONLY IF the set of ranges a draw reads is
   STATICALLY PREDICTABLE for every shader. What page-watching covers that
   prediction does not: (a) a write between the upload and the draw, (b) a
   read outside the declared ranges - any runtime-computed address (memexport,
   dynamically indexed fetch) reads bytes never uploaded and gets plausible
   garbage silently. TO ANSWER: enumerate, from the SDK analysis of all 238
   shaders (`vertex_bindings()`, memexport use, dynamic addressing flags) and
   from the DXBC resource bindings, whether any shader computes a fetch
   address at runtime. None -> predict-and-upload is sound, M5 is clean.
   Any -> conservative upload / a resident coherent mirror (page-watching
   back) / a per-shader exception list: a trade for the USER, not to discover
   afterwards. Stated cost either way: a 512 MB mirror resident in VRAM on a
   GPU that often carries ~24 GB of unrelated AI work - ask whether it can be
   SPARSE (only pages a draw touches committed; an uncommitted page faults
   instead of returning plausible garbage) rather than fully committed.
   **MEASURED (leg I, 127 shaders): the read-set is NOT index-derived for
   Fable** - 74 of 154 vertex fetches in 19 shaders index from a register
   other than r0.x (computed), 17 shaders use dynamic addressing, and ONE
   shader (0D7251C284702D9B v) uses memexport (a WRITER). The model that
   survives is predict-and-upload BY THE FETCH CONSTANT'S DECLARED RANGE
   (base + size), which bounds every vfetch whatever its index. The
   load-bearing assumption under that - "the hardware clamps, but the native
   path runs translated DXBC and a ByteAddressBuffer load does not" - IS
   SATISFIED BY THE TRANSLATOR: the SDK's DXBC vertex fetch emits a bounds
   check (`dxbc_translator_fetch.cpp` ProcessVertexFetchInstruction: the
   buffer end from the fetch constant's size field, bits 2:25 of dword 1, and
   a per-word in-bounds mask, "where the hardware clamps and returns" -
   out-of-range words read as zero, never as un-uploaded memory). Verified by
   reading the vendored source (0cb9040), not by running it. The mask is
   semantic equivalence, not only protection: the hardware clamps and returns
   zero, and the per-word mask reproduces that.
   **READ SIDE SOLVED BY THE EMITTED CLAMP; WRITE SIDE OPEN.** memexport is a
   WRITER, and the declared-range model is a READ model: the clamp bounds
   what a shader reads and says nothing about where it writes, nor about
   getting those writes back into guest memory so the next draw and the CPU
   see them - a different mechanism, not a corner of the same one. Known
   instances: **1 in Bowerstone** (0D7251C284702D9B v); population elsewhere
   UNMEASURED, and a scene that exercised zero viewport changes and zero
   alpha-map divergences is the scene least likely to exercise an unusual
   write path. Do not read "memexport 1" as "memexport is negligible". Also:
   the "vertex extents 0 short" corroboration covers the r0.x fetches only -
   a computed index is not bounded by the draw's index range, which is the
   interesting case.
3. **M5 itself**, only after 2: the SDK DXBC + its binding model (mirror
   buffer, system-constants cbuffer: viewport/ndc scale, texture signedness,
   ... - state the native path already reads) replacing the XenosRecomp
   DXIL + input-layout path. Then the shader row can move from DUPLICATED
   toward REPLACED for the first time.

Standing rule from tonight's two zero-magnitude results: **a magnitude
measured only in Bowerstone is written "0 in Bowerstone", never "0"** -
Bowerstone is a weak exerciser (the alpha-map divergence: 0 draws there; the
viewport gap: no instance there - every viewport was the full target).

## Results that landed (each with its oracle and scope)

1. **The full untile+endian differential — PASS.** `SelfTestUntileFull`
   (native_gpu_present.cpp): the hand untile loop, factored as the pure
   `HandUntileLevel` (the code that runs, not a copy) vs the SDK's
   `texture_conversion::Untile` + `CopySwapBlock` on synthetic input: 7 formats
   (block sizes 1/2/4/8/16 incl. DXT1/DXT2_3), tiled+linear, endian 0..3, 12
   geometries with packed-tail offsets. **672 cases, 0 mismatching, 0
   format-table disagreements.** Scope: untile addressing + endian swap of one
   storage level. NOT covered: format->host mapping, mip-level geometry,
   PackedMipOffset, cube faces, bias/exp/scale post-passes, DXT3A expansion, 3D/
   volume tiling (GetTexture DECLINES dimension 2 - unmeasured), and the plugin's
   live texture DECODE. Texture formats + endian are marked **PARTIAL**, never
   VERIFIED, with that scope in the oracle column.
   - **Sub-word class named and bounded:** a block narrower than the swap word
     (8-bit format with 8in16, 8/16-bit with a 32-bit swap) is undefined for a
     per-block swap: the hand loop writes past the block (120/120 cases
     overran), the SDK writes nothing. Excluded from the verdict; GetTexture now
     logs once per (format, endian) when the game fetches one, so the exclusion
     is measured, not assumed.
   - **ORACLE IS THE CPU REFERENCE, NOT THE LIVE PLUGIN PATH:** the plugin's
     D3D12 texture cache loads textures with `texture_load_*_cs` compute shaders
     and never calls `Untile`/`CopySwapBlock` (only draw.cpp uses
     `GetTiledOffset2D`, for resolve addresses). The differential proves
     conformance to Xenia's CPU reference implementation, which shares the
     tiling tables - a smaller and more honest claim.

2. **A defect in the oracle, found by that differential.** `CopySwapBlock`
   (rexcore texture_conversion.cpp, inherited from Xenia) passes a BYTE length to
   `copy_and_swap_16_in_32_unaligned`, whose count is DWORDS: for
   `Endian::k16in32` it reads and writes 4x the block. The first run of the test
   corrupted its heap on that and the process died with nothing in the log. It
   is NOT in our vendored tree (conversion.cpp lives in rexruntime.lib); the
   native handler `SdkSwapBlock` does the 16in32 swap inline with the right
   count and uses the SDK function for the other three endians. The test now
   allocates sentinel slack and MEASURES overruns per handler (sdk 0 after the
   fix; hand 120 = exactly the sub-word class).

3. **The SDK shader translator compiles and links INTO the app.** The handover
   said "adopt DxbcShaderTranslator"; it is not linkable: 0 ShaderTranslator
   symbols in rexruntime.lib (29,406 exports) and rexgpu-xenos.lib (23). The BSD
   sources are vendored under `src/native_gpu_xlat/` from rexglue-src
   **c94f5eb (2026-08-21 'Release v0.10.0')** - the commit whose include/ headers
   are byte-identical to the installed SDK's (HEAD adds API the installed headers
   lack; the first attempt failed on it). Coexistence with rexgpu-xenos.dll:
   the DLL has hidden visibility, so no symbol clash; the five cvar names the
   translator TUs define/read are left to the PLUGIN to register (vendored
   patch: accessor-only definitions, see ORIGIN.txt and xlat_support.cpp), and
   `LogCvarOwnership` prints the registry's value beside this module's so a
   divergence is visible. One table (`kD3D10StandardSamplePositions4x`) is
   defined with the plugin's values.

4. **Shader-ISA census from the SDK's own parser** (`native_gpu_shader_census.cpp`):
   a `ShaderTranslator` subclass records the opcodes as the SDK walks each
   analysed shader (SEEN, no hand decode), then the in-app DXBC translator
   translates it (PORTED). Fed from RingParse's IM_LOAD/IM_LOAD_IMMEDIATE on the
   real forward parse only. First 75 s Bowerstone run: 2,391 loads -> 84 unique
   shaders (40 VS, 44 PS), analysed 84/84, **translated 84 ok / 0 failed, 1,914 KB
   of DXBC**. Ledger: DUPLICATED (the plugin still translates the same shaders;
   the native draw path does not consume this DXBC yet).
   - **"84 ok" is not "84 correct"** (claudecode-76): the DXBC differential vs
     the plugin's own translation is built - the plugin dumps ucode + DXBC per
     (hash, modification) with its `dump_shaders` cvar; `ngpu_shader_diff_dir`
     re-translates each with the same modification and compares bytes, recovering
     the constructor parameters the dump does not record (bindless / gamma /
     msaa2x / scale) by search. Result: see the run log for this night.
   - **The SDK analyser is not safe on arbitrary input:** an exec block outside
     the ucode is read out of bounds; a texture-fetch opcode it does not name
     leaves `opcode_name` null and the disassembler strlen()s it. Two runs died
     on blob FCE031900C090BE7 (27 dwords of guest POINTERS - an IM_LOAD the ring
     parser mis-read, not a shader). `ValidateUcode` now mirrors the SDK's walk
     with the SDK's unions and REFUSES such blobs, counted (`census::Refused` +
     the shader report), never silently.

5. **Census instrument corrected (it was counting itself):**
   - `RingSeedFromShadow` copies 336 shadow registers into the ring reader at
     every resync; each went through `See()` as if the guest had written it.
     Skipped now (`g_ring_seeding`). This is the likely source of the
     '0x2010-0x2012 seen ~4-5x' part of the earlier off-census block (the seed
     writes 0x2000..0x2012 and register_table.inc names nothing past 0x200F).
   - The resync probe re-walks the same 16 KB from up to 512 start offsets;
     every failed walk's garbage headers were counted as arrivals. Walks are now
     TENTATIVE: committed only when they stick, otherwise discarded and counted
     per kind as `discarded(probe)`, so the fix is conservation-checkable.
   - `MarkPorted` was never called at the register apply sites: the 31
     registers the native path reads (audited by grep of every read form) are
     now marked, each with its rexglue counterpart named.
   - New tiers: REPLACED / DUPLICATED / unnamed per ported subject; PARTIAL
     (one stage verified, scope in the oracle string) kept apart from VERIFIED.
   - VERDICTS on the earlier findings (0x2010-0x202D, sub-0x2000 garbage, the
     767-register episode) require a run that reaches the world and stays there;
     see the run log section below.

6. **State decoder finding (blend):** the hand path used the COLOUR factor
   table for the ALPHA blend factors; the reference (`kBlendFactorAlphaMap`)
   maps raw 4/5/8/9 to SRC_ALPHA/INV_SRC_ALPHA/DEST_ALPHA/INV_DEST_ALPHA. Tables
   lifted verbatim into `native_gpu_state_decode.cpp`; `SelfTestStateDecode`
   compares the hand decoders against them over the whole value space and the
   field layout against the SDK `reg::RB_BLENDCONTROL` struct, expecting exactly
   that alpha difference; the draw path decodes alpha through the reference
   table behind `ngpu_blend_alpha_map` (default on; off = the control) and
   counts the draws it changes.

## Instrument lessons paid for tonight
- An instrument that destroys its own evidence under load: a per-draw warning
  ("dxil not found") wrote 90 MB in 75 s, rotated the log 18 times and took the
  self-test verdicts with it. Log once, with a count.
- A crash must name its input: the shader census now logs hash/type/addr/size/
  head before analysis and writes the raw blob to `ngpu_shader_census/`.
- The worktree's build dir had none of the native shader assets (ngpu_vs.dxil,
  ngpu_cache, ...) - they lived only in the main repo's build dir; two legs
  measured nothing for want of them. Copied (never moved).

## 7. The DXBC differential — 406 of 406 byte-identical (22:38 leg)
The plugin's `dump_shaders` wrote, for 238 unique shaders (83 VS, 155 PS), the
ucode and 406 DXBC translations (one per modification). `ngpu_shader_diff_dir`
re-translated each with the same modification in-app and compared bytes:
**MATCH 406, MISMATCH 0, no-ucode 0, unreadable 0**, 9,125 op marks. The oracle
constructor parameters the dump does not record were recovered by search and
were the SAME for every file - bindless=1, gamma_unorm8=0, msaa2x=1, scale=1 for
VS and scale=2 for PS (resolution_scale=2 reaches pixel shaders only) - a
recovery, not a coincidence. Census: ALU-vector 25/30 VERIFIED (24 seen),
ALU-scalar 36/50, fetch 3/9, control-flow 9/16 - exactly the ops the game
exercises.
- **What it proves, precisely:** the LIFT IS FAITHFUL. Translator and plugin
  were built from the same commit (0cb9040), so identical bytes are the
  expected result; it establishes that the transplant is unmodified, the
  integration feeds the same inputs and the build configuration was recovered.
  It does NOT prove an op correct in a deeper sense - one code compiled twice.
  The oracle column says "VERIFIED AS A FAITHFUL LIFT".
- **Byte-identity cannot survive optimisation.** The user's direction is native
  code "we can optimize"; the moment an op is optimised this test fails BY
  DESIGN. It is a LIFT-PHASE oracle. Optimisation needs a SEMANTIC oracle -
  same rendered result on the same inputs (the vision tools + per-pixel
  comparison, clause E) - decided now, not mid-optimisation.
- The first attempt (translator vendored from c94f5eb, whose headers match the
  installed SDK) gave 0 of 16 matching, every one 476-876 bytes SHORTER than
  the plugin's: the DLL was built from HEAD, whose dxbc_translator_fetch/om/
  memexport.cpp differ (+320/-66). Re-vendored from HEAD with its three newer
  headers (a diff, not a characterisation: one enum value, one inline method,
  two NON-virtual member declarations, one constexpr, one comment) placed
  target-wide ahead of the SDK's, so no TU of the exe sees a different copy.
- The row stays **DUPLICATED**: the plugin still translates these shaders for
  the main window; nothing has been removed.

## 8. The ring parse is not a SEEN source (22:30 and 22:38 legs)
With native draws on, seed excluded and probe walks tentative: PM4 seen=47/47
plus 81 off-census opcodes (47+81 = 128 = every value a 7-bit field holds),
register seen=1151/1164 plus 15,233 off-census indices - **15,233 + 1,151 =
16,384 = 0x4000 exactly**, every index below the constant blocks, each ~40k
times in a flat distribution; register 0 x3.2M (zero dwords read as 'write
reg 0'); SCLK_PWRMGT_CNTL2 x8.4M. Committed register arrivals 783M, 646M of
them (83%) off-census, 0 overshoots. **When a census reports the entire value
space of a field it is measuring the parser, not the program.**
- Mechanism: one packet shape - a type-0 header with count 16384 at register 0
  (dword 0x3FFF0000, which is also two 16-bit indices 0x0000/0x3FFF). The
  parser walks INLINE DRAW DATA as packets: the 113 user-pointer draws per
  frame copy their vertex/index arrays into the ring. Those same 113 draws are
  the ones logged as "draw failed" on the native path - one item, not two.
  The landing gate cannot fix it (a garbage count past the write pointer reads
  as "packet still being written").
- Verdicts: 0x2010-0x202D DISAPPEARS (the middle of a swept range, same flat
  count as its neighbours; the seed was never its main source); sub-0x2000
  garbage SURVIVES, explained and bounded; the 767-register conservation
  episode: the RULE was right to fire, the NUMBERS are void (two garbage-
  dominated populations).
- Done: ring-fed register/PM4/primitive See() OFF by default
  (`ngpu_census_ring`, an instrument of the parser); registers censused from
  the DEVICE SHADOW at draw time (value change per draw, a floor); primitives
  from the draw hooks' own argument; BOTH register denominators printed on
  every report (the SDK's 1,164 and the 144 the XDK shadows) with the gap
  stated - never a percentage against the smaller alone.

## Run log
- 21:06 leg A: died in SelfTestUntileFull (heap corruption from the SDK's
  k16in32 overrun) - nothing in the log. Finding 2.
- 21:24 leg A: no native draws (worktree build dir lacked ngpu_vs.dxil etc.),
  warning flood rotated the log 18x; shader census 84/84 translated.
- 22:15 leg A: crash in AnalyzeUcode on blob FCE031900C090BE7 (guest pointers).
- 22:22 leg A: same crash, now NAMED by the pre-analysis log; plugin dump 882 files.
- 22:30 leg A: full 2 min, UNTILE FULL PASS 672/0, STATE DECODE PASS, 128
  texture dumps; DXBC diff 0/16 (version mismatch) + guard too strict (390
  refused); ring census garbage-dominated (finding 8).
- 22:38 leg A: DXBC diff 406/406; guard fixed; ring census still 83% garbage
  under the landing gate.
- 22:46 leg B: SDK untile ON. Died at 34 s in the world: the ring-fed shader
  census handed the SDK analyser a 6068-dword 'vertex shader' of inline
  vertex data (0xDF87A904) that passed every structural check (exception 0
  at 0 = an abort inside AnalyzeUcode). The 128 texture dumps landed first.
  **A/B ON REAL DATA (leg A hand untile vs leg B SDK untile, matched by
  name): 44 PASS, 2 UNMEASURED, 0 FAIL.** The 2 unmeasured are the 1D lookup
  textures (256x1 f6, 64x1 f29) whose RAW SOURCE differed between the two
  runs (the game rewrites them), so no valid comparison existed for them -
  an absence of evidence, not an explanation. Covered on real data: DXT1 f18
  e1 tiled x29, DXN f49 e1 tiled x23, DXT5 f20 e1 x4, 8888 f6 e2 tiled x3 +
  linear x2, f29 e1 linear x2, f2 e0 tiled x1. BASE LEVEL ONLY (the dump
  fires at L==0); the packed-tail path rests on the synthetic sweep, a weaker
  leg (synthetic input, not the game's), wherever the 44 is quoted.
  **General lesson from the crash that ended this leg: STRUCTURAL VALIDITY IS
  NOT IDENTITY.** A validator that accepted 6068 dwords of vertex data as a
  shader is the instrument answering instead of failing; the fix was a
  better SOURCE (the shader object, which carries its own identity), not a
  stricter structural check.
- Consequence: the shader census is now fed from the SetShader hook's XDK
  shader OBJECT (magic 0x102A, sizes, physical block + 0x80), with a per-blob
  confirmation against the plugin's ucode dumps by hash; the ring feed is an
  instrument of the parser behind `ngpu_census_ring`. Validator: size cap
  4096 instructions, exec blocks must lie after the CF program.
- 22:52 leg C: full 2 min, 60.0 fps p99 19 ms 0 hitches at the end, no crash
  with the ring feed off. FIRST CREDIBLE REGISTER CENSUS, from the device
  shadow: 55 SDK-named registers seen, PORTED 19 of them; **89 shadow slots
  the SDK's register_table.inc does not name** (0x2010-0x2012 among them - the
  XDK shadows 144 slots, the SDK names 55 of those). The work queue it names,
  by change count in 2 min: RB_STENCILREFMASK x399k, RB_BLENDCONTROL1 x47k
  (the second render target's blend), PA_CL_VPORT_X/YSCALE + X/YOFFSET x22.7k
  (the VIEWPORT - the native path reads only ZSCALE/ZOFFSET today), RB_BLEND_
  ALPHA/BLUE x20k (blend constant), COHER_DEST_BASE_7 x11k; textures k_1_5_5_5
  x3 and k_24_8_FLOAT x2 unported. Primitives from the hooks: 4 seen, all
  ported. Shader census from objects: 102 unique blobs, 70 REFUSED (garbage),
  32 analysed, 11 confirmed by a plugin dump hash -> the fixed 0x80 header
  assumption was wrong; now the container's Shader header gives offset+length.
  Caveat on 'seen': the first draw counted every slot once (fixed after:
  non-zero once, then changes).
- 22:58 leg D: full 2.5 min, 60.0 fps p99 19.4 ms 0 hitches at the end.
  **Shader census from the SetShader objects, header-driven: 171 noted -> 127
  unique (53 VS, 74 PS), 0 REFUSED, 109 of 127 CONFIRMED by a plugin ucode
  dump of the same hash** (the container's Shader header offset+length IS the
  GPU's load, byte for byte; the 18 unconfirmed are most likely shaders the
  plugin served from its on-disk pipeline cache without re-dumping - not
  checked yet). DXBC differential still 406/406. Registers (device shadow,
  non-zero-once-then-changes rule): 36 SDK-named seen, 48 XDK-shadowed slots
  the SDK does not name seen, of 144 shadowed. The object layout, from the
  first 12 objects: type 6 (VS) container at +872, type 7 (PS) at +0x28;
  shader header at container+[+24]; code offset 0 or 64 within the physical
  block; magic 0x102A1101 (VS) / 0x102A1100 (PS).
- **Vision tool DEFECT, not a quirk:** the Plume shadow window cannot be
  captured by PrintWindow or Windows Graphics Capture, and the AI Vision
  tool then FALLS BACK TO A SCREEN COPY OF WHATEVER COVERS THE WINDOW - it
  returns a plausible picture of the WRONG THING with no error (the header
  carries a one-line 'fallback:' note that is easy to miss). A per-pixel
  comparison run through it would be confidently wrong - the exact shape
  that cost hours here before. Use the renderer's own `ngpu_shot_every` BMP
  for the native output; the main (plugin) window captures via PrintWindow
  fine. Recorded in the AI Vision memory so the next person knows.
- **First native-vs-plugin picture, same camera (23:04, Bowerstone Cemetery
  at the gate), `docs/native_gpu/shots/2026-09-22_2304_cemetery_native_shadow.png`
  vs the plugin window:** geometry matches (columns, fence, gate, tombstones,
  hero); shading does not - the ground is BLACK, the tree canopy reads
  blue/purple with white leaves, and the tree BRANCHES are drawn as red/green
  striped ribbons where the plugin shows brown bark. Red/green with no blue
  is what a two-channel DXN/BC5 normal map looks like used as colour - so
  either a texture-slot binding is off (a diffuse slot sampling the normal
  map) or the fetch constant's format/sign handling is wrong for that
  material; the untile stage is NOT the suspect (byte-identical to the
  reference on 44 real textures, DXN among them). Not investigated tonight;
  it is the previous session's "black terrain / format 49" item with a
  sharper symptom.
- Log hygiene: two per-frame lines from earlier instruments ("vertex extents
  over N stream-draws", "skinned draws this frame") wrote ~15k lines in 2 min
  and rotated the 5 MB log; throttled to every 300 frames. Structural fix:
  `ngpu_verdicts.txt` next to the exe - self-test verdicts, the DXBC
  differential and per-kind census lines (when they change), appended with a
  timestamp; nothing hot writes to it.
- 23:11 leg F: full run, 60.0 fps. **THE SHADOW-BLOCK ANCHOR CHECK (an external
  quantity, not the values alone):** the guest viewport is 1280x720, so
  PA_CL_VPORT_XSCALE/XOFFSET/YSCALE/YOFFSET must read |640|/|640|/|360|/|360|;
  the sampler shows 0x44200000/0x44200000/0xC3B40000/0x43B40000 = 640/640/
  -360/360 EXACTLY, plus 128 (256^2 targets) and 512 (1024^2 targets) - every
  viewport in the scene was the full target (scale = half the size), so the
  "wrong for any draw not full-window" prediction has NO exercised instance in
  Bowerstone (the registers stay unported; the consequence's magnitude there is
  0). The negative YSCALE is the Y-flip convention, recorded as such, not as
  part of the layout check. ZSCALE/ZOFFSET = -1.0/1.0 (reverse depth via the
  viewport). RB_BLEND_RED..ALPHA = 1.0/0.98/0.949 floats; RB_ALPHA_REF =
  0.996/0.502/0.059 floats (so its 1.4M changes are real per-draw alpha refs);
  RB_STENCILREFMASK = 00FFFF02/00FFFF12/00FFFF00 (real refs 2/0x12/0; the 398k
  changes are real); RB_DEPTHCONTROL/RB_BLENDCONTROL0-3/PA_SU_SC_MODE_CNTL
  decode to sane state. **The 0x2100 block and the head of the 0x2200 block are
  anchored.**
  **RETRACTION of "48 used-but-unnamed registers": past 0x2210 the shadow block
  is NOT a register array.** 0x2212 = 15.0f; 0x2224-0x2227 = 2.0/1.0/2.0/1.0;
  0x2247-0x224A = -32.0/-0.001/-80.0/-0.005 = polygon-offset scale/bias pairs
  (a Xenos register pair that lives at 0x2380-0x2383 in the hardware map, not
  here); 0x223A = guest pointers 0x12xxxxxx; 0x223B and 0x2012 = render-target
  size h<<16|w (256x256, 1280x720, 1024x1024); 0x2257/8 = physical addresses
  0xFFC8xxxx; 0x225A = a counter. The XDK's device shadow is its OWN state
  table whose head mirrors registers. So the register census now takes the 59
  slots that are registers (0x2000-0x200F, 0x2100-0x2114, 0x2180-0x2184,
  0x2200-0x2210); the other 85 dwords are XDK-private fields, sampled and
  logged, not register subjects. The polygon-offset floats and the target
  sizes are D3D9-level state the native port will need - from the XDK fields,
  not from a register.
  Shader reconciliation: the feed's unique count is STABLE at 127 (legs E and
  F, with the per-draw device pair added it did not move); the plugin dumped
  all 238 again during the boot (all files' mtimes at 23:11, before the first
  census report) - my same-run counter captured its start at the first report
  and read 1: fixed to capture at static init. The residual 111 is stable;
  whether it is "set but never drawn with" or objects the extractor rejects
  silently is decided by the rejection counter added for leg G.
- 23:19 leg G: full run, 60.0 fps. Same-run reconciliation with the counter
  fixed: PLUGIN dumped THIS RUN 83 VS + 155 PS = 238; feed 127 (53/74);
  extractor rejections 0 (attempts = the 171 arrivals). Residual 111, stable
  across E/F/G, not the extractor -> outcome (b) "translated at IM_LOAD but
  never bound at a draw in 2 min of Bowerstone" is the LEADING reading; the
  per-draw device-pair reads had no did-it-run counter in this build, so
  "stable at 127" could not yet exclude "the new path contributed nothing" -
  leg H carries the counters (arrivals / null / duplicate / new, value-
  independent). 59-slot register census: 36 seen (all SDK-named), off-census 3.
- 23:25 leg H: full run. Did-it-run for the per-draw shader-pair feed:
  32,734,841 object arrivals over 4,200 presents = 16,784,864 null + 15,949,806
  duplicates + 171 new (sums exactly; nothing dropped). The path ran and
  contributed no shader beyond the hooks' 171. Nulls are 51.3% of reads - the
  shape of ONE FIELD ALWAYS NULL plus a 2.5% tail on the other (claudecode-76's
  arithmetic), which would also explain why the 111 missing shaders are mostly
  pixel shaders (if +0x3194 never yields, the feed sees VS only). Leg I splits
  the nulls by field. XdkDeviceState row live: polygon-offset scale/bias
  pairs change 8,172 times in 2 min and the native path does not apply them
  - THE FIRST PREDICTED DEFECT WITH A NON-ZERO MAGNITUDE IN BOWERSTONE
  (predicted symptom before any fix: z-fighting on coplanar surfaces - decals,
  shadow acne, ground clutter flickering against the terrain; look for it in
  the native shot first, so the fix is demonstrated, not asserted).
- 23:30 leg I: full run, 60.0 fps p99 19.4 ms 0 hitches.
  **Null split (the one-field hypothesis is DEAD):** 58,808,325 arrivals =
  30,314,642 null + 28,493,512 duplicates + 171 new; the nulls are VS field
  12,655,415 (21.5%) + PS field 9,617,639 (16.4%) + ~8.0M SetShader(null)
  hook calls. BOTH device fields go null on real draws; at the menu neither
  ever does (37,868 arrivals, 0/0). So the 111 shaders the plugin translates
  and the feed never sees are bound through a path that leaves the device
  fields null (setters the hooks miss - the UI among them, per the earlier
  session). The hash-confirmed IM_LOAD path is the fix.
  **M5 COHERENCE, MEASURED on all 127 shaders (verdict for tomorrow's #2):**
  memexport 1 shader (0D7251C284702D9B v - the one WRITER through the mirror);
  register/constant dynamic addressing 17; vertex fetches 154, of which 80
  index from r0.x and **74 from another register (runtime-computed) in 19
  shaders** - three shaders fetch 12 of 13 that way (3A0F9098B839DDBC,
  82F6433A69263C75, D4D558DA6A82BDC8: skinning-palette / terrain-factor
  reads through vfetch, most likely). So "every read is index-derived" is
  FALSE for Fable. The model that survives: a vfetch reads within its FETCH
  CONSTANT's declared buffer (base + size; the hardware clamps to it)
  whatever its index, so the read-set is bounded by the declared ranges,
  which the draw hook has - predict-and-upload BY DECLARED RANGE (not by
  index range) is sound for reads, provided the declared sizes are right
  (the earlier "vertex extents DECLARED vs MAXINDEX" instrument measured 0
  short). The single memexport shader needs a write path (a readback of the
  exported range after the draw, or a native equivalent). Cost: uploading
  the whole declared range instead of the index-touched subset. Not
  measured: whether declared ranges are wildly larger than what draws touch
  (the extents instrument prints DECLARED vs COUNT MB - read it tomorrow).
  Second native shot (23:31:47, saved): the ground covered in RED jagged slabs
  where the 23:04 shot had it black, same place and config. **Two shots 27
  minutes apart are TWO OBSERVATIONS OF WRONG GROUND, not evidence of
  flicker**: lighting, weather, NPCs, LOD and animation all differ between
  two world moments, so "wrong in two different ways at two different times"
  fits as well as instability. Only a burst of consecutive-frame shots in one
  run can separate those; the polygon-offset symptom stays a prediction until
  that burst is taken.

**A class, not a fifth incident: composite keys collide.** The ratio tally
packed the stream into the surface's top nibble and read 14010500 as
"04010500 s1" (2026-09-23 07:52) - the fifth key-or-mask collision in this
project after the `& 15` census, the `recent[q & 31]` lookup and their kin.
Rule: every composite key gets its fields checked for overlap the moment it
is written; prefer a tuple key to bit packing.

**Open after leg X (07:52), in this order:** (1) the guest's Clear
(D3DDevice_Clear, unhooked; the targets still accumulate); (2) the resolved-
surface lookup: 0 direct-key hits against ~3,200 fetch-address matches per
frame, so the address match IS the path and its correctness has never been
isolated - now that targets are populated a wrong match hands a draw a stale
or coincidentally equal surface (a live wrong-colour candidate); ask what a
wrong match would look like from the outside; (3) the 3,900 scene draws per
300 frames with need/declared below 0.05: a live 32-bit-index candidate
until the fresh/stale cross-tab reads; (4) the 9% fresh set's (VS, surface)
distribution against the stale 91% before the 16-bit 8in16 guess is called
validated for the stale draws; (5) the 0.03% fresh records with endian 3.

## MORNING 2026-09-23, fourth stretch (07:52-08:15): resolves live, the cost measured, the frame budget named

- **Leg X (resolves LIVE, freshness in packet terms):** ~11 native resolves
  per frame against the hook's ~10 (5 full-width per frame: 14000500 fmt 2,
  14010500 fmt 32 x2, fmt 23 depth, 14000500 fmt 6; plus 6 impostor
  256x256); the resolved-surface sampling line appeared where it had never
  printed (0 direct-key hits, ~3,200 fetch-address matches per frame - the
  address match IS the path, correctness never isolated). Freshness: 9% of
  draws have a fresh record (35k matched with the start offset, 22k base
  only, per 300 frames), 99.97% of them (16-bit, 8in16); the object's
  Common word is 0x20400000 for all and its Size top byte 00 under every
  endian, so the OBJECT carries neither width nor endian where read - that
  route is closed; the fallback stays a labelled guess for the 91%, with
  the (VS, surface) fresh-vs-stale distribution and the <0.05 cross-tab
  built to bound it. Refusals 0; 2,130 draws/frame. The need/declared
  ratio line found ~3,900 scene draws per 300 frames below 0.05 - a live
  32-bit candidate until the fresh/stale cross-tab reads (a stale draw is
  16-bit by fiat, so its absence of idx32=1 is by construction).
- **Leg Y (within-run ON/OFF pairs, GPU sampled beside):** resolves cost
  ~2 ms of guest-thread frame time (four pairs, -2.8 to -5.5 fps at ~45,
  same sign), host GPU frame unchanged at 6.85-6.90 ms. Fence waits are a
  FIXED ~4.4 ms per frame under both conditions (7 submissions + ~70
  readback landings), not the resolves. **Clause A headline: the GPU has
  headroom (6.9 of 16.67 ms); the gap is guest-thread CPU work in the
  hooked path (~10.7 ms draws incl. the 4.4 ms of fence waits, + ~2 ms
  resolves).** Full table in PERF_BASELINE_2026-09-22.md.
- **VRAM climbed 8.0 -> 18.5 GB in 2.5 min and belongs to THIS process**
  (the sampler read 6.4 GB flat after the game exited): ~67 MB/s, no
  plateau - a clause-C blocker until attributed. Resolve textures are
  cached (33 creations per run) - not it. The native texture cache made
  14,813 objects (14,540 re-uploads) in 150 s on a live set of ~300; each
  re-upload retires its predecessor and BeginFrame frees the retired list
  after the fence, so freeing is by construction - what is missing is a
  LIVE count and the one-thing-differs control (`ngpu_shadow=false`, same
  sampler) that attributes the climb to the native path or to the plugin's
  own cache in the same process. Both built / planned as leg Z and Z0;
  re-uploads histogrammed by address to tell streaming from page-
  granularity over-triggering.
- Timer inside ResolveNative (lookup vs copy) and the resolve-count line
  ungated for the hooked path; committed d41361b before leg Y (local).

- **Leg Z (08:10, the cross-tab build - the timer/LIVE builds had FAILED
  silently: clang prints "error:", my check grepped "error C"; fixed, and a
  build is now verified by a log line only it can print):** the 32-bit
  candidate for the low-ratio draws is EXCLUDED on the population that can
  exclude it - of 3,896 scene draws below need/declared 0.05 per 300 frames
  (all BCC6E2DE on 14010500), 139 had a FRESH record and every fresh record
  in the run reads idx32=0 endian=1: small ranges on a large shared vertex
  buffer, not a mis-decode. Fresh share by (VS, surface) runs 0% (C27115F5
  on 04000140, 1 of 3,900) to 34% (6B210267), depth pre-pass keys ~2%: the
  fresh set is not a representative sample, but every busy key has fresh
  members and all 46,619 fresh records are (16-bit, 8in16) - the guess is
  supported per population wherever checkable, weakest on 04000140.
  Resolves 8,640 per window = 480 depth + 7,800 impostor-size + 240 not
  format 6 + 72 NAMED THE SWAP SURFACE (+48 full-width colour); the swap
  surface is learned every window, so `ngpu_present_swapped` can be tried.
  VRAM (total minus the 6.4 GB after-exit baseline): 11.1 -> 24.9 GB over a
  124 s world = 111 MB/s; 1.07 MB per creation where the creation count
  survived log rotation. nvidia-smi's compute-apps query lists no D3D12
  process; the after-exit control is the attribution. Z0 = the same with
  `ngpu_shadow=false`.

- **Leg Z0 (08:12, `ngpu_shadow=false`, same build, same sampler): the
  control that attributes the VRAM growth.** Total VRAM 9,369-9,373 MiB for
  the whole two-minute world, flat to 4 MiB over 25 samples, 6,382 after
  exit; guest 60.0 fps throughout. With the native path on (Z): 11.1 -> 24.9
  GB and 43-45 fps. So the ~100 MB/s growth is the native path's; a 32 GB
  card exhausts in under four minutes of Bowerstone. Clause-C blocker with
  an owner (a subsystem), not yet a line. Enumeration by grep of every
  creation site on the native path (the three remembered candidates all
  dissolved on reading: resolve textures are cached, Plume frees through
  D3D12MA on every path, g_rts is keyed by the three RT registers): 11
  createTexture sites (texture cache 2D/cube, 4 init placeholders, the init
  depth target, the resolved cache, the native RT colour+depth pair on
  (re)creation, the per-target DEPTH DEPENDENCY texture with its own
  framebuffer), 10 createBuffer (init, shot, texel probe, dump readbacks),
  3 createFramebuffer, bounded pipelines/shaders. Counters at the per-frame
  sites, live counts (cache map, resolved, targets, depth deps, retired) and
  D3D12MA's own statistics (allocations vs blocks vs local usage) print with
  the textures line from build 08:2x; leg Z2 reads them. The `[gpu] fence
  waits` line is the PLUGIN's logger: the 4.4 ms of fence waiting is the
  plugin's, present under both conditions, withdrawn from the native budget.
  60 -> 44 fps is the ADDITIVE cost of the hybrid (both paths live), not the
  native path against the plugin.

## MORNING 2026-09-23, fifth stretch (08:20-08:40): THE VRAM LEAK NAMED TO THE LINE

**Leg Z2 (three instruments in one run):** texture cache map 258-274, resolved
textures 33, native render targets 13, retired 2-3/frame - all bounded.
Depth dependencies live across the 13 targets: 119 -> 3,755 -> 10,091
(88-119 created per 60-frame window, none evicted). D3D12MA's books: live
allocations 447 -> 4,098 -> 10,434, 683 MiB -> 15,935 MiB, blocks tracking
allocations one for one; local usage 3.5 -> 18.9 GB; nvidia-smi 6.8 -> 23.7
GB in two minutes. 9,987 extra allocations against 9,972 extra dependencies;
1.56 MB each. **The objects are held at the object layer: `rt.deps`.**

**The keys are garbage, and their source is the retired ring parser.** The
created keys on surface 05000140 run 542 542 542 542 1229 1229 1229 1229
3413 ... 4095 0 0 0 2048 ... - arbitrary 12-bit values, the same sequence on
every surface, in runs of four, never repeating (1,250 distinct of 3,144
creations on one surface). The device's own RB_DEPTH_INFO, printed per draw
by the hooked listing, takes four sane values all morning (bases 0, 0x400,
0xE0, 0x40; the cvar's "eight depth bases" comment is off by two, not a
thousand). The hooked draw path passes the device's register to GetRT;
ResolveNative looked its target up with NONE, and GetRT then took
`g_ring.regs[0x2002]` - the ring shadow - and created a surface-sized D32
texture plus framebuffer for every value, on each of the ~4 full-width
surfaces it resolves per frame. Same class as the index decode: a retired
instrument wired into a live path, this time as a KEY.
**Fix (build 08:4x):** GetRT takes the depth register from the device when one
is in hand, from the ring only when replaying, and with neither source looks
up but never creates (counted). An LRU cap (`ngpu_depth_deps_max`, default 0
until the per-frame working set is measured with sane keys; its too-small
symptom is in the cvar text) stays as the net, evictions counted on the
always-on LIVE line. Predictions for Z3: creations per window ~117 -> ~0
after warm-up, live dependencies in the tens, evictions 0 at cap 64,
D3D12MA allocations and VRAM flat after the first minute.
**Also from Z2:** ResolveNative costs 19-20 us per call, 0.28 ms per frame
(8,640 per 600 frames, ALL performed - the depth/smaller/format counters are
categories, not refusals; my earlier reading withdrawn), so ~1.7 ms of the
toggle's ~2 ms lies outside it: a named gap. Leg Y's own per-window fence
lines: the plugin's impostor readbacks are ~54 per frame in ON and OFF
windows alike, so native resolves induce none of that; the O-to-Z doubling
(20 -> 54 per frame) tracks the native draw count. The mean-created-size
instrument is PARTIAL (171 KiB first window, 0 after: a re-upload path it
does not see) - not quoted.

**Ring-consumer audit, whole file, replay off (08:45).** Hits by enclosing
function: BridgeLogReplay 73 (replay only), BeginFrame 10 (the bool/loop
offset finder, diagnostic), RingParseWalk/RingSnapshot/RingCensusNote (the
reader itself), DrawTranslated 6 (index record - fixed; VS/PS constants -
replay-only / cvar off; a log), UploadIB 5 (replay-only), ReadDrawState 5
(ngpu_ring_states off), GetTexture 5 (draw_at_exit off), Ring{Vertex,Pixel}
Shader (null unless replaying or draw_at_exit), ResolveNative 3 (a log; the
depth key via GetRT - FIXED; RB_COPY_CONTROL - FIXED: ring read only when
replaying, its clear claim counted), ShadowDrawUP/FlushDeferredUP (UP path,
off), NoteLibraryDraw/RectF/EndFrame (logs), GetRT 1 (FIXED). Reg(dev, X)'s
ring fallback: no call site uses an X outside the device table (checked by
extraction).
**LIVE AND UNFIXED - a named census subject: BOOLEAN AND LOOP SHADER
CONSTANTS.** `ngpu_bools_ring` (default on) takes b0-b255 and the loop
constants of EVERY hooked draw from g_ring.regs[0x4900..0x4927]. The finder
line the code prints shows what is there: "ring 4900 = 000F41C0 3CBB4DBA
3CBB4DBA 3CCD1674 00000000 43200000 42E00000 00000000" - FLOATS (0.0229,
160.0, 112.0) in the boolean slots: the retired parser writing float data
into the bool registers; another window shows 00000110. Translated shaders
branch on bits of these (NGPU_BOOL(n)), so a share of every frame's draws
run branches chosen by garbage. The device-side guesses (ngpu_bool_off 0 ->
FFFFE01F..., ngpu_loop_off 0x17A0 -> zeros) are wrong too, and the finder's
scan of the device reports "bools at -1, loops at -1": the device location
is UNKNOWN. Fix path: find the XDK's SetVertexShaderConstantB /
SetPixelShaderConstantB entry points (the trace table's unnamed dirty-bit
setters are candidates) and shadow them ourselves, or locate the device's
copy. Queued behind the leak (Z3/Z4) and the clear.

**Queued: the numbers-in-comments pass.** present.cpp carries 59 quantitative
claims in comments or cvar text (percentages and "N of M"); one is known to
have been measured on a different population than the one it was quoted
against ("3.2% of Fable's recorded draws land here" - replay draws, quoted
at the hooked path, where the truth was 30-50%). The "eight depth bases"
comment was off by two, not a thousand - the instrument, not the comment,
was wrong there. Each of the 59 gets marked measured-here / measured-
elsewhere / unknown, with the population named. Not started.

- **Leg Z3 (08:31, the key-source fix, cap 64 from the tune):** depth
  dependencies created 0 per window after the first 16, live 16, evicted 0;
  per-frame working set max 2 (04000140, 05000140, 14000500), 1 elsewhere;
  D3D12MA 360 allocations / 541 MiB flat; VRAM 9,949-9,957 MiB flat; guest
  49.0-49.8 fps (46.0-46.5 in Z2's last windows; xs 9.6-10.5 vs 9.8-10.9 ms
  and host 6.8-6.9 ms in both, so the +3 fps sits in no instrument - unbanked).
  The "10 of 13 targets reported" was the instrument keyed by surface while
  targets are (surface, colour): ten surfaces of ten; re-keyed. Default cap
  16, committed 50151ba. Z4 at defaults = the confirmation quoted.

**A habit, not a sixth incident: when a keyed counter is written, state what
the key identifies and check it against the identity of the thing counted,
before the first number is quoted.** Today: the ratio tally packed the
stream into the surface's top nibble; the working-set tally keyed by surface
while targets are (surface, colour) ("10 of 13" was ten of ten). Five of
the six key/field mismatches in this project would have died at the
keyboard under that check.

- **Leg Z4 (08:37, shipped defaults, cap 16): the confirmation quoted.**
  Created 0 / evicted 0 every window, 16 live, D3D12MA 354-356 allocations
  = 541 MiB flat, VRAM 9,972-9,976 MiB flat, 48.8-50.7 fps. ResolveNative
  now 3 us per call (was 19-20: the creations were its cost, ~0.23 ms/frame,
  gone). **A fifth reason the resolve-live pictures (X, Y, Z, Z2, Z3) were
  unjudged:** the ring shadow's RB_COPY_CONTROL asserted a clear bit on 1,266
  of 8,640 hooked resolves per window (15%), and "targets cleared because a
  resolve asked for it" printed 2/9/13 times in X/Y/Z3 - targets were being
  cleared at random on the retired parser's word. Fixed in this build (ring
  read only when replaying; the claim counted).
- **The guest frame, decomposed as far as it can be today (Z4: 20.3 ms):**
  native xs bracket ~10.0 ms; the plugin's fence waits (~4.1 ms) are on the
  plugin's OWN thread, not the guest's, so they do not add; the remaining
  ~10 ms of guest-thread time is unbracketed: the game's logic, the recomp's
  runtime, and native work outside the xs timer - of which RingAdvance,
  run on every hooked draw before DrawTranslated and walking 30-37,000
  packets per frame for the retired parser, is the named candidate with a
  one-cvar test (`ngpu_ring_parse=false`, leg P1). A frame decomposition
  that CLOSES (every millisecond named, an OTHER bucket equal to frame minus
  the parts) is the instrument after it.

**Leg P1 (`ngpu_ring_parse=false`), predictions written before reading.**
It moves TWO things - the parser's per-draw guest-thread cost goes, and the
bool/loop source moves from ring garbage to device-guess garbage - so it is a
PERF and COUNTER measurement only; its picture is comparable with nothing.
Positive branch: guest fps rises by the parse cost (2-4 ms would read ~55-60
fps), LIVE / D3D12MA lines unchanged, refusals still 0, fresh index records
0 (the fallback decodes everything). Null branch: guest fps unchanged means
the 30-37,000-packet walk is not in the ~10 ms residual - cheap per packet
or overlapping work the guest does anyway - and the decomposition is
redirected, not refined.

**Why the picture stayed unjudged all morning, for the next reader who is
tempted by a screenshot:** the decision was justified FIVE separate times,
each by a mechanism nobody knew about when it was made: (1) the target
never clears (accumulation); (2) 30-50% of the frame's draws refused;
(3) the rest decoded from a stale index record; (4) the shot's subject
chosen by a busiest-target fallback; (5) targets cleared at random on the
ring shadow's copy-control word for every resolve-live leg before 08:36.

- **Leg P1 (08:42, `ngpu_ring_parse=false`): the parser costs ~1.7 ms of
  guest frame.** 53.1-54.2 fps (p50 18.4-18.7 ms) against Z4's 48.8-50.7
  (~20.2 ms), same build, same scene; xs 9.7-11.6 ms and host 6.7 ms
  unchanged (the cost is outside the bracket, as predicted); LIVE 16,
  D3D12MA 359 / 541 MiB, refusals 0 unchanged; fresh index records 0 (all
  fallback, which the earlier legs showed identical to the fresh decode
  99.97% of the time). Perf-only; the picture is comparable with nothing.
  Disposition: flip the parser's default OFF on the hooked path after B1
  (consumers enumerated: fresh index records -> the same decode; bool source
  -> from one garbage to another; ring census feed, UP path already off; the
  replay enables it itself), measured as a default-config leg.

## MORNING 2026-09-23, sixth stretch (08:44-08:55): THE BOOLEAN CONSTANTS MOVE A FIFTH OF THE FRAME

**Leg B1** (bool source toggled every 600 frames: even windows the ring
shadow, odd the device-shadow guess; 13 null schedule entries so the shots
pair at F-1/F+2; per-present clearing ON for this leg only; the RT dumps
did not write - RecordRTDumps has its own `every <= 0` gate, the seventh
site of that class, fixed after). Thirteen shot pairs, all pose-accepted,
3-frame gaps. Mid-window nulls: 0.9-7.2% changed (mostly ~1%). Every pair
straddling a switch: 19.1-20.3% changed, and the class deltas ALTERNATE
with the direction - ring -> guess: lavender -8.4/-6.5/-4.8/-3.5, green
+9.3/+9.5/+10.0/+10.2; guess -> ring: lavender +7.7/+5.8/+3.9, green
-9.5/-9.8/-9.5. **The canopy is lavender under the ring's booleans and
green under the device guess.** Only 8 of 126 fresh shaders test a boolean
(indices 129/132/136 = pixel b1/b4/b8: 3DC80BD0, EDBD9566, 52F65E01,
AFAA7F34, A9C388A8, 986808B1, D2CC2E1C, 630819653F); the sources differ at
132 (the ring's sane sample 0x110 sets it, the guess 0xFF0013A0 does not).
Five VS use loop i0 (skinning) and one PS i16; the ring's loop words are
floats too.
- Magnitude settled: booleans/loops jump the queue ahead of the clear.
- Value NOT settled: "green looks right" is a picture judgment. The ring's
  0x110 sets exactly the tested bits and nothing else, so it is plausibly
  the game's real state - in which case the lavender comes from the branch
  the TRANSLATION takes under b4, not from a wrong constant. The next step
  is the SOURCE: locate the device's boolean shadow (search for a sane bit
  pattern, or catch the writer among the trace table's dirty-bit setters),
  compare its bits with the ring's sane samples; agreement at 132 makes the
  b4 branch's translation the subject, disagreement makes the ring wrong.
- The floats in the bool slots look like a RANGE mis-decode in the parser
  (0x4000-range float data written over 0x4900) rather than random garbage,
  since 0x110 also appears there: the values exist in the packet stream and
  "stop clobbering the range" is a second route to a true source.
- With `ngpu_ring_parse` now default OFF (leg P1: ~1.7 ms), the bools are
  DETERMINISTICALLY the device guess (136 set, 132 clear) instead of
  intermittently the ring's - a known consequence, recorded in the cvar
  text; it may make the canopy green by a wrong constant, which is not to
  be banked as a fix.
- Loop-constant symptom, stated before the picture is readable: a garbage
  i0 reads as characters in bind pose or collapsed to a point if it
  resolves low or zero, as a clamp or hang if high; characters looking fine
  under a garbage count is itself a finding about whether the loop constant
  reaches the shader.
- Read the right way round: the SIGN ALTERNATION is the finding (seven for
  seven, the direction follows the switch; nothing that confounds a picture
  comparison is sign-aware with respect to a cvar switch). The magnitude is
  the weaker half: the floor is 0.9-7.2% (median 1.2%), so the ~20% effect
  is ~3x the WORST floor sample, not "an order of magnitude"; the 7.2% floor
  sample (entry 2700) has no named cause (an NPC, a camera twitch, a
  streaming event) and is the number a sceptic picks at. The lavender
  deltas also shrink monotonically across the run (-8.4, -6.5, -4.8, -3.5;
  +7.7, +5.8, +3.9): the effect's SIZE depends on something moving over the
  stand, so "a fifth of the frame" is one stand's snapshot, not the bit's
  property. Next instrument: a FORCED-BIT knob (pin index 132 set, then
  clear, everything else unchanged, toggled within one run) so the lever is
  demonstrated on the bit rather than inferred from two garbage sources.
- Habit, the pair this project keeps re-learning: when N sites are fixed,
  establish that N is the count by grepping for the BEHAVIOUR (an early
  return on a cadence or a zero knob), not the idiom that was written; the
  cadence class had seven members, the IsDumpFrame pass reached five.

**The dangerous cvars in this project are the diagnostics that improve the
picture.** Three times today the tempting argument was the same - it looks
better, ship it - and three times the answer was no: `ngpu_rt_clear` (a
per-present wipe is not what the console does), `ngpu_range_clamp` (issued
the refused draws with zero-filled vertices: a D3D12 guarantee, a Vulkan
feature flag, and it painted stretched garbage), and the forced boolean bit
(makes the canopy whichever colour one prefers). A knob that improves the
OUTPUT without establishing the INPUT is a defect wearing a fix's clothes;
a diagnostic graduates to a fix only when the value it supplies has a
verified source.

- **Leg D1 (08:50, shipped defaults: parser OFF, cap 16, resolves live,
  ring copy-control ignored):** 52.1-53.0 fps (p50 18.8-19.1 ms), xs
  10.1-11.2 ms / 2,087 draws, refusals 0, depth deps 16 live / 0 created /
  0 evicted, D3D12MA 360 / 541 MiB, VRAM 9,960-9,982 MiB flat. Clause A at
  defaults: 18.9 ms guest frame = ~10.5 native xs + ~8.4 unbracketed; GPU
  6.7 ms; memory bounded. Picture caveat: canopy green on an unverified
  boolean. (The re-keyed working-set line read "16 of 13" - raw colour word
  vs the target map's masked key - re-keyed by g_rts's own key.)

## MORNING 2026-09-23, seventh stretch (08:53-09:00): THE LEVER IS ONE PIXEL-SHADER BOOLEAN, DEMONSTRATED ON THE BIT

- **Leg B2 (08:53; build_B2 errors 0, linked 1; the build is proven by its
  own line - "per-frame working set reported by 13 of 13 targets", which
  only the re-keyed build prints):** `ngpu_bool_force=132` toggled every
  600 frames (SET / CLEAR alternately, ten windows), everything else at
  the D1 defaults (parser OFF, resolves live, depth cap 16), plus
  `ngpu_rt_clear=true` so the pairs compare frames rather than
  accumulations, dumps at the 13 schedule boundaries (F-1 and F+2).
- **Result on the scene surface 14010500 (dump_pairs_read.py):** the seven
  pairs that straddle a switch changed 20.3, 20.4, 20.4, 20.4, 21.0, 21.1
  and 20.4 % of pixels; the six pairs with the same state on both sides
  changed 3.2-4.6 % (mean 3.9). Seven for seven above the floor, and the
  magnitude is FLAT across the stand - B1's source toggle shrank
  monotonically (-8.4 to -3.5), the pinned bit does not - so the ~20 % is
  the bit's property on this stand, not the drift of two garbage sources.
- **What index 132 is:** the boolean constants are 256 bits, 0-127 the
  vertex shader's and 128-255 the pixel shader's; 132 is pixel-shader
  boolean b4 (word 4 of the block, bit 4). One pixel-shader flag moves a
  fifth of the frame.
- **What this does NOT settle:** which value is TRUE. The forced knob is
  the third "diagnostic that improves the picture" of the day; the ring
  shadow holds floats in the bool slots and the device location is
  unknown. The exit condition for the shading subject is unchanged: the
  SOURCE of the booleans, with a verified value for b4.
- B2's frame rate (45-49 fps, p50 20.8-21.0 ms) is NOT a perf reading:
  rt_clear and dumps are on. It sits beside D1, never against it.

- **Leg C1 (09:02, native OFF, vsync OFF, divider 1):** 60.0 fps in every
  window (301 swaps / 5 s), host paint 204 fps. Both named cap mechanisms
  off and read back from the plugin; the guest still holds 60. The pacer is
  a THIRD mechanism on the guest side (a clock-based limiter). Full entry in
  PERF_BASELINE_2026-09-22.md "Leg C1". Next: name the guest wait site.

## MORNING 2026-09-23, eighth stretch (09:02-09:30): THE SLACK MEASURED, THE FRAME DECOMPOSED, THE PROFILER CAUGHT TWICE

All entries in PERF_BASELINE_2026-09-22.md ("Leg C1", "THE DECOMPOSITION THAT
CLOSES", "Leg P2"); the short form:

- **C1 (native OFF, vsync OFF, divider 1): still exactly 60.0 fps** in all
  twelve windows while the host paints 204 fps - a third pacer. Both named
  mechanisms were read back from the plugin and the SDK source, and the
  workload guard (readback landings per swap 71.8 vs D1's 72.1) held. The
  vsync-off vblank claim's version skew was checked and closed (the worker's
  file unchanged since v0.10.0, DLLs dated 09-22); M5's read side likewise
  (the translator is vendored from the plugin's build commit and compiled
  into the app).
- **The port's own sampling profiler (FABLE2_PROFILE) is the decomposition
  instrument** - at defaults, no clock touched, guest-function attribution.
  P0 (native off): the render thread spins 56-58 % of its frame in the D3D
  runtime's GPU-progress poll sub_82BA1FA8 = **9.4-9.7 ms of slack, MEASURED**
  (the derived ~8.7 was 1 ms low); game busy ~6 ms once the census hooks
  (~1.0 ms) are taken off its side. P1 (native on): the poll is 0.0 %, the
  frame is all busy, game unchanged, native path ~12.5 ms scaled. Parts sum
  to D1's direct 18.9 (6.0 + 12.5 = 18.5). **Clause A's gap: ~2 ms of
  native-path CPU on the render thread**, two routes agreeing.
- **What sub_82BA1FA8 polls (read, not inferred):** the GPU's ring read
  pointer via a device-object pointer, "keep waiting" while it advanced
  within 5000 ticks, then the hang check. Its caller waits for the command
  processor, so the third pacer is on the CP side; P2 places the CP's waits
  at the cap (submission fences ~18 %, resolve readback copies ~13 %, page
  requests ~10 %, WAIT_REG_MEM ~5 %); P3 (vsync off, exact time) names it.
- **The instrument caught twice, both by its own output:** (1) "sub_832BD218"
  at 65.7 % with offsets megabytes past any function and stacks in
  nvwgf2umx - the guest table's extent was open-ended past its last host
  address, so ALL app code in every profile this tool ever produced was
  classed as guest and symbolised to the nearest sub_ (reaches back to the
  09-12 "60 % in the poll" figure); (2) "on CPU 100 %, blocked 0 %" for a
  GameThread with 64 % of samples in its NtYieldExecution wrapper - the
  per-sample ran/blocked verdict aliases with 1 ms sleeps and yields.
  Fixed (build 09:21, proven by its own startup line): scheduled CPU time
  per window from GetThreadTimes plus summed cycle deltas, and the guest
  range bounded at the app's own code, both logged.
- Rules re-learned: a cap-removal measurement needs EVERY cap mechanism named
  in the log before a null is read (C1 had two named and still nulled); the
  decomposition at defaults beats removing a cap, because removing the cap
  changes the workload; a profiler label is a claim about the SYMBOL TABLE
  before it is a claim about the code.

### The numbers-in-comments census (2026-09-23 09:35, subagent, read-only; report in the session scratchpad as comment_numbers_census.md)

**In `src/native_gpu_present.cpp` ALONE: 164 in-scope quantitative claims in
comments - 35 NAMED (the comment names a leg, date, log line or doc section),
10 TRACEABLE (the figure appears with a source in the docs), 117 UNTRACEABLE
(71 %), 2 CONTRADICTED by the record.** Scan totals: 3,806 comment lines,
1,171 with a digit, each triaged. The codebase-wide count is UNKNOWN - 164
is one file's number, not the total.

The finding is the ratio, not the two contradictions: the two wrong numbers
("eight depth bases"; "3.2 % of draws" quoted at the hooked path where the
figure was 30-50 %) cost 30-50 % of the frame and ~11 GB of VRAM before they
were found - BY MEASUREMENT, not by this audit. The audit's value is that it
names 117 more numbers carrying the same exposure. Disposal: marked in place
as unsourced rather than deleted (a number with "provenance unknown" beside it
is usable with care; a silently removed one takes its context with it; a
silently kept one lies). The "3.2 %" is corrected as a population swap
(provable without a scene); the "eight" is re-scoped, not flatly contradicted:
FOUR sane depth bases were observed in the runner's Bowerstone stand on
2026-09-23, and a count measured in one scene is a hypothesis about every
other (coordinator, 09:40).

### 09:34-09:50: the native path NAMED, and its hottest two instructions are reads from write-combined memory

- P6 (line tables for the app sources, PDB) names the ~12.7 ms: one chain
  from one guest site (the hook thunk at the game's draw-issuing function
  57.7 % -> OnDrawIndexed -> ShadowDrawIndexed -> DrawTranslated 54.5 %);
  self time SharedConstantsFor 16.8 %, DrawTranslated 10.3 %, ReadDrawState
  7.5 %, GetTexture 4.7 %, driver ~7 %.
- The 16.8 % is TWO ADJACENT INSTRUCTIONS (+0x4266/+0x426d, 14.7 % of all
  samples): the disassembly at the sampled addresses is the pack line
  `blk[128] = (blk[136] & 0xFFFF) | (blk[140] & 0xFFFF) << 16` reading our
  own block back out of the D3D12 UPLOAD HEAP - write-combined memory, ~700
  ns per load. 2.9 ms a frame from two loads; the clause-A gap is 2.2.
  The line table had pointed one statement off (the LoadV loops), and that
  wrong story fit the profile perfectly. Full entry: PERF_BASELINE "THE HOT
  PAIR DECODED", with the prediction written before the fix is measured.
- Class fix in flight: mapped heaps become a write-only type
  (src/native_gpu_wc.h) so a read is a compile error; the block is assembled
  in a shadow and stored once. Measured by P7 (busy terms: the function's
  self share, the thread's scheduled ms, the GPU-progress poll returning
  above 0 %) and D2 at defaults (60.0 = the gate met; the limiter clamps, so
  fps is the gate reading, not the measurement).
- The census subagent that produced the comment audit was later sent
  messages this session never saw ("leaves ... seen on the native window",
  "continue with the implementation") and replied asking for scope; whoever
  sent them should re-send to this session if they were meant here.

### 09:50-10:05: THE FIX MEASURED - 60.0 AT DEFAULTS IN THIS SCENE

- Class fix landed (build 10:01): `g_s.upload_map` and `g_s.cache_map` are
  `ngpu::WcHeap` (src/native_gpu_wc.h) - element stores through
  `WcSpan<T>`/`WcRef<T>`, bulk `store()`, four named `store_ptr()` sites
  (texel rows, per-element conversion, the cache allocator, per-vertex UP
  writes), eight named `slow_uncached_readback()` sites (one-shot dumps,
  the 300-frame probe and log, the rect expansion's source read, the
  tessellation comparison, the composite-draw constant log). The compiler
  found the one read the grep had missed (the composite diagnostic reading
  pixel constants back). SharedConstantsFor assembles its block in a stack
  array, stores once, keeps a CPU shadow (the ps_debug modes read that),
  and counts distinct results.
- **P7 (profiled): SharedConstantsFor 16.8 % -> 4.5 %, frame 19.8 -> 17.8 ms,
  xs 8.8-9.2 ms. D2 (defaults, no profiler): 60.0 fps, 300-301 swaps in
  every steady window, p50 16.6-17.0, 0 hitches - where D1 read 52.** The
  limiter clamps; the gate's rate reads as met in the runner's Bowerstone
  stand. One scene; the town and the lake are owed. Full entries in
  PERF_BASELINE: "THE HOT PAIR DECODED", "RESULT (leg P7)", "GATE READING
  (leg D2)".
- Not a regression but its own item: a deterministic 3.2-3.4 s frame ~35 s
  into the stand in D1, P6, P7 and D2 alike (streaming; the render thread
  48 % in ntdll waits during it). The stability clause fails on it here.
- The distinct-results counter: ~2,070 blocks a frame, ~60-100 distinct,
  95-97 % non-adjacent repeats. A per-frame input-keyed cache is possible;
  its ceiling is the remaining 4.5 % (~0.8 ms). Candidate, not queued.
- Next self-time order on the render thread: DrawTranslated 13.4 % (its top
  leaf the byte-wise endian swap into the heap at present.cpp:7538, ~3.3 %
  - byte stores into write-combined memory; swap in a stack buffer and
  store whole lines), ReadDrawState 9.4 %, GetTexture 4.9 %.
- The comment census's 117 unsourced claims are marked in place
  (`[unsourced, pre-2026-09-23]`, 125 markers, comment lines only, verified
  by a diff filter); the two contradicted ones corrected, the "eight depth
  bases" re-scoped to "four observed in the Bowerstone stand".

## MORNING 2026-09-23, ninth stretch (10:15-10:35): the gate bar decided, the rule applied, the boolean source located

- **The user's decision (relayed by claudecode-76, 10:20): PARITY WITH THE
  PLUGIN is clause A's perf bar.** The p99 tail and the 3.2 s streaming
  hitch are inherited defects, recorded, not blocking. Next: prove parity in
  the town and the lake, then back to correctness.
- **The parity rule, written before any leg, then applied in Bowerstone
  (PB_off1-3 / PB_on1-3, n = 3, interleaved):** rate, p50 and hitches read
  as parity but are the CLAMP reading itself (empty rows); the tail, one
  statistic, reads WORSE by one spread width (p99 medians 19.1-19.2 vs
  19.4-19.6); worst not separated. "0.3 ms" is a reading to retake, not a
  debt. Pre-registered: under the outlier hypothesis (which the table
  leans to) a per-draw fix does NOT move p99.
- **P8: the swap-shadow fix landed on both halves** (old leaf gone, a 1.0 %
  store leaf appeared, DrawTranslated -2.1 points); p99 unchanged as
  pre-registered. Next instrument built: every native frame over 18 ms
  dumped with its own counters plus the count per window.
- **CS1: the booleans and loops travel as ONE inline SET_CONSTANT of type 2,
  index 0, 40 dwords (8 bool + 32 loop), at least ~195 a frame (the walk
  sees ~one packet in seven, so plausibly per draw).** The "floats in bool
  slots" of the ring shadow is one mis-decoded 2,548-2,948-dword load per
  second - real, self-diagnosing, and 10,000x too small to be the standing
  defect. **The standing defect is at the consumer: at defaults
  ngpu_bool_off = 0 ("they come from the ring") and the ring parser is off,
  so the translator is handed device+0 (the object's header) as booleans
  and device+0x17A0 (inside the PS float block) as loops, every draw.**
  Fifth retired instrument wired into a live path. The finder (40-dword
  value match against the device object, every offset, stability, all-zero
  count) and a consumer-side print are in the source for the next build.
- **10:31: the game handed to the USER** (plugin path, no input, lock
  "USER DRIVING" until 14:31) to make the town and lake saves; not a
  measurement leg. Everything that needs the exe waits; everything else
  continues.
- Commits: 58e0d3b, db780f6, b281127 (local; nothing pushed).

## MIDDAY 2026-09-23 (11:00-12:15): the gate bar decided, the parity rule applied twice, and a user-facing bug that refuses every mechanism

- **The user's decision (relayed): PARITY WITH THE PLUGIN is clause A's perf
  bar**; the p99 tail and the streaming hitch are inherited defects, recorded,
  not blocking. The rule was written BEFORE any leg (both arms, n >= 3, named
  rows, spreads quoted, "not separated" as a legitimate outcome) and then
  applied twice.
- **Bowerstone cemetery (PB legs):** quotable for DIRECTION only - the census
  ran in both arms by cvar but the native arm did more census WORK (644 vs
  ~1,500 census lines), so a cvar value is not a workload. Three of five rows
  were the 60 clamp reading itself back. The tail read worse by one spread.
- **Bowerstone MARKET (PC legs, the gate scene, the user's own save):** both
  arms BELOW the clamp (50-57 fps) so the rows carry information - and the
  verdict is **NO MEASUREMENT OBTAINED**: the invariant moved (landings per
  swap 62.67-62.94 in five legs, 60.36 in one) which voids the table by the
  pre-registered rule, and the plugin arm alone spans 48.4-57.4 fps, so the
  stand could not have separated anything. A finding about the stand, not
  about the native path.
- **The invariant was re-named honestly:** it is BOTH a comparability gate and
  a partial content measure (0.03 % in the cemetery, 4 % in the market), so a
  move cannot alone distinguish a broken harness from a passing cart, and it
  is never the sole gate.
- **Save selection: the menu is a CARD FAN navigated with RIGHT.** A blind
  "downs" script loaded the wrong save (the lake) and then no save at all
  (Download); the vision tools showed the real menu; SAVE_INDEX plus the
  region assertion is the permanent shape. The assertion caught a wrong
  scene on its first outing - six clean legs of the wrong place is the
  failure where every number is correct and worthless.
- **Every leg now records its binaries** (size, mtime, SHA-256 of the exe and
  both DLLs, the build dir and HEAD) in out/<TAG>.artifact.txt.
- **The recompiler's determinism is measured, not assumed:** the build's own
  codegen prints "0 written, 561 unchanged" - a full content comparison
  (WriteIfChanged reads the bytes) with "0 module(s) up to date" proving the
  stamp skipped nothing - 58 times across 29 builds today.
- **The user's upscaled-texture flash (release line, not ours):** five
  mechanisms killed by reading - content-hash substitution (no 1280x720 entry
  among 55,848 files; the trace reads "none" twice), shared staging overlap
  (no shared buffer exists; every upload creates its own committed resource),
  upload budget/bandwidth (the video's bytes never use an upload resource -
  shared memory plus a compute pass), VRAM pressure (26 GB free), descriptor
  collision (per-texture indices, freed against a submission fence), and the
  missing-barrier race (both dispatch sites fence and submit). The attract
  mode is a POWERLESS subject (zero pack activity during it). The mechanism
  is unknown and no fix will be designed against a mechanism we do not have.
  Investigation in the session scratchpad as texpack_video_flash.md.
- **A safety rule had to be corrected:** "the save import is read-only" is
  true of the import and says nothing about the destination - the runtime's
  content root IS the user's save folder and the game autosaves there.
  Running the game with a save loaded writes their saves by design. Both save
  trees are now backed up twice (Documents and D:), verified by SHA-256 per
  file (51 of 51), with a RESTORE manifest naming the two deliberately
  excluded shader-cache files.
