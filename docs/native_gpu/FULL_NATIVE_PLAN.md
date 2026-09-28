# Full native: no command buffer, no Xenos plugin

User decision, 2026-09-27 08:45: "Lets Skip the command buffer entirely, lets not use any type of emulation, we want
to be 100 % native". This is the 09-22 direction ("replace emulation with native calls ... game agnostic layer")
sharpened: the PM4 bridge that 1.1.x ships was a staging post, not the destination.

## Where the time goes today (measured, not estimated)

Fairfax castle stand, shipped 1.1.0 build, xperf 1 ms sampling with stacks (PROF_F1, 2026-09-27 09:30). The plugin's
GPU thread is 100% busy and caps the game at ~37 fps; the card is ~47% busy.

| Share of the GPU thread | What | Removed by full native? |
|---|---|---|
| ~22% (~6 ms/frame) | PM4 parse + the plugin's register writes and bookkeeping + lockstep sync | yes |
| ~78% (~22 ms/frame) | issuing the draws in the native backend (OnBridgeDraw) | no - it moves |

Inside the 78%: UpdateBindings 26%, GetSamplerParameters 20% (12.8 points of it was a lock in the live-cvar mirror,
fixed in 1.1.1: 37 -> 41 fps), RequestTextures 10%, BeginSubmission 7%, CopyToGuestMemory 7%, render-target Update 7%.
The game's render thread spends ~60% waiting on this thread.

**So full native alone is worth ~6 ms at the castle (to ~45-50 fps); reaching 60 also needs the per-draw cost down.**
Both are in this plan; the per-draw work does not wait for the rest.

**UPDATE, tiling census (2026-09-27 10:00): the draw count itself comes down with full native.** Per frame, by the
tiling pass each bridge draw belongs to (bin_select):

| | draws | predicated | 0x80000001 | 0x2 | 0x8 | all tiles | 0x80000003 + 0xc |
|---|---|---|---|---|---|---|---|
| Fairfax | 14,409 | 96% | 3,981 | 4,020 | 3,990 | 1,494 | ~920 |
| Market | 7,891 | 94% | 1,609 | 1,645 | 1,615 | 1,921 | ~1,100 |

The game records the scene once and the console GPU replays it per tile (predicated tiling: a 1280x720 2x-MSAA frame
does not fit the 10 MB EDRAM), so the bridge issues every scene draw three times. A front end at the device level sees
each draw ONCE and tiling disappears (M4_DESIGN: SetPredication -> no-op): Fairfax ~14.4k -> ~6k draws. With the ~6 ms
of parsing gone as well, **P3 is the path to 60 at the castle**, not only a purity goal.

## The measure of "100%"

A census, not a claim. Two numbers, driven to zero:
1. **PM4 packets the game writes that no replaced function accounts for.** Attributed from the ring write pointer at
   the entry and exit of every hooked XDK function (NG2's method: engines that build packets with computed opcodes
   cannot be attributed by call counts).
2. **Draws the native front end did not issue itself** (compared per frame against the bridge, which stays alive as
   the reference until the end).
Then: rexgpu-xenos.dll not loaded, no ring buffer allocated, and the picture compared frame by frame with the bridge
build (the reference) at Bower Lake, Bowerstone Market, Fairfax and a cutscene.


**P2 RESULT, Fairfax castle stand (1.1.2 settings, MSAA off), 2026-09-27 11:40** (docs/native_gpu/p2/
p2_fable_fairfax1_report.txt, tools per NG2's P2_CENSUS_FORMAT): 60 frames, 14,340,291 packets, 607,675 draws -
**METRIC 1 = 0 unattributed packets, 0 unattributed draws.** Draws per frame 10,137-10,144, reconciling with the
tiling census's ~10,069-10,169 (a second instrument). Draw producers per frame (by the draw packets in their cursor
ranges): sub_8221DFC0 DrawIndexedVertices ~5,594, sub_8221D1B0 ~1,803, sub_82221858 SetShader ~542 (packets inside
its range - nesting to resolve in P3), sub_82221B90 LoadShaderConstants ~514, sub_8221C898 ~475, sub_822655F0
SetPredication ~236, sub_8228EF80 ~230, sub_822194B8 ~195, sub_8221B010 ~129, sub_8221C3E8 DrawVertices ~126,
sub_82217DB8 DrawVerticesUP ~92, sub_82205F68 clear ~41, sub_82221740 ~28. 2.47 M packets (17%) come from buffers
recorded before the window and replayed (last-writer map). Guest recorder balanced: 5,482,390 enters = exits.


**P3 FIRST DATA - where a draw's register state lives (register-map discovery, 2026-09-27 12:00; FABLE2_P3MAP,
tools/native_gpu/p3_regmap.py, docs/native_gpu/p2/p3_regmap_fairfax1.*).** 400 guest calls at Fairfax with the
XDK device object (0x5000 bytes) at each call's exit, joined by packet address to 1,600 bridge draws with the plugin's
register file; 616 of 3,368 registers vary. Owners of the joined draws are the right functions (DrawIndexedVertices
sub_8221DFC0 764, sub_8221D1B0 143, DrawVerticesUP 150, Resolve sub_82206888 233, SetShader 126), so the snapshot is
at the draw. Findings:
- **Fetch constants (0x4800+) live in the device at +0x480** - found by the data, as the 09-22 notes said.
- ~~Shader float constants are NOT in the device~~ - WRONG, retracted 12:20: that came from comparing against the
  plugin's final register file. Decoding the packets each draw call wrote (p3_flush.py) shows the flush writes them
  inline as type-0 writes to 0x4000+, and ONE device word explains 412 of 513 constant registers in >= 95% of writes.
- **Render-target / viewport / window registers: the device holds the FULL-SCREEN, untiled values, the plugin's
  register file the per-tile ones** (RB_SURFACE_INFO pitch 0x140 in the device vs 0xA0 per tile; viewport scale
  64.0 vs 128.0) - the device is the natural single-pass source, i.e. tiling disappears by reading it.
- Of the 09-22 contiguous map (0x2000/0x2100/0x2180/0x2200 blocks at +0x2880/+0x28CC/+0x2920/+0x2934), 78 of 144
  registers match in >=95% of draws; much of the 0x22xx tail does not (the contiguity assumption is wrong there).
So P3 is not a copy of the device: it needs the XDK's draw-time flush reproduced natively (which dirty groups it
writes, how it derives the per-surface values, where constants come from). Next: per register class, the flush
function's own code (sub_8221xxxx, recompiled) as the reference, verified draw by draw against the bridge.


**P3 FLUSH DATA (12:20, p3_flush.py on 1,500 calls with the bytes each call wrote, docs/native_gpu/p2/
p3_flush_fairfax2.*):** registers the calls' own packets write, and whether ONE device word explains >= 95% of the
non-trivial writes: fetch constants 119 of 121; float constants 412 of 513; bool/loop 40 of 194; render state 34 of 93
(render-target, surface and window registers are derived at draw time and per tile, not stored plainly). Ranges begin
with 0xFFFFFFFF filler + type-2 NOPs; the flush writes constants inline (type 0, 0x4000+).


**P3 MAPPING VALIDATION (12:25, out of sample: fit calls 0-999, test 1000-1499):** the counts above are over registers
that VARY in the window with non-trivial writes (value not 0, 1 or 0xFFFFFFFF) - e.g. "412 of 513" is 80% of the
VARYING float-constant registers, not of all float constants (1,561 float registers took <= 1 distinct value and
need a constant, not a device read). Survival out of sample by writes in the fit window: 1-5 writes 128 of 148 hold
(86%), 6-20 writes 32 of 52 (62%), 21-100 writes 209 of 219 (95%); 259 in-sample mappings never reached the test set
(257 of them rest on 1-5 writes). **Stage 2 converts only registers whose mapping rests on > 20 writes and holds out
of sample; everything else stays decoded until a larger capture proves it.** Test power (12:35): with >= 5 test writes the
1-5 bucket holds only 10 of 22 and 6-20 only 2 of 22, vs 209 of 219 above 20 - low-write mappings are mostly
spurious, the threshold stands on mechanism. Qualifying set by class: float constants 173 (of 175 tested), fetch 32
(of 40), render state 3, bool/loop 0. **Stage 2 order: float constants, then the proven fetch registers; render state
and bool/loop stay decoded.**

**P3 STRATEGY (decided 12:20, autonomous per the user's "fully autonomous"):**
- Stage 1 - guest-thread front end: at each draw, the register values the game's own (recompiled) XDK code just wrote
  for it are applied to a native register file and the SAME transplanted backend draws, on the game's thread: no
  plugin CP thread, no ring kick, no tiling replay (each draw once, render targets full-screen from the device). The
  bridge keeps running as the reference; gate = 0 register mismatches per draw (known tile transforms excepted).
- Stage 2 - replace decoding with device reads, class by class, where proven (fetch and float constants first).
  Metric: share of each draw's state still taken from the written packets, driven to 0 = no emulation left.
- Then P5 (no packets written, no plugin) and P6 (multithreaded recording) as planned.
Also a payoff to state to the user: once render targets are ordinary PC targets (no EDRAM model), any internal
resolution becomes possible (1920x1080 exactly, a true 3840x2160, the display's own ultrawide shape); today the
supersampling multiple is integer only (3x = 3840x2160 pixels).
USER REQUIREMENT (2026-09-27): the internal resolution must be a player-selectable option in the F10 menu, next to
the other display options (e.g. 1280x720, 1920x1080, 2560x1440, 3840x2160, the display's native size) - not a fixed
or config-file-only value.


**P3 STAGE 1 VALIDATION RESULT (13:00, Fairfax stand, FE_F6/FE_F7):** the front end is driven by the guest's
CP_RB_WPTR write (plugin export RexNgpuSetKickCallback, on the game thread): it decodes the HARDWARE ring from its own
read index, expands every INDIRECT_BUFFER where it executes, applies SET_BIN_MASK/SELECT and skips predicated packets
like the CP. (Two earlier designs failed and are recorded in git: per-call range decoding - the XDK cursor is the
LAST dword written (NG2) and ranges start mid-packet - and a per-call stream.) Snapshots sampled by address, FIFO per
address. After a 100,000-draw warm-up, ~1.9 M draws compared per leg: the draw packet's own 0x21F9-0x21FC always
differ (expected); everything else <= ~1.9%, concentrated in six 3-register float-constant groups; render state
<= 0.4%. Timing test (FE_F7): of 613,354 mismatches in memory-loaded constants (LOAD_ALU_CONSTANT), the source
memory holds the PLUGIN's value at the plugin's execution in 495,746 (81%) - the game rewrites constant memory after
the kick and the plugin, a frame behind, reads the newer data; the front end reads at kick time. 211,393 packet-
sourced mismatches remain (0x43FC-FE, 0x47FB-FC ~5 k each; 0x2182 ~5.8 k) - open. NEXT (stage 1b): the front end
issues the draws itself (shaders from IM_LOAD/IM_LOAD_IMMEDIATE, resolves, swaps), the bridge only compares.
INSTRUMENT NOTE (added 14:30): every figure in the paragraph above (the ~1.9%, 613,354, 495,746 / 81%, 211,393)
was produced by the FIRST join - a per-address FIFO sampled by dword position ((addr>>2) % 16 == 7). That join
paired the template draws (1FA98980/9C0/A00, many executions a frame at different surfaces) with the wrong
execution, and mispairing can only ADD mismatches, so those figures are UPPER BOUNDS from a superseded instrument,
not estimates. Superseded, not re-derived in place.

**P3 STAGE 1 RE-MEASURED (14:25, Fairfax save 2, 60 s stand; instrument native-gpu 0dee0d9 + hashed sampling).**
Join: key = (packet address, execution ordinal since that side's own last swap) - NG2's alt b6347fa ported; the
draw packet's own 0x21F9-0x21FC applied AFTER the snapshot as the CP does (so they are compared, not excluded);
ring-reset / no-progress guards; stale snapshots (> 3 front-end frames) expire and are counted. Sampling: 1 in 16
packet addresses by a HASH of the address - the plain dword-position residue aliased with the stream layout
(FE_F17, same join: residue 0 gave 565,845 packet-sourced mismatches over 1,722,000 comparisons, residue 7 gave 0
over 216,000, and residue 7's memory-load rate was 25% HIGHER - different packet populations, not sample size).
UNITS: a "mismatch" is one register-instance (one register in one compared draw), never a draw.

| leg | hashed residue | comparisons after warm-up | packet-sourced | memory-loaded (source holds bridge value) | registers ever differing | join failures |
|---|---|---|---|---|---|---|
| FE_F18r0 | 0 | 1,809,500 | 0 | 413,774 (412,954 = 99.8%) | 62 | 0 dropped, 0 unmatched, 1 expired |
| FE_F18r7 | 7 | 1,801,500 | 0 | 467,540 (467,088 = 99.9%) | 80 | 0 dropped, 0 unmatched, 0 expired |

All differing registers are LOAD_ALU_CONSTANT-loaded float constants (groups 0x4130-2, 0x414C-E, 0x41A4-6,
0x4540-2, 0x455C-E, 0x45B4-6 carry ~99%). THE GATE (0 register mismatches per draw outside the known per-draw
registers) IS NOT MET: 820 (r0) and 452 (r7) register-instances are unexplained, and the timing class is real -
the front end reads constant memory at the kick, the GPU (and the plugin) read it later, after the game has
rewritten it. Consequence for stage 1b: a front end that DRAWS at decode time would use stale constants for those
draws. It must resolve LOAD_ALU_CONSTANT sources when the draw is issued at GPU time, not when decoded - e.g. keep
the source address and read it at issue (a deferred issue point: the next kick or the swap), which is what the
hardware does. Measured F7 -> F17r7 at matched residue, packet-sourced 211,393 -> 0 (old vs new instrument); F7 and
F16 are different populations (compared set and residue), so no F16 fall number is quoted.

**READ-POINT TEST (FE_F19, 14:30, Fairfax save 2, 60 s, hashed residue 0).** Each sampled draw's memory-loaded
constants were re-read at the NEXT kick and at the frame's XE_SWAP, and every reading compared with what the plugin
used. Over 561,850,784 memory-loaded register-instances (post-warm-up): read at decode -> 404,801 differ; read at the
next kick -> 409,069 differ (14,032,672 not yet read when compared); read at the swap -> 14,326 differ (115,997,856
not yet read - the plugin had executed those draws before the front end decoded that frame's XE_SWAP). The next
kick is no better than decode; the swap reading is ~28x better even on its smaller population (populations differ,
so this is a ratio of counts, not of rates). Reading: Fable fills LOAD_ALU_CONSTANT sources LATE in the frame.
STAGE 1b DESIGN CONSEQUENCE: the front end decodes at each kick but ISSUES a frame's draws at that frame's XE_SWAP,
resolving memory-loaded constants then (one frame of latency, as the GPU has). The 14,326 residual is the next thing
to explain (a later rewrite still, or a real mismatch).

**DEFERRED DECODE REFUTED (FE_F21, 14:40).** Decoding a whole frame's ring words at the frame's swap
(FABLE2_P3FE_DEFER=1; the trigger is the game's swap call, because Fable's XE_SWAP sits inside an indirect buffer, not
at the ring's top level - FE_F20's top-level scan never fired) produced garbage. There were 412 M packet-sourced
register-instance mismatches over 598,000 comparisons, 3,368 registers differing, and type-3 "ops" 00-03 that are
not packets. The FE saw 16.6 M draws against ~31 M at kick decode, and 214,143 snapshots expired. The indirect buffers
are consumed by the GPU (the plugin) within the frame and the game RECYCLES their memory before its swap, so the
command stream must be decoded at the kick; only the constant-memory reads can be late. The flag stays in the census
build as the recorded negative; never use it.
STAGE 1b DESIGN (revised): decode and record at each kick on the guest thread (packets validated: 0 mismatches); a
LOAD_ALU_CONSTANT becomes a (guest source address, register range) reference, and its bytes are copied into the
draw's upload-buffer slot at SUBMISSION time (a late latch), not at record time. The GPU executes the command list
after submission anyway, so this is the hardware's order: the command stream is fixed at the kick, and the constants
are fetched when the GPU gets there. What is validated is the swap-time reading (FE_F19: 14,326 residual); the
submit point must be measured the same way before 1b draws.

**STAGE 1b BUILT (d304ecb, 14:47-14:59): the front end draws on the guest thread** (FABLE2_P3DRAW=1, NG2 fd2c392
ported: dirty-bitmap register sync, shaders from IM_LOAD / IM_LOAD_IMMEDIATE with the microcode copied, backend
swap at XE_SWAP, the plugin's callbacks compare-only). FD1 (census build, 1/16 hashed compare): picture correct at
Fairfax (screenshot), backend 27.9 M draws / 3,901 swaps / 0 failed; 1,836,000 comparisons including warm-up
(1 in 15.2 of backend draws - the hash samples evenly) with 0 packet-sourced mismatches over 1,736,000 post-warm-up;
memory-loaded 304,570 register-instances, 304,474 source-now-bridge (~96 unexplained).
THE COST OF THE FRONT END DOING THE DRAWING (not "of recording": the switch changes who draws, the census plugin is
in both arms): FD A/B, clean exe, same binary, env switch only, interleaved A B A B, Fairfax 60 s, last 8 [swap]
windows - plugin-fed 60.0 / 60.0, front-end-fed 59.0 / 59.1. A FLOOR, not a cost: the plugin-fed arm sits at the
cap, so its headroom is invisible; what it does show is that front-end drawing on the guest thread CANNOT HOLD 60
at Fairfax. An uncapped pair is not available: the game's own 60 fps limiter (fable2_60fps, forced on for game
speed) caps it with vsync off too.
THE STALE CONSTANTS, CHARACTERISED (FE_F23, 15:03): every example is ONE block, guest 1FAAF000 (+0x000, +0x070,
+0x1D0: unit 3-vectors, e.g. -0.258121 vs -0.258066, 0.916056 vs 0.916028), loaded by many draws into
vertex c76/c83/c105 and pixel c80. That is a per-frame shared constants block (a light or camera direction) the game
rewrites after the kick; the front end reads it one animation step (~1e-4) earlier than the GPU path. Invisible in
the picture, a correctness item.
REVISED DESIGN FOR FABLE (b, over NG2's a): the front end DECODES on the guest thread into compact per-draw records
(register writes, LOAD_ALU_CONSTANT as source references, draw info, shaders) and a RECORDING thread applies them
and records the D3D12 work - reading the referenced constant memory at record time, which is the plugin's (and the
GPU's) read point. Both findings point there: (a) costs the game thread the whole draw issue (it cannot hold 60),
and (a) reads the shared constants block too early. It keeps the pipelining the plugin thread gave, with no PM4
parse on a second thread and no plugin in the draw path. NG2 (constants filled before the kick, holds 58-60 with (a))
does not need it.

**DESIGN (b) BUILT AND VALIDATED (f3ae688 + this commit, 15:10-16:23).** FABLE2_P3DRAW=2: at each kick the guest
thread FLATTENS the new ring words (indirect buffers inlined - they are recycled later; bin predication applied to IBs
by the flattener; every draw packet preceded by an address marker) and queues the batch; a RECORDING thread runs the
decoder on it and draws through FrontEndDraw / FrontEndSwap, reading LOAD_ALU_CONSTANT memory and microcode at record
time. Results at Fairfax, save 2:
- Picture correct (screenshots FR1, 15:22).
- Correctness (FR_C11, 60 s, 1/16 hashed compare, symmetric pairing - whichever side arrives second compares):
  1,640,000 comparisons after warm-up, 0 alignment drops, 10 / 9 expired, 0 packet-sourced register-instance
  mismatches. Memory-loaded: 613,535 register-instances differ, all in the moving per-frame block at 1FAAF000, now
  equal to 6 significant digits (the recorder reads it slightly LATER than the plugin instead of earlier).
- Ordering: the plugin's draw callback waits for the recorder so that the plugin's fence writes follow the recording.
  Comparing global draw counts let the plugin run up to ~15 draws ahead (the front end sees a few draws the plugin
  never calls back); now EXACT - the recorder publishes each draw's identity (address, initiator), the plugin walks
  the sequence (FR3: 102 front-end-only draws skipped, 0 plugin-only, 0 timeouts).
- Frame rate (vsync 60, last 8 windows): count-based wait 59.8 / 59.6 (FR A/B, within noise of plugin-fed
  59.9 / 59.7); EXACT wait 58.4-59.0 (FR3) - the plugin now waits on nearly every draw (31.2 M waits, 29.9 s), so the
  recorder is the critical path.
Bugs found and fixed on the way (recorded because each produced a plausible wrong number first): the snapshot key
recomputed the address from the flattened buffer (no pairing at all); a predicated-off draw's marker labelled the
next draw; snapshots allocated per sample (the heap's large-block path: 2 fps with the plugin waiting per draw).
NEXT (P5 in this shape): the per-draw wait exists only because the plugin still parses the ring in parallel. With the
recorder executing the stream's memory writes and fences itself, in order (EVENT_WRITE_SHD / MEM_WRITE / the read
pointer the game polls), the plugin's parse and the wait both go - no plugin in the frame at all.

## Phases

**P0 - measure (done).** The split above.

**P1 - per-draw cost (starts now, ships in 1.1.x).** The 78% moves into the native path whatever happens, so cheapen it
first: UpdateBindings / sampler parameters / RequestTextures work only when their inputs changed (dirty tracking the
guest already gives us), fewer submissions, no per-draw guest-memory copies. Each change A/B'd at Fairfax and Market
exactly like 1.1.1 (interleaved stands, same view signature).

**P2 - attribution census.** Hook every XDK entry point (config/hooks/native_gpu_trace.toml already has 313) and record
ring write-pointer ranges on the guest thread; the parser maps each packet to its producer. Output: packets and draws
by producer, and the unattributed count. The 09-22 work says what to expect: ~68% of Market draws came through the
wrappers; library emitters (resolve kicks, clear rectangles, fan draws, UP draws) and three predicated-tiling passes
made up the rest.

**P3 - native front end, in shadow.** At each draw, build the register file from the XDK device object's own state
(registers at dev+0x2880..0x2934, fetch constants +0x480, vertex constants +0x780, shaders +0x3198/+0x3194, dirty masks
+0x00..+0x28) and feed the SAME transplanted backend (it is register-file driven, not packet driven). The bridge keeps
running; every draw is compared register by register (the lockstep diff exists). Gate: 0 mismatches over a Bower
Lake + Market + Fairfax run, then the front end draws and the bridge only checks.

**P4 - close the gaps the census names.** Predicated tiling (draw once, no bins), boolean/loop constants (find their
device offsets), index width/endian, inline IM_LOAD shaders and shaders with no object, library-emitted draws, resolves
and clears as native calls, memexport, EDRAM aliasing (the render-target cache already models it).

**P5 - stop the command buffer.** The XDK's packet writers and KickOff become no-ops; VdSwap, vblank and GPU
interrupts are produced by our own swap path; the runtime gets a minimal graphics system instead of rexgpu-xenos.dll;
every plugin cvar the exe uses (swap_post_effect, texture_pack_*, readback_*, draw_resolution_scale_*,
anisotropic_override, ...) is defined by the exe itself. Census number 1 must read 0 here.

**P6 - multithreaded recording.** With no single parser thread in the way, record draws on the game's own render
thread (or split per render pass) instead of one worker.

## Shared with NG2

NG2 (claudecode-79) builds the same P2 census on its side now. The kit gets each phase as it lands, with the commit
and the measurement that justified it. Nothing on this track ships until its own A/B and picture check pass.

**STAGE 2, STEP 1 - WHERE THE REGISTER FILE COMES FROM (SRC5, 16:44, Fairfax save 2, census frame 2500; NG2's source
census 4d52ba8/e44defa ported).** FABLE2_P3SRC=<frame>: every hooked call that WROTE packets, and every call enclosing
one, with its r3-r10 and 256 bytes behind each pointer argument (p3_src.bin, 28,140 calls); FABLE2_P3MAP: the device
object at those calls' exits (p3_guest.bin, 28,139); the kick-mode front end's register file at every draw of the
frame (p3_fe.bin, 18,630). Joined by the draw packet's address inside a call's cursor range (tools/native_gpu/
p3_src_split.py, p3_src_full.py). Three traps on the way, each first giving a plausible number: NG2's 6,000-call cap
caught only the frame's start (a Fable frame is ~91,000 hooked calls); sampling every call on every thread under the
census lock starved the render thread until the guest froze (cdb stacks: one thread in VirtualQuery holding the lock);
the kick thread alone is not the packet writer (418 of 12,180 draws joined). And the first vote read "~52%" for every
register. CORRECTED (17:05, peer arithmetic: 799 contaminated joins out of ~10,000 cannot pull a vote to 52%): the ~52%
is a TOOL ARTIFACT - p3_sources.py keys each vote by (call, stack DEPTH, source), so one device word reached at depth 0
and at depth 1 splits its tally about in half (e.g. 0x4130: dev+0x08B0 51.7% best, the same word "second" ~51%). The
799 draws decoded in frame 2499 (the kick lags the census frame marker) are a separate, smaller contamination at
recycled addresses (NG2's ng2_073 shape), excluded from the clean population. The valid per-register measure is the
per-offset agreement of p3_src_split.py / p3_src_full.py below.
CLEAN POPULATION (fe frame 2500, joined: 9,429 draws; writing hooks 65: 5,460, 64: 1,589, 68: 590, 69: 518, 86: 305,
62: 283). Of 3,368 registers, 2,698 are constant over the frame; of the 670 that vary: **437 are a device word at the
writing call's exit on >= 95% of draws** (e.g. RB_SURFACE_INFO 0x2000 -> +0x2880 95.7% first executions / 100% tile
replays; depth control 0x2200 -> +0x2934 91.2% / 99.4%; fetch 0x4803 -> +0x048C 93.0% / 97.8%), **93 are 80-95%**
(the frame-boundary / state-changed-after-the-draw shape NG2 also saw), **140 are PER-DRAW**: the draw packet's own
0x21FA-0x21FC; 0x2180-0x2182; VS constants c0-c7 (0x4000-0x401F - per-draw matrices, NG2's c4-c6 analogue);
~40 pixel constants (0x4402-0x44BF, 0x4534, 0x4540/1); 0x47E0-0x47FC; texture fetch constants 0-2 (0x4800-0x4811)
and scattered fetch 5th words (0x4826, 0x482C, 0x4854-0x4859, 0x486E, 0x4874, 0x48B8/9/BE/BF); 0x4904/0x4908.
Full list: docs/native_gpu/p3/p3_src_full_fairfax_2500.txt. NEXT: the per-draw 140 - find each in the writing call's
arguments / the memory they point at (the p3_sources vote restricted to the clean population), then a device-built
register file compared with the decoded one at every draw (the stage-1 front end as the oracle).

**THE 102 FRONT-END-ONLY DRAWS, IDENTIFIED (FR4, 16:54).** In design (b) the recorder ISSUES every draw it decodes, so
these are drawn natively and were never drawn on the plugin path. They are the device's template block at
1FA98980-1FA98A38 (24 packets, 8 bytes apart, twice) plus two draws at 1F49739C / 1F4976B4. The 1FA989xx block has
initiator 00010081: point list, auto-index, ONE vertex each; the two others are 00030088: prim 8 (rect list), 3
indices. All come before the plugin's first reported draw (1F5276C0), and the count is exactly 102 in every run
(FR3, FR4). SAFETY IS STRUCTURAL, NOT EMPIRICAL: they are issued before the first present, into targets that are
cleared or overwritten before anything reaches the screen - true in every scene, independent of anyone having looked.
DECOMPOSITION OPEN: the log line is capped at 40, so "the block twice" is only what the cap showed; 24 x 2 + 2 = 50,
not 102 (peer arithmetic) - the full list of the 102 is not yet taken. The (a)-vs-plugin measurement's premise is unchanged: in (a) and (b) alike, the extras are
start-up draws, not per-frame work.
Stated beside the exact-ordering fps (58.4-59.0): that is the cost of the interim, where the plugin still parses the
ring in parallel and waits per draw for the recorder - not design (b)'s steady-state cost. The memory-loaded
constant figures, count WITH magnitude: stage 1b 304,570 register-instances at ~2e-4 relative (the front end read the
1FAAF000 block an animation step early); design (b) 613,535 at ~1e-6 (the recorder reads it slightly LATER than the
plugin). Incidence up, magnitude down ~100x; why incidence rose is not yet measured.

**STAGE 2 STEP 2 (first pass, 17:04): the per-draw 140 against the writing call's ARGUMENTS** (p3_sources.py --frame
2500, clean population 9,645 joined draws; docs/native_gpu/p3/p3_src_perdraw_fairfax_2500.txt). No argument or
256-byte sample explains them: VS c0-c7 best 5-27% (a device word at the other depth, i.e. the depth artifact again
on a minority), PS constants 30-64% at device words that serve many registers at once (dev+0x0868 votes for dozens -
a common value such as 0 or 1.0, not a source), fetch 0-2 at 5-50%; r3-r10 immediates explain none. So the per-draw
state is NOT in the draw call's immediate arguments or the first 256 bytes behind them. The 140 are the genuinely
per-draw state (matrices, per-material constants, the bound textures) - an irreducible bucket, not work left over -
and the next step is to find where the library reads them from: sample DEEPER (the whole object behind r4/r5, or the
shader-constant shadow the library copies from) or read them at the library's own SetVertexShaderConstantF /
SetTexture entry points, which are hooked calls with the values in their arguments.

**STAGE 2 STEP 2 - THE PER-DRAW 140, TRACED (17:05-17:40, census frame 3200, legs SRC7/SRC8/HUNT1-3).**
- MEMORY-LOADED OR PACKET-WRITTEN (p3_fe_src.bin): VS c0-c7 (0x4000-0x401F), most PS constants and ALL texture fetch
  constants are written straight into the packets (0% memory-loaded). A few groups are LOAD_ALU_CONSTANT from one
  shared page (0x40A0-3, 0x4402-3, 0x4410-3, 0x47E0-EF: per-frame blocks) and 0x4130/1, 0x4540/1 from 4 / 36 pages.
- LAST WRITER (p3_fe_wpkt.bin: the packet that last set each register, joined to the call whose range holds it;
  tools/native_gpu/p3_src_writer.py): the packet-written per-draw registers are written by the XDK's dirty-state
  flush, hook 65 (sub_8221DFC0: walks dirty masks, calls sub_8221C7D8 x8) and hook 64 (sub_8221D1B0), and their values
  are in NONE of the writing call's arguments or 256-byte samples.
- VALUE HUNT (FeHuntValues, whole guest memory at the kick, 8 distinct draws): the VS c0 row exists ONLY inside
  command-buffer memory in physical memory; in the virtual heaps a copy survives only for the shared matrices, at
  0x7049xxxx (thread stack, one as a row, two transposed). The per-draw constants are COMPUTED by the game, passed
  through a stack buffer and copied by the library; by the kick only the packet copy remains.
- WHO RECEIVES THEM (every call sample searched for every constant row the frame's draws used, 6,471 distinct rows):
  hook 64 r6 points INTO THE DEVICE - 0x44142C00 = device +0x780 (the VS constant shadow, c0 at +0x780) and 0x44143C00 =
  +0x1780 (the PS shadow) - 11,437 PS / 2,585 VS row matches; hooks 69 (sub_82221B90, r6, 2,475 calls a frame) and 67
  (sub_82221740, r6 / r4) take STACK pointers (0x7049xxxx) with 472 / 250 VS row matches; hooks 60, 74, 86, 59 similar.
READING: the flow is game computes -> stack buffer -> setter (hooks 69/67, SetShaderConstant-style) -> device shadow
(+0x780 VS / +0x1780 PS) or the command buffer -> dirty-state flush (hooks 64/65) -> packets. The census has now
MEASURED what the user's decision assumed: the per-draw state lives at the API boundary (the setters' arguments), not
in any structure a front end could read at the draw - so stage 2 hooks the XDK's setters and draw entry points; that is
the counted answer the census was for, not a fallback.
NEXT: the stage-2 ORACLE - at each writing call's exit (hooks 64/65, game thread) build the register file from the
live device (the 437) plus the setters' captured arguments (the per-draw set), key it by the call's packet range, and
compare with the kick-decoded file at every draw (the stage-1 front end as the oracle), register class by class.

**STAGE 2 STEP 3 - THE FLUSH'S ENTRY (SRC9 / SRC10, 17:39-17:48, census frame 3200).** The device's constant shadows
(+0x780 VS, +0x1780 PS, 8 KB) snapshotted at the ENTRY of every dirty-state flush (hooks 64 / 65; p3_flush_entry.bin,
tools/native_gpu/p3_src_flush.py), and 4 KB behind each of the flush's pointer arguments. Against 7,085 clean draws
with a snapshot of their writing flush:
- THE CRITERION IS THE RATE (>= 90% = device state at the flush; below = not), stated because a range-based wording
  contradicted itself (peer review, 18:00). DEVICE STATE: VS c8-c19, c21-c39, c41-c63; PS c1-c3, c5-c7, c11-c63
  (all 92-100%). NOT DEVICE STATE: VS c0-c7 (14-35%), VS c20 (83%), VS c40 (54%); PS c0 (53%), c4 (54%), c8 (22%),
  c9 (53%), c10 (39%). PROVISIONAL: these joins are address-only (see the temporal check below); the borderline
  members (VS c20, c40, PS c0/c4/c9) move first when they are re-run with the kick window.
- The VS c0 row of 7,092 draws (a slightly different filter from the 7,085 above: non-trivial c0 required): found in
  the shadow on 660 (9.3%), and NEVER in the 4 KB behind any flush argument, as a row or transposed.
- TEMPORAL CHECK (SRC12, 18:05; kick-count stamps on every call record and decoded draw - command-buffer memory is
  REUSED within a frame, so address-only joins can pick a different use of the same bytes): the packet that last set
  VS c0 is NOT inside any recorded call's written bytes as a SET_CONSTANT at that call's exit - 9,121 of 9,569 show other
  data there, 17 the reserved 0xFFFFFFFF, 426 have no covering call at or before the kick, 5 a SET_CONSTANT not
  covering c0 (9,121 + 17 + 426 + 5 = 9,569); of the 9,143 with a covering call, 7,795 (85%) had it exit 5+ kicks
  earlier. So its writer is outside every hooked call that moves the main cursor (+0x30).
  AN ADDRESS IS NEVER AN IDENTITY WITHOUT A TIME in this system - the third join fooled by one today (the per-address
  FIFO pairing template draws; the 799 recycled addresses from the previous frame; command memory reused within a
  frame): every join from here on carries the kick stamp. (This also qualifies every address-only join above: the device-word agreements of 95-100% stand as measured, but
  the joins behind them should be re-run with the kick window.)
READING: the per-draw matrices (VS c0-c7) never pass through the device or the flush's arguments. Together with the
whole-memory hunt (the row exists only in command-buffer memory by the kick, and transiently on the stack) and the
unfilled-header packets seen in stage 1 (a header reserved as 0xFFFFFFFF and patched later), the XDK's DIRECT constant path
is the explanation all four point to: the library reserves space in the command buffer and GAME CODE writes the
computed matrix straight into it. FOUR INDEPENDENT INSTRUMENTS AGREE (the shadow misses c0-c7; 4 KB behind every flush
argument misses them as a row and transposed; the whole-memory hunt finds them only in command memory by the kick; the
reserved-then-patched headers of stage 1), which is what makes this a finding. What is NOT yet established is the
writer's identity (the reserve API and the game code that fills it): that is the next measurement. For "skip the command buffer entirely" this is the one place where Fable itself writes GPU data into command memory:
a native path must hook that reserve/commit API (and read the payload the game wrote), or hand the game a native
buffer to write into. Everything else per-draw (PS c0/c4/c8-c10, fetch 0-2, the draw words) still to trace the same way.

**P5 ON FABLE - THE PLUGIN NO LONGER PARSES THE RING (18:26-19:45; exe native-gpu + rexglue fable-p5-exec 4345bfe; NG2's
executor, fork 471b367d / alt 6fce132, ported to design (b)).** FABLE2_P3DRAW=2 FABLE2_P5EXEC=1: the recorder, as it
decodes each flattened kick, collects the command processor's side effects - fences (value / counter), interrupts,
memory waits, register writes outside the draw ranges, EVENT_WRITE_ZPD (kind 8: the plugin's fake occlusion samples;
Fable issues ~450 queries a frame) and EVENT_WRITE_EXT (kind 9), the swap - and pushes them after the batch's draws are
recorded, the read pointer last; the plugin's worker executes them instead of parsing (0 plugin draw callbacks). The
plugin's swap callback now only captures the gamma ramp (a latent bug fixed on the way: FrontEndSwap had been asking
for the ramp outside the callback, the one moment the export returns nothing, so the front-end modes ran linear gamma).
- P5A (first leg): picture correct; 5.24 M side-effect records (1.55 M occlusion events, 52 K fences, 12.8 K interrupts,
  3.36 M register writes, 61 K memory waits); 0 stale read pointers, 0 wait timeouts; 31.1 M draws recorded.
- Interleaved A/B, same exe and plugin, env switch only, Fairfax 60 s, last 10 windows (logs D:/fable2_flash/ab110/
  logs): P5C plugin-fed 59.97 / 59.98 vs executor 58.18 / 57.59; P5D (after removing the per-draw microcode copy and
  flattening in runs) 59.99 / 59.96 vs 58.26 / 57.78 - indistinguishable from P5C (+0.13 fps against a 0.5-0.6 fps
  within-arm spread); P5E (decoder hot path trimmed - three changes in one commit, so not attributable to any one:
  one load + bswap per dword, no locked increment per packet (~240,000 a frame), census-only bookkeeping off)
  59.91 / 59.96 vs 59.97 / 59.89. HOW MUCH CAME OUT is bounded by the measurements, not by an estimate: the executor was
  at 58.02 fps (17.24 ms) before and its recorder is 95.3-99.5% busy at 16.67 ms after (15.88-16.58 ms of work), so
  0.65-1.35 ms per frame left the recorder - not the "~4.8 ms" (240,000 x a guessed 20 ns) first written. Work done identical: "draws
  11018 of 11364" in the stand view in every leg of both arms (one leg 11363).
- AT THE CAP NOW, so fps no longer measures the cost: BUSY SHARE is the readout, logged every 5 s on the critical
  thread of BOTH arms (P5F, 19:55-20:03; steady windows, the first window of each leg is start-up and excluded):
  plugin-fed - the plugin's command-processor thread 99.8-99.9% busy; executor - the recorder 94.1-97.7% (plus the
  plugin's executor thread 16.5-24.6%). Fps 59.97 / 59.96 vs 60.01 / 60.00. Not strictly like for like: the plugin
  thread's "busy" includes memory waits it spins through INSIDE packet execution, which P5 moves to the executor
  thread. What it does establish: the plugin-fed path is at least as saturated as the executor's recorder, so the
  earlier prediction that the executor falls below 60 first in a heavier scene does not follow (withdrawn). Next: take work off the recorder (P6: split recording across threads, or move
  the flatten-side decode of register writes forward), measured by busy share, then Market / walking routes.

**THE GAME'S OWN GRAPHICS SYSTEM - rexgpu-xenos IS NO LONGER THE GRAPHICS SYSTEM (20:00-20:44; FABLE2_NATIVE_GS=1;
NG2 alt 6f02c22 / 736839f ported: src/fable2_native_gs.{h,cpp}).** The app's OnPreSetup hands the runtime an
IGraphicsSystem that owns a D3D12 provider and the runtime presenter, the GPU register window (0x7FC80000: reads as the
plugin answered them; a CP_RB_WPTR write kicks the front end on the kicking thread), an executor XThread for the
side-effect batches (interrupts dispatched there; scratch-register write-back; COHER busy bit never set) and the vblank
worker. rexgpu-xenos.dll is loaded only as a library, for the settings it defines. The backend runs on the provider's
device and direct queue; the frame is presented into the runtime presenter by a PRESENT THREAD (own command list on the
same queue after the frame's submission is queued; the recorder only hands over "frame ready").
- GS1-GS5: picture correct at Fairfax (3840x1600 ultrawide, HUD band, overlay), draws identical (11018 of 11364),
  0 stale read pointers, 0 wait timeouts, 1.55 M occlusion queries; but 56.4-58.5 fps.
  - Presenting on the recording thread cost 15% of its wall time (the wait for the async submit): moved to the present
    thread (GS5: 57.0 -> 58.5; 4,200 frames presented, 0 skipped, 0 submission waits timed out).
  - The recorder's work per frame was then IDENTICAL to P5's (same inclusive composition within 0.5 points, same
    samples: P5EQ vs GSP profiles), and no thread was spinning: the missing frames were PACING. The vblank worker used
    Win32 Sleep(1) (NG2's port); the plugin's uses rex::thread::Sleep - with the runtime's sleep, GS6 / GS7 59.65 /
    59.27 fps, draws identical.
- The recorder is at 98.8-99.3% busy in this mode: no margin. NEXT: take work off the recording thread (split
  recording, P6), measured by busy share - the user's endpoint (no plugin in the frame) holds ~60 at Fairfax now but
  has no headroom.

**P6 STEP 1 - THE DECODER'S REGISTER WRITES IN BULK (21:05-21:19).** The GSP profile showed FeDecode at 23% exclusive of
the saturated recorder and ~300,000 single-register FeSet calls a frame. A run of writes wholly inside one draw range
(0x2000-0x23FF, 0x4000-0x4927 - every SET_CONSTANT and most type-0 packets) has none of FeSet's side cases, so it is now
a byte-swapped copy with the dirty bits set word by word (FeSetRun).
- CORRECTNESS (BV2, recorder + plugin parse + 1/16 comparison): 1,886,000 comparisons after warm-up, 0 packet-sourced
  mismatches, 0 memory-loaded, 0 alignment drops. (BV1, the first attempt, hung at start-up before the menu - guest
  threads waiting 30 s+; not reproduced in BV2: 1 of 2, recorded, not explained.)
- OWN GRAPHICS SYSTEM, 120 s leg (GS8): 60.0 fps in all 23 steady windows, median frame 16.6-16.7 ms, no drift;
  recorder busy 88.9-93.9% (was 98.8-99.3% in GS6 / GS7) - the first headroom in this mode; 7,800 frames presented,
  0 skipped; 0 stale read pointers, 0 wait timeouts.

**SECOND LOCATION - BOWERSTONE MARKET, save 1 (21:25-21:37; interleaved, same exe and fork plugin; logs MKA1/MKG1/
MKA2/MKG2, last 10 windows):** plugin-fed 59.72 / 60.00 fps with the plugin's command processor 99.7-99.9% busy;
own graphics system 60.00 / 59.80 fps with the recorder 63.9-67.5% busy. Draws in the stand view 5283 of 5683
(plugin-fed) vs 5238 of 5612 (own GS): the Market crowd moves, so these are not the same view and no draw-parity claim
is made from them (Fairfax's static stand is the parity evidence).
MARKET, CORRECTED (peer audit): MKA1 stood in a different spot (its views 5218/5283/5315/7088 appear in no other leg) and
is excluded; the matched set is MKA2 (plugin-fed) 60.00 vs MKG1 / MKG2 (own GS) 60.00 / 59.80 on the last-ten convention,
59.78 vs 59.75 / 59.62 over the whole steady period - EQUAL.

**WALKING ROUTE, Market save 1 (hp_leg route, 150 s, 21:40-22:05; WKA1/WKG1/WKA2/WKG2, first 28 steady windows):**
plugin-fed 59.79 / 59.73 fps (min window 57.3 / 57.4, worst frame 115.8 / 122.3 ms, hitches 6 / 7); own graphics
system 59.87 / 59.89 (min 57.4 / 57.0, worst 117.2 / 133.9 ms, hitches 5 / 8). A walk cannot repeat a camera path,
so this is not a controlled comparison - but the dips are present IN BOTH ARMS: they are the scene, not the renderer.
Headline for the night: the native path (no plugin graphics system) is NO WORSE than the old path anywhere measured
(Fairfax stand, Market stand, a Market walk); it does not remove the walking dips, which the old path has too.

**RELEASE READINESS (GSR1, 22:05):** the own-graphics-system mode runs on the RELEASED 1.1.3 plugin DLL unchanged
(rexgpu-xenos 476447cad811, loaded only for its settings): Fairfax 59.98 fps, 1.55 M occlusion queries, 0 stale read
pointers, 0 wait timeouts. So shipping it needs only the exe and a switch - no fork plugin. Exposing it (an F10
"Renderer: fully native" row, default off, as NG2 did in fc2ebb5) is the user's decision; not done.
