# Plugin performance baseline — clause A (2026-09-22, claudecode-b8)

SCOUTING (one scene, standing then a short walk; NOT yet the n>=3 named route
with the spread quoted — that is still owed). Recorded because clause A had NO
baseline at all.

## Config (all proven from the log's own echo)
- Build: v1.0.0 shipping, **plugin path** (rexgpu-xenos), gpu_backend d3d12.
- `GPU: internal scale 1x1` (resolution_scale=1), `readback 'none'` for the
  first menu read; **readback=some** for the gameplay reads (so the render is
  correct enough to drive; readback-off is the native-comparison config and is
  still owed as a second run).
- 60 fps patch ON — proven: log line **"Patch: frame divider (loaded) 2 -> 1"**.
- vsync ON (caps at the 60 Hz refresh — a confound: it bounds the number at 60,
  so this measures "does it hold 60", not headroom; a vsync-off run is owed).
- Display 3840x1600, RTX 5090. nvidia-smi: idle 0-1% / 6.5 GB resident before;
  42% / 8.0 GB during gameplay.

## Numbers (guest fps from the log's [swap] line, never the host loop)
- **Menu:** hard **30.0** guest fps, 0 hitches — WITH the divider patch on. So
  the divider does NOT take at the menu; this is a menu property, NOT frame-rate
  coupling, and says nothing about gameplay. (Separated per claudecode-76.)
- **Gameplay, Bowerstone Cemetery (Hero 1 save), standing ~70 s:**
  **60.0 guest fps, p50 16.5 ms, p99 ~19.5 ms, worst ~20 ms, 0 hitches**, steady
  across ~14 consecutive 5 s windows.

## What it means for the native layer
- **Gameplay is 60 fps with the patch** — the frame-rate-coupling question is
  answered FOR THIS SCENE: 60, correct speed, not 30.
- **RETRACTED (was "CPU/emulation-bound"): the measurement CANNOT say what the
  ceiling is.** The run is AT A CAP — 60.0 exactly, p50 16.5-17.0 ms, vsync on
  AND the patch's own 60 fps limiter. At a cap nothing saturates by
  construction, so GPU 42% is what a capped run looks like whether the true
  ceiling is CPU, GPU or neither. Utilisation and the cap are not independent.
  I read a conclusion off a number that cannot support it (claudecode-76 caught
  it). **The discriminator, owed and now THE test: vary ONE axis - 1x vs 2x
  resolution_scale, same scene/route. 2x still 60 -> GPU headroom, ceiling
  elsewhere. 2x below 60 -> GPU cost matters here. That works under a cap;
  utilisation % does not.** vsync off too, but if the divider-1 build self-caps
  at 60 it may stay 60 - if so, say so, don't call the capped number a ceiling.
- **Display:** the NVIDIA RTX 5090 renders the game and reports ~59-60 Hz
  (a second AMD adapter drives a 3440x1440@120 panel; the game window is
  3840x1600 - the config is muddled, record it, don't lean on it). The p99
  ~19.5 ms tail is NOT a 60 Hz vsync quantum (a missed vblank there is ~33 ms),
  so the 60 is most likely the patch's SOFTWARE cap, not blocking vsync.
- **The tail SURVIVES and is the one useful stability finding:** p99 ~19.5 ms >
  16.67 ms while standing still -> not strictly p99-stable per the gate.
  Keep it, but DO NOT attribute it yet - candidate causes (software pacing, the
  spin-wait, a periodic emulation cost) are not separated.

## DISCRIMINATOR RESULT (2026-09-22): 1x vs 2x, same scene = GPU HEADROOM
Ran it. Bowerstone Cemetery, Hero1 save, standing, readback some, vsync on,
patch on, proven from the echo (`internal scale 1x1` then `2x2`):
- **1x: 60.0 guest fps, p50 16.5 ms, p99 ~19.5 ms, 0 hitches.**
- **2x: 60.0 guest fps, p50 16.5 ms, p99 ~19.0 ms, 0 hitches.** IDENTICAL.
4x the pixels at no cost -> **GPU has headroom in this scene; the 60 is the
software cap (the patch's limiter), not a GPU ceiling.** And the p99 ~19 ms tail
is present at BOTH 1x and 2x -> resolution-INDEPENDENT, so it is pacing/emulation
jitter, not GPU load. This is the data behind retracting "CPU-bound": it is not
that the GPU is idle-because-CPU-bound; both are capped and the GPU is provably
not the ceiling here. Whether the UNCAPPED ceiling is CPU is still unknown (needs
vsync+cap off, or a scene that falls below 60). nvidia-smi: 1%/6.5 GB before,
~42%/8 GB during (a capped-run number, not a workload number).

## TOWN RESULT (2026-09-22, user drove) — THE CEILING SCENE, and it is CPU-bound
User navigated (real controller) into Bowerstone. Plugin, 2x, readback some,
patch on, vsync on. Guest fps from the swap line:
- **Bowerstone Slums:** ~57-58 standing, 40-51 moving/streaming, hitches on
  region streaming. nvidia-smi GPU 32%.
- **Bowerstone Market (busiest):** ~48-57 fps, p50 17-20 ms, p99 30-88 ms
  (streaming), climbing to ~57 as it settled. **nvidia-smi GPU 30%, VRAM 12.8 GB.**

**This is the valid version of the CPU-bound claim** (the one the cemetery cap
could NOT support): the town is BELOW the 60 cap - so the workload is finding its
own ceiling, not being paced - AND the GPU is at 30% (huge headroom). Not vsync
either: p50 17-20 ms is not a 16.67/33.3 ms quantum. So the town's per-frame
ceiling (~17-20 ms => 50-58 fps) is CPU/EMULATION cost (PM4 parse, page-watch,
game-thread), and the hitches are asset streaming. **The GPU is provably not the
limit in the town.** This is the measurement that validates the whole native
direction: removing emulation is removing the exact cost that holds the town
under 60. It should recover the town to the 60 the cemetery already caps at.

**2026-09-23 note (coordinator): the 46 fps below is n = 1 on a build since
retired as clause-A evidence (resolves dead, ~1,400 draws). It is the REASON
the market is the stand that matters, not the plugin's town baseline; that
baseline comes from the parity legs like every other row - three per
condition, spread quoted. Never compare a native town number against 46.**

**CLEAN STANDING NUMBER (user parked the hero in the market, hands off):**
6 consecutive 5 s windows, **~46 guest fps (44.8-46.9), p50 ~21.5 ms, p99 ~24 ms,
0 HITCHES**, GPU ~30%. Zero hitches = this is NOT streaming; it is the STEADY
per-frame emulation cost. 21.5 ms/frame vs the 16.67 ms 60-fps budget = the town
runs ~5 ms/frame over budget, GPU idle. **This is the definitive clause-A town
ceiling: 46 fps, CPU/emulation-bound, and the 46->60 gap is what native must
close.** (Cemetery caps at 60; market floors at 46 - same build, same settings.)

Owed to make it clause-A rigour: a NAMED fixed town route + n>=3 with the spread,
a STANDING steady-state (separate the CPU ceiling from the streaming hitches),
1x-vs-2x in town (expect still-below-60 since GPU is not the limit), and combat.
Keep Slums and Market as separate labelled populations.

## SCENE CAVEAT — this is not the scene the gate must answer
The user's remembered figures are "60 in the lake area, 40-60 in town".
Bowerstone Cemetery is NEITHER. The **40-in-town** case is the hard one and the
one clause A has to answer; a LIGHT scene holding 60 tells us nothing about it.
Keep Cemetery as its own labelled population; its spread must not travel to town
or lake. The town route (where it drops below 60) is the measurement that
actually finds a ceiling.

## Still owed (the real clause-A baseline)
- A NAMED, FIXED route with MOVEMENT and combat (standing understates cost).
- n>=3 per config, spread quoted.
- The matrix: readback some vs none; 1x vs 2x (shipping); vsync off for headroom.
- The Bower Lake harness scene (the handover's route), not just this save.

## NATIVE-PATH TIMINGS BEFORE 2026-09-23 ARE VOID AS A COMPARISON (claudecode-4c)

Every native-path frame time or fps taken before the build of 2026-09-23
~07:55 was measured with the RESOLVE SUBSYSTEM DEAD: `ngpu_replay_resolves`
(default true) queued each guest resolve for a replay that `ngpu_bridge_draws`
(default false) never ran, so no RT-to-texture copy was ever performed (leg O:
ResolveNative entered 0 times; the hook reports ~10 resolves per frame). Those
copies are now real work behind `ngpu_hooked_resolves` (default on), and they
land on the side of the comparison that has to meet clause A. So: earlier
native timings flattered the native path and are retired as clause-A
evidence, the same way the picture readings were retired. The cost is
measured as it is introduced: leg X (resolves ON) against leg Y (resolves OFF,
same build, same config, the cvar being the only difference), frame-time
p50/p99 from the `[perf]` line and guest fps from `[swap]`, quoted with the
spread across windows. Reminder from the same morning: the native path also
now issues ~2,060 draws per frame instead of ~1,400 (the range refusals are
gone), which is a second reason the earlier numbers do not describe the
current binary.

### The clause-A target, in plain words (leg X, 2026-09-23 07:52)
The translated (hooked) path spends **10.7 ms of guest-thread time per frame
on 2,130 draws** (the `xs:` line, same run), against a 16.67 ms budget at
60 fps; the guest ran at 43.0-43.8 fps (p50 22.7 ms) in legs T and X alike -
leg T had resolves DEAD and 2,075 draws, leg X resolves LIVE and 2,130, so
the drop from 60 fps (leg O, ~1,400 draws issued, ~975 refused) is the draw
COUNT that correctness restored, not the resolves. That per-draw cost on the
guest thread is the optimisation target. The resolves' own cost is a smaller
term measured by leg Y (within-run ON/OFF pairs, GPU sampled beside them).

### HEADLINE FOR CLAUSE A (2026-09-23, from legs X and Y): the GPU has headroom; the gap is guest-thread CPU work
The native renderer's own GPU-side frame is **6.85-6.90 ms** (host `[perf]`
p50) against a 16.67 ms budget, under resolves ON and OFF alike, at 55-61%
utilisation with ~18 GB of unrelated work resident. Everything over budget is
CPU work on the GUEST thread inside the hooked path: ~10.7 ms for 2,130 draws
(~5 us per draw: state decode, index decode, descriptor setup) plus ~2 ms for
~11 resolves (~180 us per resolve - not what issuing a GPU copy costs, so
something in that path is synchronous: a readback, a CPU-side conversion, a
fence wait or a per-call allocation; find which before optimising anything
else, it is the number most likely to fall by an order of magnitude). 60 fps
therefore does not need the rendering to get cheaper; it needs the per-draw
and per-resolve guest-thread work to shrink or move off that thread. Do not
read 43 fps as a GPU problem.
VRAM: the sampler's tail after the game exited read 6.4 GB, so the 8.0 -> 18.5
GB climb during leg Y belonged to THIS process, not the background: ~10 GB in
2.5 minutes, not seen to plateau. A leak signature until shown otherwise;
bears on clause C (end to end, not two minutes) and on the mirror-sizing
decision the user has reserved. Next legs sample per process
(`nvidia-smi --query-compute-apps=pid,used_memory`) and print the native
path's own allocation counters beside it.

### Resolve cost, measured as it was introduced (leg Y, 2026-09-23 07:58, within-run pairs)
One run, `ngpu_resolves_toggle_every=600` (10 s windows, hooked resolves ON on
even windows, OFF on odd), reader `scratchpad/resolve_cost_read.py`. Windows 0/1
straddle the world load and are excluded; four clean pairs:
| pair (ON/OFF windows) | guest fps ON | guest fps OFF | ON-OFF fps | guest p50 ms ON / OFF | host p50 ms ON / OFF |
|---|---|---|---|---|---|
| 2/3 | 42.2 | 47.8 | -5.5 | 23.2 / 20.9 | 6.90 / 6.80 |
| 4/5 | 42.0 | 47.3 | -5.3 | 22.9 / 21.0 | 6.90 / 6.90 |
| 6/7 | 43.1 | 45.9 | -2.8 | 22.8 / 21.1 | 6.90 / 6.85 |
| 8/9 | 43.8 | 47.2 | -3.4 | 22.5 / 21.1 | 6.90 / 6.85 |
**The resolves cost about 2 ms of guest-thread frame time (spread 1.4-2.3 ms,
-2.8 to -5.5 fps at ~45), same sign on all four pairs; the host frame is
unchanged (6.85-6.90 ms), so the cost is on the guest thread where the
hooked path records them.** Settling check: first-minus-second host p50
within a window +0.03 ms mean, spread 0.00-0.10 - no settling effect, both
lines kept. GPU beside it (nvidia-smi every 5 s): utilisation 55-61% flat
through the world, no structure at the 10 s toggle period; memory.used
climbed monotonically 8.0 -> 18.5 GB over the run (07:58:09 -> 08:00:21) -
flagged as a candidate (the process's own growth is not separated from the
background here), not a finding. The cvar is read at every resolve call, not
latched, which is what made the paired design possible; build the next cost
switch the same way. Clause-A picture after this: ~10.7 ms draws + ~2 ms
resolves of guest-thread time per frame on the native path, against 16.67.

### A FIXED cost with a name: ~4.4 ms per frame of fence waiting on the guest thread (leg Y, both conditions)
**CORRECTION (08:20): the `fence waits` line is tagged `[gpu]`, not `[ngpu]` -
it is the PLUGIN's (rexgpu-xenos) own submissions and readback landings, not
the hooked path's.** It is present under both conditions because the plugin
runs under both; with the native shadow off entirely (leg Z0) the guest holds
60.0 fps, so those waits fit the budget on their own. They are NOT part of the
native path's 10.7 ms and the sentence below attributing them to the hooked
path is withdrawn. What the native path adds on the guest thread is the
translated-draw time (the `[ngpu] xs:` line, 10.2-10.7 ms) plus ~2 ms of
resolves; 60 -> 44 fps is that addition.
The `[gpu] fence waits in 5.0 s` line, attributed to leg Y's windows: submissions
~1,480-1,580 x 580-640 ms per 5 s with resolves ON and ~1,600-1,660 x 630-670 ms
OFF - per frame about 7 command-list submissions and ~2.7 ms waiting on them in
BOTH conditions; readback landings ~70 per frame and ~1.6-1.7 ms in both;
resolve readback waits 0. So ~4.4 ms of every ~23 ms guest frame is the guest
thread waiting on fences, independent of the resolves, and a large share of
the "10.7 ms draw path". The resolves' own ~2 ms is CPU inside ResolveNative
(~140 us per call at ~14 per frame), not a fence; a timer with a breakdown is
the next instrument. Target with a name: why the hooked path submits seven
command lists a frame and waits on each.

### 60 fps against 44 fps is the ADDITIVE cost of the hybrid, not the native path's cost
Leg Z0 (native shadow off, plugin alone): 60.0 guest fps and VRAM flat at
9.37 GB for the whole world. Legs Z/X/Y (both paths live): 43-45 fps. That
difference is what running the native path ALONGSIDE the plugin costs today
- the hybrid is worse than either endpoint by construction. It is NOT "the
native path is 6 ms slower than the plugin"; native-only against plugin-only
is the comparison clause A needs and it does not exist yet. VRAM: the
~100 MB/s growth is the native path's (Z0's flat line is the control).

### Why 12 ms claimed and 6 ms measured are consistent (an inference, to be measured)
The [ngpu] xs timer brackets DrawTranslated per hooked draw on the guest
thread (10.2-10.7 ms) and that time is additive - it does not exist with the
native path off. The frame only grew from 16.67 ms (Z0, vsync-bound) to
22.2-23.3 ms (Z) because at 60 fps the guest thread has idle slack under
vsync: if the game's own work is ~10 ms, adding ~12 ms (draws + resolves)
gives ~22 ms. The ~6 ms of slack is INFERRED from these two numbers, not
measured; a guest-thread busy-time reading with the native path off is owed
before either figure is argued at the gate. (Coordinator's factor-of-two
challenge, 08:25.)

### The VRAM leak, fixed at its source (2026-09-23 08:31, leg Z3) - and the steady-state number the mirror decision needs
Owner: `rt.deps`, one surface-sized D32 texture + framebuffer per distinct
depth key per target, never evicted; the keys were the RING shadow's word
(the retired parser's garbage, ~10,000 distinct values in two minutes)
because ResolveNative looked its target up without a depth register. Fix:
GetRT takes the depth key from the device's register (four sane bases in
Bowerstone), from the ring only when replaying, and creates nothing with no
source; an LRU cap (`ngpu_depth_deps_max`, default 16 = 8x the measured
per-frame working set of 1-2 bases per surface in Bowerstone, eviction
counter always printing) stays as the net for scenes nobody measured. In
Bowerstone the per-base identity feature is exercised at a working set of 2
on three surfaces (04000140, 05000140, 14000500) and 1 elsewhere - minimal,
not nothing; the cap of 16 is eight times that, and the EVICTION path is
what remains untested - a fact about Bowerstone, not the game.
Leg Z3, same scene as Z2: depth dependencies created 0 per window (was ~117),
16 live (was 10,091), D3D12MA 360 allocations / 541 MiB flat (was climbing
to 10,434 / 15.9 GB), nvidia-smi **9,949-9,957 MiB flat** for the whole
two-minute world; guest 49.0-49.8 fps (43-45 before - observed, not
attributed).
**Steady-state native VRAM cost, bounded: ~9,950 (native on, Z3) - ~9,370
(native off, Z0) = about 0.6 GB in Bowerstone.** This is the budget the
user's reserved mirror-sizing decision (sparse/tiled vs a full 512 MB span,
on a card that routinely carries ~24 GB of unrelated work) was missing while
the leak made the question unanswerable; it is now a real trade with real
headroom.
The +3 fps (46.0-46.5 -> 49.2-49.7, ~1.1 ms of guest frame) that came back
with the leak fix is in NO instrumented time on either path: the native xs
bracket (9.8-10.9 -> 9.6-10.5 ms), the host GPU frame (6.8-6.9 both), and the
plugin's per-frame fence waits (landings 1.54 -> 1.56 ms, submissions 2.48 ->
2.53 ms) all held. One fifth of it IS attributed: ResolveNative fell from 19-20 to 3 us per
call once the depth-texture creations left it (Z4), ~0.23 ms per frame. The
remaining ~0.9 ms is unbracketed guest-thread time (the plugin's fence waits
are on its own thread and do not add); VRAM pressure on the driver (16-24 GB
and 10,000+ committed blocks vs ~10 GB and 51) is not excluded, only not
shown. The unbracketed remainder of the guest frame is ~10 ms of 20.3 and
is the largest clause-A item; leg P1 (ring parser off) carves the first
bucket out of it.

### The first bucket of the unbracketed ~10 ms: the ring parser, ~1.7 ms (leg P1, 2026-09-23 08:42)
`ngpu_ring_parse=false`: 53.1-54.2 fps (p50 18.4-18.7 ms) against Z4's
48.8-50.7 (~20.2 ms), same build and scene; xs, host frame and every counter
unchanged. RingAdvance on every hooked draw (30-37,000 packets a frame) for
the retired parser's output. Clause-A arithmetic now: native xs ~10.0, parser
~1.7 (removable), unbracketed remainder ~8.5, of a 20.3 ms frame against
16.67.

### Clause A at the shipped defaults (leg D1, 2026-09-23 08:50): a 2.2 ms gap, and a factor-of-four discrepancy under it
Guest frame 18.9 ms (52.1-53.0 fps) against 16.67: **the gap is 2.2 ms**, not
the 3.6 quoted from the 20.3 ms figure, and the unbracketed guest-thread
remainder (~8.4 ms) is nearly four times that gap - clause A needs 2.2 ms
found inside a bucket four times its size that has no instrument yet, not a
cheaper renderer. The discrepancy: native off (Z0) is 16.67 ms and defaults
are 19.05, so the MEASURED additive cost of the native path is 2.4 ms while
the xs bracket alone claims ~10.5 - reconcilable only if the guest had ~8 ms
of idle inside the 16.67 ms frame (game work ~8.7 ms), which is INFERRED and
now carries the whole story. The measurement that settles it, not yet run:
native path OFF and the 60 cap removed (vsync off) - ~115 fps confirms ~8.7
ms of busy time and the slack; near 60 collapses the reconciliation and one
of the two numbers measures something other than its label. It also settles
vsync-versus-software-cap by which knob removes the cap. No clause-A
statement leans on either figure until it reads (leg C1, after B2).

### Leg C1 (2026-09-23 09:02): the 60 is NOT the vblank and NOT the host present - a third pacer holds it, on the guest side

The measurement the coordinator asked for: native path OFF (`ngpu_shadow=false`,
the tune log shows the runner's `ngpu_shadow = true` replaced by the later
`= false`; no `[ngpu]` subsystem or xs line in the log), `vsync=false` read
back FROM THE PLUGIN ("GPU: ... vsync false", fable2_app.h:389), the divider
patched ("Patch: frame divider (loaded) 2 -> 1"), same runner and stand as D1.

What `vsync=false` removes, from the SDK source (rexglue-src): the host present
never waits anyway (d3d12_presenter.cpp:1158 presents with sync interval 0 and
tearing allowed), the emulated vblank drops from 1/refresh to 1 ms
(graphics_system.cpp:157-164, `no_vsync_interval_ticks = guest_tick_frequency /
1000`), and WAIT_REG_MEM polls with a yield instead of a sleep
(command_processor.cpp:1750). The monitors report 59 Hz, so a 60 quantum cannot
be told from the host refresh by rate alone - which is why the SOURCE was read.

| window | guest fps | swaps / 5 s | p50 ms | p99 ms | host [perf] fps |
|---|---|---|---|---|---|
| all 12 world windows | 60.0-60.1 | 301 every window | 16.6-17.0 | 19.0-19.6 | 203.6-204.0 (6.7 ms, gpu 68 %) |

Workload guard (the coordinator's, because a 1 ms vblank could speed a
vblank-derived clock): the plugin's readback landings per swap, a counter that
exists with the native path off - C1 21,604-21,655 per 301 swaps = 71.8 per
swap; D1 18,864-19,059 per 261-265 swaps = 72.1 per swap. Same scene, same
work per frame. Plugin submissions per swap DIFFER, 5.0 (C1) vs 6.6 (D1), and
that is the guard working, not a confound: landings are the scene-driven
quantity and hold; submissions are the native-path-driven quantity and move
because the native path is off in C1 (the coordinator's distinction).

**The null is itself a clause-A finding, not only an instrument failure.**
With the native path OFF the guest's own limiter holds 60.0 exactly: the game
plus the plugin fit inside 16.67 ms with room to spare, and the limiter does
its job. At the shipped defaults (D1, 52 fps) the native path pushes the frame
past the budget and the limiter can no longer hold it. So clause A is not "can
we reach 60" but "does BUSY time fit in 16.67 ms" - and only the wait bucket
answers it. The wait is to be TIMED, never removed for a measurement: a guest
whose limiter is gone runs its logic faster, the scene diverges, and the frame
rate stops describing the same workload (the confound C1 was guarded against).

Version skew on the mechanism read above, checked and CLOSED: the vblank
behaviour was read from rexglue-src (HEAD 0cb9040, 2026-09-22), which is newer
than the installed SDK in three headers. But graphics_system.cpp, the file
that holds the vsync worker, last changed at c94f5eb on 2026-08-21 (v0.10.0),
and the linked rexruntime.dll / rexgpu-xenos.dll are dated 2026-09-22 17:32 -
built after that change, so the source read IS the linked code. The same
check closes the M5 read-side claim (ProcessVertexFetchInstruction's per-word
in-bounds mask): that function is in src/native_gpu_xlat/, vendored from
0cb9040 and compiled INTO the app (ORIGIN.txt: the commit the installed
plugin was built from), so it is a read of the compiled source, not of a
different version.

What the 09-12 "GPU progress poll" sub_82BA1FA8 actually polls (read from the
recompiled code, fable2_recomp.27.cpp:22509): it loads a pointer from the
device object, reads the word it points to (the GPU's ring READ POINTER as
written back), compares it with the last value it saw, refreshes a "last
progress time" from the system tick when it moved, and returns "keep waiting"
(1) while that time is under 5000 ticks old; past that it calls the hang check
sub_82BA75F0. So the poll is not waiting on a schedule: its CALLER waits for
the command processor to consume the ring up to a target. The pacing question
therefore moves to the command processor - what holds ONE swap per 16.67 ms
there with the vblank at 1 ms.

**Reading:** 301 swaps in every 5 s window with both named mechanisms off is a
LIMITER, not a busy guest (a busy guest at ~16.7 ms would not land on 301
twelve times). The pacer is in guest code: the game (or its D3D runtime) waits
by a clock, not by the vblank. So the 60-cap slack CANNOT be measured by removing
vsync; it needs the guest's own wait site named (the caller of
KeDelayExecutionThread / KeWaitForSingleObject / NtWaitForSingleObjectEx in
the frame loop) and either timed per frame (the decomposition's WAIT bucket)
or patched. Until then the ~8.7 ms "slack" of the reconciliation is still
DERIVED, not measured, and no clause-A statement leans on it.

### THE DECOMPOSITION THAT CLOSES (legs P0 and P1, 2026-09-23 09:10-09:14, FABLE2_PROFILE=1, same runner and stand as D1)

The instrument: the port's own sampling profiler (src/fable2_profiler.cpp), 1 ms
samples of the GameThread and the 3D Engine thread, reports every 10 s: the
guest function under each sample and the host frames above it. It runs at
defaults and touches no clock. Read with scratchpad profile_read.py.

**3D Engine (the render thread), world windows only, shares of the thread's
samples; ms = share x that window's [swap] frame time:**

| leg | native | fps | frame ms | in sub_82BA1FA8 (GPU-progress SPIN = waiting for the CP) | game code (many small sub_82xxxxxx) | "sub_832BD218" (see below) |
|---|---|---|---|---|---|---|
| P0 | OFF | 60.0 | 16.7 | 56-58 % = 9.4-9.7 ms | ~36 % = ~6 ms | 6 % = 1.0 ms |
| P1 | ON | 47.8-48.3 | 20.7-21.0 | 0.0 % | ~34 % = ~7.1 ms | 65.7 % = 13.6 ms |

- **The slack is REAL and MEASURED: 9.4-9.7 ms per frame at native off.** The
  render thread spends it spinning in the D3D runtime's GPU-progress poll
  (sub_82BA1FA8, db16cyc no-ops: on-CPU, but a wait), i.e. waiting for the
  command processor to consume the ring. Game busy at native off is
  7.0-7.3 ms per frame (the reconciliation's DERIVED ~8.7 ms was 1.5 ms high).
- **At native on the render thread never waits:** the poll is 0.0 % of samples
  in every world window; the whole 20.7 ms frame is busy. The game's own code
  is unchanged (~7.1 ms); the native path is 13.6 ms of the profiled frame.
  Against D1's unprofiled 18.9 ms the same shares give ~6.5 ms game + ~12.5
  ms native (the profiler's suspend/resume costs ~1.8 ms per frame - P1 runs
  48 fps where D1 ran 52 - so quote SHARES, and scale by the unprofiled
  frame).
- **The two halves agree with the bracket:** the xs bracket measured 10.1-11.2
  ms in D1; the profiler's native share is 12.5-13.6 ms, i.e. the bracket plus
  2-3 ms of native-path work OUTSIDE it (resolves, texture uploads, the
  plugin fence waits the [gpu] logger reports on this thread). The "~8.4 ms
  unbracketed" of the D1 entry is therefore ~6.5 ms game + ~2 ms native
  outside the bracket - it now has a name on both sides.
- **"sub_832BD218" is NOT a guest function doing work: it is the profiler's
  label for the APP'S OWN CODE.** Its "waiting in" offsets (+0x134404,
  +0x2e9903 ...) are megabytes past any function, its stacks sit in
  nvwgf2umx (the NVIDIA driver) and D3D12Core, and the modules line shows no
  "app" class at all: the guest-function table's extent is open-ended past
  its last host address, so app code following the generated code in the
  exe is classed as guest and symbolised to the nearest preceding sub_.
  The 6 % at native OFF is the app's tracer/census hooks on the render
  thread (~1.0 ms per frame at ngpu_census=true - a confound every census
  leg carries).
- **Aliasing caveat on "on CPU 100 %, blocked 0 %":** the profiler's blocked
  measure is binary per 1 ms sample (did the cycle counter advance), so a
  thread that sleeps or yields in ~1 ms steps scores on-CPU every sample; the
  GameThread reads 100 % on-CPU with 64 % of its samples in the game's
  NtYieldExecution wrapper (sub_82CC38E8) and 76 % in ntdll syscalls - it is
  WAITING too. The fix (coordinator, 09:20): accumulate scheduled CPU time
  per window (GetThreadTimes / cycle deltas) instead of thresholding; being
  added to the profiler before P3.

**Clause A restated with measured parts (native off -> on, render thread),
corrected by the coordinator's three points (09:25):**
- The GATE number is D1's direct 18.9 ms = **2.2 ms over 16.67**. The
  profiler's figures are the decomposition's INTERNAL arithmetic, and they
  must be paired like with like: the profiled 13.6 ms carries ~1.8 ms of
  profiler overhead; the scaled ~12.5 ms is the one to set against a budget.
- The census/tracer hooks (~1.0 ms, the 6 % at native off) are NOT part of the
  game in a shipped configuration, so they come off the GAME side: game busy
  ~6.0 ms, native path ~12.5-13.1 ms (P1's 13.6 scaled by the P4-measured
  profiler cost of ~0.7 ms gives 13.1; P4's own 67.4 % of 18.9 gives 12.7;
  both include the ~1.0 ms census), and the parts SUM to D1's measured frame
  (6.0 + 12.7-13.1 = 18.7-19.1 against 18.9 direct). The native path's
  budget at 60 fps is therefore ~10.7 ms, and the gap is **~2 ms of
  native-path CPU on the render thread** - two independent routes (D1 direct,
  P0/P1/P4 decomposed) land on ~2.2, which is worth more than either alone.
  Not "the game is slow", not "the GPU is slow" (GPU 6.7 ms).
- The symbolisation defect is GENERAL, not specific to one label: every host
  address above the guest range, in every profile this tool has produced, was
  attributed to a guest symbol. That reaches backwards to the 09-12 profile
  recorded in patches.toml ("60 % in the GPU-progress poll") - a second,
  independent reason not to quote it. Fixed in the profiler by bounding the
  guest range at the app's own code (logged at startup); P3 onward carry it.

### Leg P2 (09:15, native OFF, vsync ON, 60.0 fps): where the command processor's thread sits at the cap

FABLE2_PROFILE="3D Engine,GPU Commands", same runner and stand, OLD profiler
(thresholded on-CPU; the shares below are of its samples, the exact scheduled
time comes with P3). The 3D Engine thread repeats P0. The GPU Commands thread
(the plugin's command processor), world windows: modules gpu plugin 45-47 %,
driver ("other") 24-27 %, ntdll 20-22 %, rexruntime 6-7 %, guest code 1 %.
Its wait sites ("waiting in" = the plugin frame above a syscall, % of samples):

| site | % |
|---|---|
| D3D12CommandProcessor::EndSubmission+560 | 11.5 |
| D3D12CommandProcessor::CopyToGuestMemory+2ea | 9.5 |
| D3D12CommandProcessor::BeginSubmission+375 | 6.6 |
| SharedMemory::RequestRange+1d | 5.7 |
| CommandProcessor::ExecutePacketType3_WAIT_REG_MEM+4c5 | 4.9 |
| rex::thread::MaybeYield+a | 4.9 |
| SharedMemory::RequestRanges+244 | 4.1 |
| D3D12CommandProcessor::LandCompletedResolveReadback+2bd | 3.1 |

Self time: VCRUNTIME140!_NLG_Return2 9.9 % (unexplained: an EH/longjmp-shaped
symbol; possibly a mis-symbolised hot function), ZwDelayExecution 6.9 % (the
WAIT_REG_MEM sleep with vsync on), WriteRegister 5.6 + 4.4 %, driver thunks
~7 %, UpdateBindings 3.2 %.

Reading (shares only, aliased on-CPU): at the cap the CP's waits are the
submission fences (EndSubmission / BeginSubmission, ~18 %), the resolve
readback copies (CopyToGuestMemory + LandCompletedResolveReadback, ~13 %),
shared-memory page requests (~10 %) and the ring's WAIT_REG_MEM (~5 %). The
vblank wait is NOT the dominant CP wait even with vsync on. Which of these
holds the swap at 16.67 ms with the vblank at 1 ms is P3's question (same
threads, vsync OFF, exact scheduled time): an idle CP reads few Mcycles, a
spinning one many, and the site names the pacer.

### Legs P3 and P4 (09:21-09:25, upgraded profiler: exact scheduled time per window)

The profiler now prints, per thread and 10 s window, "SCHEDULED x ms of y ms
wall = z% (GetThreadTimes kernel+user; N Mcycles summed)". Reference: a
thread that spins the whole window reads ~41-42 G cycles per 10 s on this
machine (the render thread at native off, 57 % of it in the poll), so Mcycles
is an absolute busy meter and SCHED% agrees with it within 2 %.

**P3 (native OFF, vsync OFF, the C1 condition; 60.0 fps, 16.7 ms), world windows:**

| thread | SCHED % | scheduled ms / frame | Mcycles per 10 s | where |
|---|---|---|---|---|
| 3D Engine | 98.1 | 16.35 | 42,327 | 57 % spinning in the poll, ~7 ms game (+~1 census) |
| GPU Commands (the plugin's command processor) | 95.0-96.4 | 15.8-16.1 | 40,825-41,217 | plugin code 47 %, driver 25 %, ntdll 20 %; "waiting in" EndSubmission+560 12 %, CopyToGuestMemory 9 %, BeginSubmission 6 %, RequestRange 5 %, WAIT_REG_MEM+4c5 4.6 %, MaybeYield 4.6 % |

What the CP's top "waiting in" sites ARE (read in the plugin source): the
EndSubmission site is ExecuteCommandLists + Signal (driver CPU work, not a
wait); CopyToGuestMemory is a copy loop into guest memory (work); the
BeginSubmission and LandCompletedResolveReadback sites carry no wait call in
their bodies. The ring's own WAIT_REG_MEM plus MaybeYield is ~9 % of samples,
~1.5 ms per frame. So **with the native path OFF the plugin's command
processor runs ~16 ms of real work per 16.67 ms frame - the emulation cost
that the 09-22 town result found (46 fps, GPU 30 %) is this thread, and at
60 fps in this scene it is within ~0.6 ms of saturation.** The exactness of
60.0 with vsync off is still NOT named (a saturated CP would jitter; ~9 % of
its samples are in the ring wait, which is where a 60 Hz-written value
would show); it is recorded as open, not explained.

**P4 (native ON, D1's settings; 50.6-51.2 fps, 19.5-19.7 ms), world windows:**

| thread | SCHED % | scheduled ms / frame | Mcycles per 10 s | where |
|---|---|---|---|---|
| 3D Engine | 94.5-97.8 | 18.5-19.1 | 41,110-41,690 | poll 0.0 %; "sub_832BD218" (app code = the native path incl. census hooks) 67.4 %; game ~32 % |
| GameThread | 98.6-99.2 | 19.1-19.4 | 42,260-42,800 | 63 % in the game's NtYieldExecution wrapper sub_82CC38E8, 76 % of samples in ntdll |

- The profiler's cost with the exact accounting is ~0.7 ms per frame (19.6 vs
  D1's 18.9 direct), less than the ~1.8 estimated from P1 (which had a hitch
  window). Scaled to 18.9: native path + census hooks 67.4 % = 12.7 ms, game
  ~6.2 ms; taking the ~1.0 ms census off the app side gives game ~6.2 +
  native ~11.7 + census ~1.0 = 18.9. The gate arithmetic stands: ~2 ms.
- **A spin-yield defeats the exact measure too:** the GameThread is 99 %
  SCHEDULED while yield-spinning (NtYieldExecution returns immediately when
  nothing else is runnable, so the thread never leaves the CPU). Scheduled
  time separates SLEEPS from work; only attribution separates SPINS from
  work. Both are needed; neither alone is the wait bucket.
- Second classifier defect, found by the first fix's own log line: the app's
  code is linked in GAPS between generated translation units (the profiler's
  anchor sits inside the guest host range), so bounding the range at the
  app's code did nothing ("bound not needed" printed). Fix v2: a generated
  function's extent is capped at 256 KB and the startup line counts the gaps
  that exceed it (a handful = the app's TUs; many = the cap is too small).
  P5 carries it.

### Leg P5 (09:27, native ON, all three threads, capped-extent classifier): the CP is NOT the pacer, and the native path has a caller

Startup gap census (the classifier's own check): 2 gaps over 256 KB, 3.6 MB
beyond the cap, largest 3,061 KB - a handful, the app's translation units;
the modules line now carries an "app" class. 50.6-51.0 fps, 19.5-19.8 ms.

| thread | SCHED % | scheduled ms / frame | Mcycles / 10 s | modules (of on-CPU samples) |
|---|---|---|---|---|
| 3D Engine | 95.3-97.3 | 18.5-19.1 | 41,195-41,274 | guest 28-30 %, **app 50-52 %**, driver 11-12 %, ntdll 8 % |
| GPU Commands | 76.9-78.4 | 15.0-15.1 | 33,192-33,435 | plugin 40 %, ntdll 32 %, driver 22 %, rexruntime 5 % |
| GameThread | 97.8-99.2 | 19.0-19.3 | 42,428-42,461 | guest 17 %, ntdll 77 % (66 % of samples in the yield wrapper) |

- **The coordinator's discriminator (09:30), answered:** at native OFF the
  CP did 15.8-16.1 ms in a 16.7 ms frame (P3); at native ON it does
  15.0-15.1 ms in a 19.5 ms frame (77 %), cycles 33.3 G vs 41.0 G. The CP's
  work is PER-FRAME CONSTANT, not throughput-limited: it gained ~4.5 ms of
  slack while the render thread lost all of its own. So AT NATIVE ON the CP
  does not hold the frame - it does one frame's work per frame, concurrently,
  and the render thread never waits for it (poll 0.0 %). **That is
  established for native ON only.** At native OFF the arithmetic is the
  opposite shape: the CP does 15.8-16.1 ms in a 16.67 ms frame while the
  render thread works ~7 ms and waits ~9.6 ms, and 7 + 9.6 = 16.6 - a render
  thread waiting for a CP within 0.6 ms of the frame. There the CP remains
  the live candidate for the binding constraint, and the open question
  narrows from "what paces the 60" to "why 16.67 rather than the CP's own
  ~16.0" - a 0.6 ms discrepancy, not an unknown mechanism. Two conditions,
  two bottlenecks; neither result travels to the other's condition.
- **What the P3 number says, and what it does NOT:** with the native path
  OFF, the plugin's emulation alone nearly fills the 60 fps frame on its own
  thread (~96 %) - the strongest argument yet that the in-place replacement
  is the right direction. But that thread runs CONCURRENTLY with the render
  thread, so the 100 % native build removing it frees a CPU core and the
  bottleneck that binds at native OFF; it does NOT shorten the guest frame at
  native ON by a millisecond. Clause A stays the render-thread arithmetic:
  16.67 ms, the game holds ~6.0, the native path must come down from
  ~12.7-13.1 to ~10.7 - the ~2.2 ms D1 measured directly. (Coordinator,
  09:35: the sentence a reader carries away must not make the gate look met.)
- **The native path's samples now attach to their generated CALLER:**
  sub_8221DFC0 carries 59.5 % of the render thread's samples (the game's
  draw-issuing function - the site the hooked D3D draws are called from) and
  sub_82BA34D8 (the D3D runtime's swap, VdSwap's function) 4.0 %; the game's
  own functions are the remaining ~30 %. The app's functions themselves
  appear as bare RVAs (fable2+482b93d 58.5 %, +48301e9 57.4 %, +487f6c6
  57.2 %, +4880acc 55.4 %, +4899241 19.4 % inclusive): the Release build
  carries no symbols for the app's own translation units. The next
  instrument is symbols for those TUs (line tables for the app sources only,
  not the ~500 recompiled ones), so the ~12.7 ms names its functions.
- The GameThread's yield: host fn (incl) reads NtYieldExecution_entry 64 % ->
  MaybeYield -> SwitchToThread -> RtlDelayExecution -> ZwDelayExecution 59 %:
  the SDK's MaybeYield is a Sleep(0)-shaped delay, scheduled the whole time.

### Leg P6 (09:34, native ON, render thread, app functions NAMED through the PDB): the ~12.7 ms has functions, and one of them is bigger than the gap

Build 09:33 (line tables for the app sources only, PDB 22 MB). 50.4-50.5 fps,
19.7-19.9 ms, 3D Engine SCHEDULED 96.2-96.6 %; modules guest 28-29 %, app
49-50 %, driver 12 %, ntdll 9-10 %.

Inclusive (% of the thread's on-CPU samples): ngpu_8221DFC0 (the hook thunk at
the game's draw-issuing function) 57.7 % -> ngpu::OnDrawIndexed 57.6 % ->
ShadowDrawIndexed 56.5 % -> ShadowDrawIndexedImpl 56.4 % -> DrawTranslated
54.5 %. The whole native cost sits under ONE call chain from ONE guest site.

**Self time (% of samples; ms = % x 19.8 ms frame):**

| function | self % | ~ms / frame | ~us per draw (2,090 draws) |
|---|---|---|---|
| ngpu::SharedConstantsFor | 16.8 | 3.3 | 1.6 |
| ngpu::DrawTranslated | 10.3 | 2.0 | 1.0 |
| ngpu::ReadDrawState | 7.5 | 1.5 | 0.7 |
| ngpu::GetTexture | 4.7 | 0.9 | 0.45 |
| NVIDIA driver thunks (nvwgf2umx) | ~7 | 1.4 | |
| ntdll!NtWaitForSingleObject | 2.8 | 0.55 | (the fence wait) |
| D3D12Core | 1.2 | 0.25 | |
| ngpu::Lookup | 1.1 | 0.2 | |

Wait sites ("waiting in", the app frame above a syscall/driver leaf):
DrawTranslated+ba24 3.4 %, plume::D3D12CommandList::setPipeline 3.4 %,
OnPresent 2.8 %, EndFrame 2.8 %, ShadowDrawIndexedImpl+13fc 2.1 %,
DrawTranslated+be88 1.7 %, D3D12DescriptorSet::setTexture 1.5 %, GetTexture
1.5 %.

**Reading:** SharedConstantsFor alone (~3.3 ms self, per draw) exceeds the
2.2 ms clause-A gap. The four named ngpu functions are ~7.7 ms of self time
per frame; the driver ~1.4 ms. The cost is CONCENTRATED, as the bare-RVA
shape predicted, and it is per-draw CPU work in our own code - the kind that
dirty tracking, caching and batching remove, not driver or GPU cost. Next:
read SharedConstantsFor for what it does per draw (a 4 KB+ constant block
copied or hashed 2,090 times a frame is the obvious suspect), then measure
one change at a time with the same instrument.

### THE HOT PAIR DECODED (09:45): two READS from write-combined memory per draw - prediction written BEFORE the fix is measured

P6's leaf list put 8.8 % + 5.9 % = 14.7 % of the render thread's samples on
two ADJACENT instructions, SharedConstantsFor+0x4266 and +0x426d. The line
table (llvm-symbolizer against the 09:33 PDB) said present.cpp:6563 as built
= committed line 6560; the DISASSEMBLY at the sampled addresses named the
statement without the line table's slack:

    movzwl 0x220(%r14),%ecx    ; blk[136] & 0xFFFF
    movl   0x230(%r14),%eax    ; blk[140]          <- 8.8 %
    shll   $0x10,%eax          ;                   <- 5.9 %
    orl    %ecx,%eax ; movl %eax,0x200(%r14)   ; blk[128] = ...

i.e. `blk[128] = (blk[136] & 0xFFFF) | ((blk[140] & 0xFFFF) << 16)` where
`blk = g_s.upload_map + off` - a pointer INTO THE D3D12 UPLOAD HEAP. A CPU
mapping of an upload heap is WRITE-COMBINED memory: stores stream through the
WC buffers cheaply; a LOAD is uncached, drains the buffers and stalls the
core. Two loads per draw, 2,090 draws: 2.9 ms. Implied cost per load from
P6's own figures: 2.9 ms / 4,180 = **~700 ns** (the coordinator's 300 ns
guess was low by 2x; the direct measurement is checked against ~700). A
first reading of the same profile blamed the 40 LoadV per draw beside that
line - plausible, wrong, and nothing in the profile contradicted it; the
disassembly did. (Rule: a hot LINE is a claim about the line table; a hot
ADDRESS is a fact. Disassemble at the sampled address before naming the
statement.)

**The class, enumerated by reading and bounded by arithmetic:** every read
through a pointer derived from `g_s.upload_map` or `g_s.cache_map` (BOTH are
UploadBuffer heaps: present.cpp 8260/8270). Per-draw readers at defaults:
the pack line and the forced-bit read-modify-write, both in
SharedConstantsFor - two loads a draw. The other sites are one-shot dumps
(the skin dump, the transform rows), a probe every 300 frames, a log at draw
200 every 300 frames, the ps_debug flat-colour modes, the rect expansion
(reads source vertices per RECT draw - ~1,800 per run, not per frame) and a
six-shot tessellation comparison. The bound agrees: at ~700 ns the ~12.7 ms
native path admits at most ~18,000 WC loads a frame (~9 per draw), so no
per-draw scan of the mapped heaps can exist today.

**The fix, and the discipline that keeps it fixed:** assemble every block in
a stack/thread-local shadow, copy it into the heap ONCE, read only the
shadow; and the mapped pointers become a WRITE-ONLY TYPE
(src/native_gpu_wc.h: WcHeap -> WcSpan<T> -> WcRef<T> with operator= and
nothing else), so a read through the heap is a COMPILE ERROR - the compiler
enumerates every reader, not the grep. The one escape hatch is named
`slow_uncached_readback()` for the diagnostics that genuinely need it, so a
grep is the audit trail.

**PREDICTION (written 09:50, before the build):**
- P7 (native ON, render thread, profiler): SharedConstantsFor self share
  falls from 16.8 % to a few percent; the render thread's SCHEDULED ms per
  frame falls by ~2.5-2.9 ms; **the GPU-progress poll sub_82BA1FA8, 0.0 % in
  P1/P4/P5, returns to a small positive share** - slack reappearing on the
  render thread is what "no longer the bottleneck" looks like, and that
  counter cannot be confused with the cap (coordinator).
- D2 (defaults, no profiler): the guest-side limiter holds EXACTLY 60.0, so
  if the render thread drops below 16.67 ms the fps reads 60.0 and the frame
  time clamps at ~16.7 - a successful and an over-successful fix read the
  same. So D2's fps is the GATE reading (60.0 = clause A's rate target met in
  this scene), not the measurement; the measurement is P7's busy figures.
- Well short of these (SharedConstantsFor still >10 %): the WC model is
  wrong and the ~700 ns is measured directly (a timed loop of N loads from
  the mapped heap against N from a stack array).

### RESULT (leg P7, 10:01, native ON, render thread profiled, build 10:01 with the write-only heaps): the prediction held on the instrument

Steady state = the last six 5 s windows (the first six carry the load and a
streaming stall of one 3.3 s frame; hitches 4/3/10/41/8/8 then 0/0/0/0/0/0).

| quantity | P6 (before) | P7 (after) | predicted |
|---|---|---|---|
| guest fps, steady | 50.4-50.5 | **55.5-56.1** | (clamped at 60 if under budget) |
| frame ms p50 (profiled) | 19.7-19.9 | **17.7-18.2** | -2.5 to -2.9 |
| 3D Engine SCHEDULED ms / frame | 19.0-19.7 | **17.0-17.5** | |
| SharedConstantsFor self | 16.8 % | **4.5 %** | "a few percent" |
| app / driver / guest shares | 50 / 12 / 29 | 45 / 11 / 32 | |
| xs bracket | (D1 10.1-11.2) | **8.8-9.2 ms** | |
| GPU-progress poll sub_82BA1FA8 | 0.0 % | **0.0 %** | ">0 if slack returned" |

- Saved: ~2.0 ms of the profiled frame (19.8 -> 17.8) and 12.3 points of the
  render thread's samples (16.8 -> 4.5 %). The prediction was 2.5-2.9; the
  landing is 2.0-2.4 (the residual 4.5 % is the block's assembly, which the
  prediction did not separate from the reads). The poll did NOT return above
  zero: at 17.8 ms profiled (~17.1 unprofiled) the render thread is still
  the bottleneck, ~0.5-1 ms over the 16.67 budget at defaults. D2 (defaults,
  no profiler) is the gate reading.
- **The distinct-results counter (coordinator's point 3), printed every
  ~1.09 s:** 126,300-126,800 blocks built per window = ~2,070 per frame (the
  draw count, as it should be); 12,500-13,100 identical to the previous
  draw's (10 %); only 3,700-6,300 DISTINCT per window = ~60-100 per frame.
  So 95-97 % of the blocks a frame are repeats of one already built that
  frame, non-adjacent (per material, not per run of draws). A per-frame
  cache keyed on the INPUTS could skip most of the remaining assembly - but
  the assembly is mostly reading those inputs, so the ceiling of that second
  fix is the 4.5 % (~0.8 ms), not more. Recorded as a candidate, not queued.
- New self-time order on the render thread: DrawTranslated 13.4 % (2.4 ms,
  @present.cpp:6970), ReadDrawState 9.4 % (1.7 ms, @2888), GetTexture 4.9 %,
  SharedConstantsFor 4.5 %, NtWaitForSingleObject 2.6 %, driver ~5 %.
- The census self-check on this build: both large gaps resolve to app code
  (the symbol just past the cap is an app function in each), so the
  "1 unresolved" it printed was a labelling defect in the check itself - an
  app verdict counted as no verdict. Fixed to a tri-state (generated / app /
  no symbol) in the next build.

### GATE READING (leg D2, 10:03, shipped defaults, NO profiler, build 10:01 with the write-only heaps): 60.0 fps in this scene

Same runner, same stand, same settings as D1 (52.1-53.0 fps, p50 18.8-19.1
ms, 08:50). Steady state, the last seven 5 s windows:

| window | guest fps | swaps / 5 s | p50 ms | p99 ms | hitches |
|---|---|---|---|---|---|
| 1 | 59.9 | 300 | 16.7 | 19.8 | 1 |
| 2-7 | **60.0** | 300-301 | 16.6-17.0 | 19.1-19.6 | 0 |

- **Clause A's rate target reads as met in this scene at defaults: the
  guest-side limiter clamps at 60.0**, where D1 read 52 with the same build
  minus the write-only heaps. The frame time under the clamp (p50 16.6-17.0,
  p99 19.1-19.6) is the same shape as the native-OFF baseline of 09-22
  (16.5 / 19.5), i.e. the tail is the pre-existing pacing tail, not ours.
- This is ONE scene (the runner's Bowerstone stand) at ONE resolution; the
  09-22 town result (46 fps native OFF, CPU-bound) and the lake are separate
  populations and are owed their own legs before "clause A met" is written
  without a scene name.
- **A pre-existing 3.2-3.4 s stall, NOT a regression:** every leg of today
  shows one frame of 3,225-3,404 ms about 35 s into the world stand (D1
  3,260; P6 3,405 split over two windows; P7 3,337; D2 3,225), with 300+ ms
  frames beside it and 41-47 hitches in that window. Same point in every
  run, so it is deterministic streaming (a region or a shader burst), and
  D1 carried it before any change of 2026-09-23. Unattributed; during it
  the render thread's samples sat 48 % in ntdll waits (P7's 10:02:04
  window). Its own item: the gate's stability clause fails on it in this
  scene regardless of the rate.

**TWO VERDICTS ON D2, NOT ONE (coordinator, 10:10):**
- **RATE: MET in the runner's Bowerstone stand at 1x** - 60.0 fps, 300-301
  swaps in every steady window, where D1 read 52.
- **STABILITY: NOT MET in the same leg** - p99 19.1-19.6 ms is over the
  16.67 ms budget, and one 3.2-3.4 s frame with 41-47 hitches sits ~35 s into
  the stand. "60.0 fps at defaults" must not travel without this line; the
  next person to meet the 3.2 s frame would otherwise read it as a regression.

**A DECISION THAT IS THE USER'S, STATED HERE AND NOT RESOLVED HERE.** Clause
A carries two bars that now disagree in this scene:

| bar | Xenos plugin (09-22 baseline, same scene) | native path (D2, 2026-09-23) |
|---|---|---|
| rate | 60.0 (capped) | 60.0 (capped) |
| p50 | 16.5 ms | 16.6-17.0 ms |
| p99 | ~19.5 ms | 19.1-19.6 ms |
| multi-second hitch in the stand | present (D1, 3.26 s) | present (D2, 3.22 s) |

- If the bar is **parity with the Xenos layer** ("performance >= the Xenos
  layer"): the native path is at parity on every row in this scene, and
  clause A's perf half is close to done HERE (town and lake still owed).
- If the bar is **60 fps stable, p99 <= 16.67 ms, no hitches**: NEITHER path
  meets it, the plugin never did, and the remaining work is the tail and the
  streaming hitch - a different kind of problem from the 2.2 ms just closed.
Which bar the gate is, is the user's call. Relayed by the coordinator; the
file records the question, not an answer.

**Next perf candidate, measured before it is touched (10:15):** the byte-wise
endian swap of vertex streams into the upload heap (present.cpp:7538,
DrawTranslated's top leaf, ~3.3 % = ~0.6 ms) moves only ~10 KB per frame in
the steady state ("streamed per frame ... MEAN 10 KB", P7 and D2; the sampled
frame 0 KB copied per draw). So its cost is per BYTE, not per volume: byte
stores into write-combined memory. The fix shape is therefore "swap in a
stack buffer, store whole lines once", not a SIMD shuffle for volume; the
stores stay stores. Worth ~0.5 ms in the town, nothing at the clamp here.
Candidate; the boolean/loop constant SOURCE (correctness) and the 3.2 s
streaming frame (stability) rank above it.

## THE GATE BAR, DECIDED (2026-09-23 10:20, the user's decision as relayed by claudecode-76)

**"PARITY WITH THE PLUGIN is clause A's performance bar. Clause A's perf
half passes where the native path matches or beats the Xenos layer. The
~19.5 ms p99 and the 3.2-3.4 s streaming hitch are INHERITED DEFECTS -
recorded, not blocking. Next work: prove parity holds in the town and the
lake, then back to correctness."** (Relayed, not heard first-hand; recorded
with that attribution so it is traceable.)

Consequences applied to this file:
- The p99 tail (19.1-19.6 ms native, ~19.5 plugin) and the 3.2-3.4 s
  streaming frame in the Bowerstone stand are INHERITED DEFECTS: kept as
  named open items with their measurements (the hitch is the largest open
  stability item), no longer gate items, and no work goes to them ahead of
  the parity legs. Nobody re-promotes them from the numbers alone.
- Clause A's perf half PASSES IN THE BOWERSTONE STAND at 1x: the native path
  is at parity or better on every row of the side-by-side table above (rate
  60.0 = 60.0; p50 16.6-17.0 vs 16.5; p99 19.1-19.6 vs ~19.5; the hitch
  present in both). ONE scene.

### THE PARITY DECISION RULE, written before the town and lake legs run

Parity is a COMPARISON, so every scene needs BOTH legs: plugin (native path
OFF, `ngpu_shadow=false`) and native (shipped defaults), same save, same
stand, same settings, back to back, n >= 3 per condition (the noise-floor
rule: three legs, quote the spread). The plugin baselines for the town and
the lake DO NOT EXIST yet; a native-only 60.0 there proves nothing.

Rows that count, each read from the [swap] line's steady windows (the load
and the first streaming stall excluded, the exclusion stated): guest fps,
p50 ms, p99 ms, worst ms, hitches per window. PARITY = for every row, the
native legs' spread lies within the plugin legs' spread or on its better
side; both spreads are quoted, never two single numbers. A row where the
spreads overlap partially is "not separated at n = 3" and is written that
way, not as a pass or a fail. Rate under the 60.0 clamp counts as equal.

### THE PARITY RULE APPLIED, BOWERSTONE STAND, n = 3 per condition (legs PB_off1-3 / PB_on1-3, 10:25-10:45, build 10:01, interleaved off/on/off/on/off/on, 75 s stands)

Steady windows = the last six [swap] windows of each leg (the last 30 s; the
load and the first streaming stall excluded). Per-leg values, then the rule
on the spread of per-leg MEDIAN window values (worst = per-leg max; hitches =
per-leg total). parity_read.py, no human picked a window.

| leg | fps | p50 ms | p99 ms | worst ms | hitches |
|---|---|---|---|---|---|
| PB_off1 (plugin) | 60.0 | 16.8-17.0 | 19.0-19.5 | 20.1 | 0 |
| PB_off2 (plugin) | 60.0 | 16.8-17.0 | 19.1-19.6 | 20.6 | 0 |
| PB_off3 (plugin) | 60.0 | 16.9-17.0 | 19.0-19.6 | 20.1 | 0 |
| PB_on1 (native) | 60.0-60.1 | 16.5-17.0 | 19.1-19.6 | 20.4 | 0 |
| PB_on2 (native) | 59.8-60.0 | 16.6-17.0 | 19.2-20.1 | 26.8 | 0 |
| PB_on3 (native) | 59.6-60.0 | 16.5-16.9 | 19.1-22.3 | 23.8 | 0 |

| row | plugin spread (medians) | native spread (medians) | verdict |
|---|---|---|---|
| fps | 60.0-60.0 | 59.9-60.0 | PARITY (both at the clamp) |
| p50 ms | 16.8-17.0 | 16.6-17.0 | PARITY |
| p99 ms | 19.1-19.2 | 19.4-19.6 | **WORSE** (by ~0.3 ms; the spreads do not overlap) |
| worst ms | 20.1-20.6 | 20.4-26.8 | NOT SEPARATED at n = 3 |
| hitches / window | 0 | 0 | PARITY |

**Reading:** the native path is at parity on rate, p50 and hitches in this
stand, and ~0.3 ms WORSE on the p99 tail at n = 3 (its per-window p99 also
reaches 20.1-22.3 where the plugin's stays under 19.6); the worst frame is
not separated. So clause A's perf half does NOT yet read as a full pass in
Bowerstone under the rule as written - one row is worse by a third of a
millisecond. This is the honest first application; the 09-22 single
scouting run (60.0 / 16.5 / ~19.5) is retired as the Bowerstone plugin
baseline in favour of PB_off1-3. Next: the swap-shadow fix (built 10:50) and
whatever else moves the tail, then this table again on that build - the
p99 row is what the native path owes here.

**Write-only heap: what the type owns and what it does not (10:50).** Every
element store and bulk copy goes through WcSpan/store(), where a read cannot
compile. THREE sites remain raw `store_ptr()` pointers - the texel-row writer
(memset + decoded rows), CacheAlloc's return (the stream builders'
destination) and the per-vertex UP writer with pointer arithmetic - and for
those the grep is the instrument, not the compiler. A declared exception,
not an assumed-fine one: converting the texel path means a second
multi-megabyte copy per texture, so it waits for a measurement. The
vertex-stream endian swap (present.cpp, the case-2 loop) now swaps in a
normal-memory shadow and stores once; P8's prediction is the old leaf gone
AND a small store leaf appearing, DrawTranslated self down ~3 points net.

## THE BOOLEAN/LOOP CONSTANT SOURCE, READ OFF THE PACKET STREAM (leg CS1, 2026-09-23 ~10:30, ngpu_ring_parse=true + ngpu_log_consts=48, native ON)

The instrument: every SET_CONSTANT / LOAD_ALU_CONSTANT the ring parser walks
is counted by (opcode / type / index / size) per ~1.09 s window and the first
48 are logged. The denominator is in the same window: ~126,400 hooked draws
(the shared-block "built" count).

**Found: the booleans and loop constants travel as ONE inline SET_CONSTANT of
type 2 (BOOL), index 0, 40 dwords = the 8 bool words followed by the 32 loop
words (0x4900..0x4927), 11,855-12,003 per window = ~195 per frame, ~one
per 10.6 draws.** The values are IN the packet, not in memory. The parser
decodes them correctly (base 0x4900 + i for i < 40).

**Found: why the ring shadow read "floats in bool slots".** Once per window
the walk decodes a LOAD_ALU_CONSTANT of type 0 from index 0x3E0 with a size
of 2,548 or 2,948 dwords (0x4000 + 0x3E0 + 2548 = 0x4DD4): floats swept over
the PS constants, the fetch constants, the BOOL and LOOP range and beyond.
Beside it, single packets with impossible types (94, 108, 126, 144) - the
retired parser walking inline draw data, exactly the reason it was retired.
One such sweep per second is enough to leave floats in 0x4900-0x4927 at any
draw whose SharedConstantsFor read the ring shadow between the sweep and
the next type-2 packet.

Coverage of the walk (the absence caveat, coordinator): per window it logged
~42,900 register sets at index 0x180 and ~43,000 at 0x208 against ~126,400
hooked draws (~0.34 per draw), ~16-19,000 of each per-draw float load - the
walk sees a substantial fraction of the stream, so type-2 packets at ~12,000
per window are a real rate, not a trickle. What a parse=true leg can NOT
give is the TRUE VALUE at defaults: the parser clobbers; the values come from
the device shadow, which is what the finder below names.

The float loads' addresses (1D1E08C0, 1D0B2800 ...) are PHYSICAL and the
device pointer is VIRTUAL (04142480); the instrument's "inside the device"
check compared them directly and printed "NOT inside it" - a units mismatch
in the check, not a finding about the loads. Not acted on; the finder does
not need it.

**Next (built after P8): the bool/loop shadow FINDER** - match the 40 packet
dwords against the device object (+0..0x4000) when a type-2 packet is walked,
log the offset that matches, count matches / misses / all-zero packets. A
match names ngpu_bool_off and ngpu_loop_off (= bools + 0x20) as VERIFIED
values, which is the exit condition for the shading subject: b4's value then
comes from the guest's own shadow at defaults, with the ring parser off.

**The table read against ourselves (coordinator, 10:55), and both branches
pre-registered before the re-run:**
- Three of the five rows are EMPTY, not passing: fps and p50 are the limiter
  reading itself back (a path twice as fast and one a hair under budget print
  the same 60.0 / 16.7), and hitches 0 vs 0 is an absence with no floor. The
  rule's "equal under the clamp" is the right DECISION; it is not evidence.
  In Bowerstone the entire informative content of the table is the TAIL, and
  the tail is the native path's.
- p99 and worst are ONE tail statistic, not two rows; "worst NOT SEPARATED"
  corroborates p99, it does not add a failure. Count the tail once.
- The separation is one spread wide: nearest edges 19.2 (plugin) vs 19.4
  (native), within-condition spreads 0.1 and 0.2. WORSE stands as written;
  "0.3 ms" is a reading to be re-taken, NOT a debt of known size - it must
  not travel between sessions as a figure.
- PRE-REGISTERED: if the native tail were a UNIFORM shift (+0.3 on every
  frame, a per-draw residual), the swap shadow and the remaining WC stores
  should move p99. If it is OUTLIER-driven (most frames identical, a few much
  worse - an EVENT cost), removing per-draw cost moves p50, which the clamp
  hides, and **p99 does not move at all**. The table already leans outliers
  (native per-window p99 up to 22.3 where the plugin stays under 19.6; worst
  26.8 vs 20.6: a uniform shift does not make a 6 ms worst). So **"p99
  unchanged after the swap shadow" is a PREDICTED outcome under the live
  hypothesis, not a failed fix.**
- The row is decided by ~54 frames per condition (p99 of ~300-frame windows,
  6 windows, 3 legs). Instrument, built next: every native frame over a
  threshold (18 ms) dumped whole - frame index, interval, native draws,
  stream and constant bytes, cache bytes built and copied, resolves - plus
  the COUNT of overrun frames per window (the value-independent signal:
  three frames or thirty). Overrun frames with ZERO upload bytes exonerate
  the texel writer (the one big raw WC store site) outright; that negative is
  reported, not dropped.

### RESULT (leg P8, ~10:27, native ON, profiled, build 10:24 with the swap shadow): both halves of the prediction

| quantity | P7 (before) | P8 (after) | predicted |
|---|---|---|---|
| DrawTranslated self | 13.4 % | **11.3 %** | down ~3 points net |
| the case-2 swap loop's leaf (present.cpp:7538 as built) | 1.8 + 0.8 + 0.7 % | **gone** | gone |
| a new store leaf | - | **DrawTranslated+313f @7575, 1.0 %** | "appears at roughly nothing" |
| frame ms p50 (profiled) | 17.7-18.2 | 17.6-17.9 | |
| xs bracket | 8.8-9.2 | 8.8 | |
| p99 ms (profiled windows) | 20.3-22.3 | 21.6-22.8 | UNCHANGED under the outlier hypothesis |

- Net: DrawTranslated -2.1 points (~0.4 ms), the old leaf gone AND a new one
  present (the store into the heap), so the saving is accounted for, not
  merely relabelled. The new leaf at 1.0 % (~0.2 ms for ~10 KB) is larger
  than "roughly nothing": a sequential WC store should run at line-fill
  rate; that residual is noted, not chased (see the line named below).
- p50 and the frame did not visibly move (within a window's noise) and
  **p99 did not move** - the pre-registered outcome under the outlier
  hypothesis, not a failed fix. The overrun-frame dump (next build) decides
  which frames make the tail.

**CS1 read against itself (coordinator, 11:00), and the consumer-side finding:**
- "One type-2 packet per ~10.6 draws" is a FLOOR: the walk logs the per-draw
  float loads at 0.13-0.15 per hooked draw, i.e. it sees roughly one packet
  in seven, so the true type-2 rate is plausibly ~1,300 a frame, one per
  ~1.6 draws - effectively per draw. Quote "at least ~195 a frame"; the
  next build prints the walk's own draw-packet count per window beside the
  hooked draws, so the coverage becomes a measured fraction.
- The sweep fails its magnitude check by ~10,000x: one sweep per ~1.09 s
  window, ~5 draws between it and the next type-2 packet, ~0.004 % of
  draws. It corrupts a handful of draws a second; it cannot make five
  vertex shaders sit in a reproducible garbage state. The mechanism is real
  and self-diagnosing (types 94/108/126/144 are impossible), and it is NOT
  the standing defect.
- **The standing defect is consumer-side, and the defaults path shows it by
  construction:** `ngpu_bool_off` defaults to 0 = "none known - they come
  from the ring", and the ring parser is retired and OFF at defaults. So at
  defaults SharedConstantsFor reads the eight bool words from device+0 (the
  object's header) and the 32 loop words from device+0x17A0, which is
  INSIDE the pixel-shader float block that starts at 0x1780. The translator
  is handed pointers and floats as booleans and loops on every draw. That
  is the fifth instance of a retired instrument wired into a live path
  (the design assumed the ring would supply the values). The finder names
  the shadow; until then b4's "20 % of the frame" was a header word.
- Next build (after the user's session): the matcher reports EVERY matching
  offset and whether it is stable across packets, scans +0..0x8000, keeps
  the all-zero count prominent (an all-zero payload matches everywhere and
  is never a hit); and the consumer-side print logs the words the
  translator is handed beside the last type-2 payload, counting agree /
  disagree. The finder is a VALUE match and address-free, so the
  physical/virtual units defect in the "inside the device" check cannot
  reach it.

## USER SESSION 2026-09-23 10:31 - NOT A MEASUREMENT LEG

fable2.exe launched for the USER to drive (PID 120996, plugin path
`ngpu_shadow=false` so the world draws correctly for navigation, no pad
script, no profiler, no HUD; log out/USER_saves_session.log), to make the
town and lake saves the parity legs need. The test lock reads "USER DRIVING,
do not launch" until 14:31. No number from this session is evidence of
anything: mixed input, the user's route, the plugin renderer. Their save
folder is theirs; a leg copies, never writes.

**P8 read against ourselves (coordinator, 11:10):**
- The decomposition SUMS by two routes: leaves 1.8 + 0.8 + 0.7 = 3.3 out,
  1.0 back in, net -2.3 points; DrawTranslated self 13.4 -> 11.3 = -2.1.
  Two routes within 0.2 points is what rules out relabelling.
- The saving is THREAD-level, not yet frame-level: frame p50 17.6-17.9 vs
  P7's 17.7-18.2 OVERLAP, so by our own rule NOT SEPARATED. The honest
  line: "-2.1 points of render-thread time, not yet visible in frame time"
  - consistent with the render thread leading by under a millisecond.
- The residual 1.0 % leaf: "0.2 ms for 10 KB is impossible" rests on the
  streamed-per-frame counter, a DIFFERENT subject. The block now counts its
  own bytes per window; the residual is judged against that, and the
  impossibility does not travel as a fact.
- Pre-registered and confirmed: p99 unchanged, so **the swap shadow is not
  the p99 fix and the failing parity row still owes in full - the tail is
  the only open item on the perf half, and nothing has landed on it yet.**
  The overrun-frame dump is its instrument.

**The sentinel class (coordinator, 11:05), fixed:** `ngpu_bool_off` = 0
meant "none known" AND was a readable address, so the unknown case did not
fail - it succeeded at the object header, silently, from the day the ring
parser was retired. Both offsets now default to -1 (unreadable); the unknown
path gives ZERO booleans / loops and COUNTS the draws ("NO BOOLEAN/LOOP
SOURCE this window: N draws ..."), a reported outcome. The class grep over
the INT32 cvars found no second member with a readable "none" sentinel
(ngpu_loop_off's 0x17A0 was a wrong guess, not a sentinel). Consistency
check kept: a constant header word is exactly what B2 saw - a flat lever,
sign alternating seven for seven - so B2's demonstration that the consumer
reads bit 132 survives; "which value is true" stays open. Loop counts: the
translator ANDs the loop constant with 0xFF for the count and takes bits
8-15 for the start (dxbc_translator.cpp ~1726-1745, the ISA's own field
layout), so a float read as a loop constant gives a count of its low byte -
0-255, never a billion - which is why nothing hung: not a clamp, the
hardware's field width. It absorbs the defect without counting it; the
sentinel counter above is the count now.

**Sequencing for the next table (the coordinator's constraint):** the
bool/loop fix lands FIRST (CS2 is a 60 s leg and the fix is two verified
cvar values), then ONE six-leg table per scene on that build, with the
plugin legs re-run as the CONFINEMENT control (they do not touch the
translator; matching across builds says our change stayed inside the native
path). Instruments in measurement legs: ngpu_log_consts=0 (finder and
consumer print off); the overrun dump stays on because the tail is the
subject and its cost is one clock read per frame - stated, not hidden.
The bounded set to re-walk after the fix: the 8 of 126 shaders that test a
bool, the 5 that test 132, the 5 VS using loop i0, and the 7 unreproducible
VS; the DXBC differential is translation-time and does not need re-taking.

**PRE-REGISTERED BEFORE THE BOOL/LOOP FIX LANDS (coordinator, 11:20):** the fix
changes which branches shaders take and how many iterations five VS run, a
per-draw cost that can reach frame time, so the tail is no longer "nothing
landed on it". Three legitimate outcomes for p99 on the post-fix table:
- IMPROVES => loop counts were inflating shader work (NOT the swap shadow
  working late: P8 already showed it leaves p99 unchanged).
- UNCHANGED => the tail is still unexplained; the overrun dump is the
  instrument.
- WORSE => the true loop counts are HIGHER than the garbage ones: we had
  been under-running shaders - a correctness gain that costs time.
What is not legitimate is choosing afterwards which one was expected.

The loop-count effect is READ, not estimated: count = the low byte of the
dword, and the low byte of an exactly representable float (0.0, 0.5, 1.0,
2.0 ...) is 0x00, so a slot holding a clean constant gives count 0 and the
loop never runs - the recorded "low/zero = bind-pose or collapsed" symptom,
arriving as a consequence. A computed value gives an arbitrary 0-255. The
consumer-side print in CS2 says which sits at 0x17A0.

STRUCK from the symptom record as unreachable by construction: "a high
count = a clamp or a hang". The ISA masks the count to 8 bits, so a
billion-iteration loop cannot occur through this path. The mask is CORRECT
CODE (dxbc_translator.cpp implements the field layout faithfully); the
defect was entirely upstream, and the field width is what kept a
data-corruption bug from presenting as a hang for days.

"VERIFIED offset" = an ACCEPTANCE TEST, not a matcher hit: with the new
offsets set, the consumer-side print must show the words handed to the
translator EQUAL to the last type-2 payload with the disagree count at or
near zero - end to end. Caveat, named: that test runs with the parser on;
defaults run with it off. The offsets are a property of the device object
written by the guest's own calls, so they should hold either way - an
assumption, stated. The defaults-side check that needs no parser (next
build): the bool and loop words read from the device must VARY across the
leg (real state) and must NOT equal the header words (device+0..); constant
= a dead slot, equal to the header = the offsets resolved back to where we
started. One line per window, in the configuration we ship.

THREE RENDERING STATES, named so no picture or shader verdict crosses them:
(1) before 2026-09-23 ~11:00: header words as booleans, PS floats as loop
counts; (2) sentinel build: ZERO booleans and zero-iteration loops, counted;
(3) post-fix: the guest's own words. Verdicts recorded under one do not
carry to another.

THE CONFINEMENT CONTROL'S CRITERION, stated first: apply the parity rule to
plugin-old (PB_off1-3, build 10:01) vs plugin-new (the post-fix build):
spreads overlapping on every row = confined; a row separating = something
leaked and the whole comparison is re-examined. Not "they look about the
same".

Distinct in the class record: ngpu_bool_off's 0 was a SENTINEL that read;
ngpu_loop_off's 0x17A0 was a WRONG GUESS. Both wrong, different kinds.

## CONTROL VALIDITY: WHAT IS STILL LIVE AT `ngpu_shadow=false` (read off the gates, 2026-09-23 11:40)

Prompted by the USER, playing the plugin-path build during the save session:
"0.2.12 has the video flashing it did not flash like this in prior versions"
- then "half the screen of the FMV playback". **Found by watching the screen
for ten minutes, which six legs of counters and three profiles did not
surface: every instrument built today measured something we had already
decided to look at.** Recorded beside the five counter-found defects as the
other half of the lesson.

The plugin condition of every parity table is `ngpu_shadow=false`. Is that
"the same binary with the feature off"? Enumerated, not assumed:
- Gated OFF by ngpu_shadow (first-line returns): ShadowPresent,
  ShadowDrawIndexed/Impl, ShadowDrawVertices, ShadowDrawUP (present.cpp
  12881, 12964, 13115, 13199, 13289). The plugin bridge callbacks are
  registered only by BindBridge(), inside the native EndFrame, under
  ngpu_bridge (default false): at the plugin condition the plugin's parser
  runs with NO callbacks. The untile lift is inside the native texture path.
- NOT gated by ngpu_shadow and live at defaults: the 1,858 mid-asm trace
  hooks (config/hooks/native_gpu_trace.toml; 309 read-only bodies in
  native_gpu_trace.cpp - no register writes); the census when its cvar is
  set (the leg runner sets ngpu_census=true in every leg, including the
  plugin legs: a ~1 ms diagnostic live in the control, stated); the
  profiler when FABLE2_PROFILE is set; a title-update check on the setup
  screen; app line tables (no runtime effect). No app-side mirror-copy path;
  the plugin's "mirror copies" counter reads 0 in every leg.
- The DLLs: rexgpu-xenos.dll and rexruntime.dll in this build are
  BYTE-IDENTICAL (SHA-256) to those in Releases/fable2recomp-v0.2.16-win-
  amd64.zip, the last release before v1.0.0 on the same day, and differ from
  v0.2.12's. The plugin that decodes and draws the FMV is the shipped one.
- The branch's app vs v1.0.0's: 11 setup/installer commits behind (the
  coordinator's read), and ahead by the native work + the trace hooks +
  the profiler + 45 lines of setup-screen title-update checking.

**Verdict so far:** at ngpu_shadow=false the FMV path reaches no native-GPU
code. The flash is either pre-existing in v0.2.16/v1.0.0 with these DLLs or
a timing effect of the read-only trace hooks. The test (after the user is
done, never before): the LOCAL v0.2.16 release build at the same FMV -
flashes there = pre-existing and the plugin condition stands as the
control; does not = build without the trace toml and re-test. Half-screen
= a pitch/height/tiling mismatch in the plugin's texture path (SDK code,
shared with NG2 - the coordinator takes it to the user). No six-leg table is
re-taken until this is settled.

**Control symmetry, verified from the legs' own lines (coordinator, 11:50):**
the census is live in BOTH arms - PB_off1/off3 log "ngpu_census = true ...
ngpu_shadow = false" and PB_on1/on3 the same set without the final
override; both arms print [ngpu-census] output. The Bowerstone table stands:
its control differs by ONE thing. Stated for the bar: the plugin condition
of every table is the shipped plugin + 1,858 read-only mid-asm hooks + the
census, present in both arms; a no-toml build bounds that when worth an hour.
The comparison build for the FMV test is v0.2.17 (local, fable2.exe SHA-256
b76eb049..., DLLs byte-identical to this build's and v0.2.16's); that
v1.0.0's plugin is the same is an inference from identical DLLs across 16/17
and eleven setup-only commits, labelled as one. The hooks' claim, tightened:
"no guest-visible state change" - across all 309 bodies, zero register
writes, zero guest store primitives (REX_STORE/StoreV/Store32/PokeV/WriteV),
zero memcpy/memset; they write host-side counters and samples only.

**SUPERSEDES "the table stands" (coordinator, 11:55; the rule this project
already paid for: A CVAR VALUE IS NOT A WORKLOAD).** The census cvar is
symmetric; the census WORK is not: the native subsystem adds its own
per-draw census entries (644 census lines in the off legs, ~1,500 in the
on legs), counted per draw, reported every 10 s - the cost lands in the
frame, not in the print. It inflates the native arm, the arm the open row
says is worse by 0.3 ms. So the Bowerstone table (PB_off1-3 / PB_on1-3) is
QUOTABLE FOR DIRECTION, NOT MAGNITUDE. The next table runs BOTH arms with
every diagnostic off - `ngpu_census=false`, `ngpu_dump_textures=0` (the
runner's base tune dumps 64 textures to disk per run in the native arm
only), `ngpu_dump_rts=0`, `ngpu_log_consts=0` - closer to what the user
ships; the overrun-frame dump stays on in both arms (one clock read per
frame, symmetric, stated). The native-only census cost then falls out for
free as PB_on(new) - PB_on(old), and only the COMPARISON carries over: the
absolute numbers move because ~1 ms leaves both arms.

**The hook claim, bounded rather than recalled:** the recompiler emits
exactly four guest store primitives (REX_STORE_U8/U16/U32/U64, counted in a
generated TU); none occurs in the 309 hook bodies; the native sources define
no guest store helper; no hook writes through a Host()/Phys()/VBase()
pointer. "No guest-visible state change" is closed the way the site count
was.

**The texture-dump hypothesis for the tail, tested from the artifacts (12:00):
DEAD.** In PB_on1/2/3 the 128 dump files (64 textures x 2) carry
modification times within three seconds BEFORE the log's "scene loading ->
world" line (10:13:30-31 vs 10:13:33; 10:17:15-17 vs 10:17:18; 10:20:59-
10:21:01 vs 10:21:02) - the first 64 textures, at load, about a minute
before the steady windows begin. They sit entirely inside the excluded
window and cannot be the tail. The per-draw census in the native arm remains
the live asymmetry. Right shape, right arm, right magnitude, wrong TIME: a
candidate that fits three ways still needs the fourth read.

**A class, recorded because both asymmetries point the same way:** the arm
under development is the arm you instrument, so diagnostics accrete on the
subject side and every A/B is biased against your own work - in a KNOWN
direction, which makes it correctable. Before any comparison, enumerate the
diagnostics live in each arm and expect the imbalance to favour the control.
It will recur on the town and lake tables, and on NG2's.

**Re-sequence (coordinator, 12:00):** the clean table FIRST, on today's
shading, no bool fix - it isolates the diagnostics (the census), hands over
their cost against the old table for free, and may settle the Bowerstone row
by itself; THEN the bool fix; THEN a second clean table. Two tables each
differing by one thing, not one differing by three. Binary discipline: the
PB legs ran on the 10:01 build (58e0d3b); the current exe (10:30) already
adds the swap shadow (P8: no p99 effect, pre-registered and measured). The
clean table runs on a rebuild of 58e0d3b in its own worktree so that it
differs from the PB table by the diagnostics ALONE; the runner takes a
BUILD_DIR override for it.

**Binary provenance (coordinator, 12:10):** no copy of the 10:01 build the PB
legs ran survives on disk (only the 10:30 exe and the v0.2.16 extract), so
the clean table runs a REBUILD of 58e0d3b in its own worktree, carrying the
caveat explicitly: same commit, rebuilt; the recompiler is not deterministic
(on the books), so shader artefacts may differ - "same source, probably same
binary", and the comparison is stated that way, not as byte-for-byte. The
current exe (10:30) is NOT used for it: P8's frame-level reading of the swap
shadow was NOT SEPARATED, which is not a licence to treat it as nil. From
this point every leg records size, mtime and SHA-256 of fable2.exe,
rexgpu-xenos.dll and rexruntime.dll in out/<TAG>.artifact.txt, and every
table quotes them. Today's artifacts, for the record: fable2.exe 83,675,136
bytes, 10:30:27, sha256 6bda9321fbe8db7d...; rexgpu-xenos.dll 7,277,568,
2e4ec3f16be3ccd1...; rexruntime.dll 11,039,744, 88e744239a8c8394... (the
two DLLs are the SDK's staged files, identical in every build today and in
the v0.2.16/v0.2.17 releases).

**THE NON-DETERMINISM CAVEAT IS RETIRED, MEASURED RATHER THAN ASSUMED
(12:20).** The coordinator asked what a rebuild actually re-runs. The build
script runs the recompiler on every build (~33 s, all phases through Write)
and ends with its own comparison: **"Codegen summary: 0 written, 561
unchanged, 0 deleted"** - it re-derives all 561 generated sources and finds
every one identical to what is on disk. That line appears 58 times across
today's 29 build logs and reads the same every time; not one build wrote a
generated file, and their mtimes are still 2026-09-22 17:30. So the
recompiler's determinism FOR THIS COMMIT AND MANIFEST is a measurement taken
29 times today, not a hope - and it is exactly the build-twice-and-hash test,
already run for free. Further: the recompiler's inputs (fable2_manifest.toml,
config/) are IDENTICAL between 58e0d3b and HEAD (`git diff --stat` empty), so
checking out the PB commit changes nothing it reads.
Consequence: the clean table rebuilds 58e0d3b **in the main worktree**, where
the generated sources are the very files the 10:01 build compiled; only the
app's own ~40 translation units recompile and link. A fresh worktree was the
wrong venue - generated/default is gitignored, so building there would have
re-run the full recompiler and 561 TU compiles, reintroducing the question
it was meant to settle; that worktree is removed. The residual caveat is
compiler/linker reproducibility on identical inputs, and the per-leg artifact
manifest records the hash either way.

**Positive control, stated once (coordinator):** rexgpu-xenos.dll
2e4ec3f16be3ccd1... and rexruntime.dll 88e744239a8c8394... are identical
across every build today AND across the v0.2.16 and v0.2.17 releases. The
plugin is a fixed point in all of this, so any difference between any two of
today's legs is in our app, by elimination - which retires a class of "maybe
the plugin changed" questions in advance.

**A NOUN CONFLATION IN THE CAVEAT ITSELF, and two more tail candidates
killed from logs already on disk (12:30).** "The recompiler is
non-deterministic, that is on the books" names the wrong tool. The books
(memory `build-step-not-deterministic`) are about **XenosRecomp, the SHADER
translator** - guest Xenos microcode to DXBC - whose Fable II corpus had four
containers that both crashed and produced, every produced output distinct.
They say nothing about the **PPC recompiler** (guest executable to C++, the
561 generated sources), which is the tool a rebuild would re-run and which
today's 29 builds measured as deterministic. Two tools, one noun; the caveat
was inherited from a subject that was never in the path.
- The shader translator IS in a live path (the in-app translator, vendored,
  runs at RUNTIME in the native arm). Its determinism in THIS build is
  bounded by the 09-22 DXBC differential: 406 translations compared between
  two independent runs (the plugin's dump and the app's re-translation),
  MATCH 406 / MISMATCH 0. An unstable container of the recorded class never
  repeats an output, so none of those 406 are of that class here. A bounded
  claim, not a 20-run per-subject census.
- **Runtime translation and PSO creation are NOT the tail:** across every
  steady window of PB_on1 and PB_on3 the counts are FLAT - vertex shaders
  loaded 32, pixel shaders 38 (leg 1) / 37 (leg 3), pipelines 81 / 79, with
  0 rejected and 0 fallbacks. Nothing is translated or compiled during the
  measured windows, so the classic multi-millisecond PSO-compile spike is
  excluded. Third candidate dead, again from artefacts already on disk.
- **But those counts DIFFER BETWEEN LEGS of the same condition** (38 pixel
  shaders / 81 pipelines in leg 1 against 37 / 79 in leg 3, same save, same
  stand, same build). So the native arm's workload is not identical leg to
  leg - one extra shader and two extra pipelines - which is a contributor to
  its wider spread and a thing to quote when the spread is read. The plugin
  arm has no such per-leg variation to report because it creates none.

**"561 unchanged" IS A CONTENT COMPARISON - read from the SDK's source, so the
determinism claim is closed (12:40).** codegen_writer.cpp's
`FlushPendingWrites` emits every file's content into `pendingWrites` and then
calls `WriteIfChanged(path, content)`, which is (file_io.h:50):
`if (auto existing = ReadFileBytes(path); existing && *existing == content)
return WriteOutcome::Unchanged;` - the bytes are generated and compared
in full. And the summary line itself separates the two cases the coordinator
named: it ends **"0 module(s) up to date"**, so the input-fingerprint stamp
(output_stamp.cpp, "unchanged modules skip codegen entirely") short-circuited
NOTHING - the module was re-derived and all 561 files compared, 29 times
today, every one identical. The caveat is closed, not narrowed.

**The oversize function is EMITTED, not skipped (same read):** codegen_writer
warns "Function 0x82242F10 is 2433598 bytes, exceeds max_file_size_bytes"
and then pushes the code into its bucket regardless; the partitioner simply
gets one oversized bucket. So there is no generated code the app lacks, and
the coincidence with the symbolisation trouble is ruled out rather than
assumed. Keep the warning beside the gap census as an EXTERNAL corroboration:
the toolkit's own mouth, 2.3 MB of generated source for that one function,
arrived at independently of the profiler.

**A note worth its own line (coordinator):** an isolation measure can
recreate the variable it was meant to remove - the fresh worktree chosen to
make the rebuild one-variable would have re-run the full recompiler because
generated/ is gitignored. Check what the isolation actually isolates.

**Operational guard before the checkout:** nothing is pushed, so the branch
tip exists in exactly one place. Tip recorded here before HEAD moves:
**native-gpu = e57a83dd7302394c310c53393c31d169f181c6c3** (the tip at the
moment HEAD moves; the earlier c302a81 in this line was two commits stale when
the checkout actually happened - record the tip AT the checkout, not before it). The tree is clean at that commit; after the clean
table the checkout returns by hash, not by branch name, and is verified.

### THE POSITIVE CONTROL THIS COMPARISON WAS MISSING (12:50): a guest-side invariant, carried in every table from now on

The coordinator asked for a quantity upstream of everything we change,
present in BOTH arms, to turn "these legs disagree" into "these legs
disagree about the thing under test". The plugin's readback landings per
swap is it (the same quantity that served as C1's workload guard), and it
is on disk for all six Bowerstone legs:

| leg | landings / window | swaps / window | **landings per swap** | native draws / frame |
|---|---|---|---|---|
| PB_off1 | 21,624 | 301 | **71.84** | n/a (native off) |
| PB_off2 | 21,618 | 301 | **71.82** | n/a |
| PB_off3 | 21,624 | 301 | **71.84** | n/a |
| PB_on1 | 21,620 | 301 | **71.83** | 2,111 |
| PB_on2 | 21,623 | 301 | **71.84** | 2,110 |
| PB_on3 | 21,546 | 300 | **71.82** | 2,069 |

- **The guest workload is the same in all six legs to 0.03 %** (71.82-71.84).
  The stand reproduces, both arms are doing the same job, and the parity
  comparison is about the thing under test.
- **The native draw count varies ~2 % between legs** (2,069 in leg 3 against
  2,110-2,111), and leg 3 is also the one with 37 pixel shaders / 79
  pipelines rather than 38 / 81 - so the two observations are one. The
  coordinator offered two readings; it is NEITHER cleanly. Not
  creation-order noise (the counts are flat within every window, not still
  climbing), and not a different scene (the guest-side invariant is
  identical). It is minor CONTENT variation inside the same stand - an NPC
  walking past, an idle animation - which changes what is on screen by ~2 %
  of draws and can need one more pixel shader, while leaving the guest's
  per-swap work unchanged. That is the size of variation the rule's spread
  is meant to absorb, and it is now quantified rather than assumed.
- **Carried in every table from now on, beside the binary hashes:** landings
  per swap per leg (the invariant), and native draws per frame per leg (the
  content-variation measure). A table whose invariant moves is not a parity
  reading at all.

**Three tail candidates dead today, none of them costing a leg, all from
artefacts already on disk:** the texture dumps (timestamps: written at load,
a minute before the windows), runtime shader translation and PSO creation
(counts flat through every window: 32 VS, 37-38 PS, 79-81 pipelines, 0
rejected, 0 fallbacks), and the plugin changing under us (DLL hashes
identical across every build today and both releases). Listed with their
refutations so they are not re-proposed. **The pattern is worth naming: the
logs we already had answered three questions we were about to buy legs for.**
Still live: the per-draw census asymmetry (the clean table removes it), and
the ~122 native texture creations per second that run THROUGH the measured
windows - right place, right time, right arm.

## THE TOWN PARITY TABLE — Bowerstone Market, n = 3 per condition (legs PC_off1-3 / PC_on1-3, 2026-09-23 11:36-11:49)

The gate-critical scene, from the USER's own save (card 2, "Hero 2 /
Bowerstone Market"), region asserted as `bowerstone_market` in every leg.
**Both arms run BELOW the 60 clamp here (50-57 fps), so unlike Bowerstone
cemetery these rows carry information rather than reading the limiter back.**
Binary: the 11:26 rebuild of 58e0d3b (artifact manifest per leg); ALL
diagnostics off in both arms (census, texture dumps, RT dumps, const log);
steady windows = the last six of each leg.

| leg | fps | p50 ms | p99 ms | worst | hitches | landings/swap | native draws/frame |
|---|---|---|---|---|---|---|---|
| PC_off1 | 48.4-54.6 | 18.1-20.4 | 20.5-28.7 | 33.3 | 0 | 62.88 | - |
| PC_off2 | 56.2-57.4 | 17.2-17.5 | 19.7-21.1 | 43.3 | 2 | **60.36** | - |
| PC_off3 | 49.7-55.6 | 17.8-19.8 | 20.8-28.9 | 33.0 | 0 | 62.87 | - |
| PC_on1 | 54.7-55.8 | 17.7-18.0 | 20.3-22.2 | 33.5 | 0 | 62.94 | 2,221 |
| PC_on2 | 40.5-56.6 | 17.3-23.2 | 24.3-46.7 | 74.2 | 8 | 62.67 | 2,204 |
| PC_on3 | 52.5-54.0 | 18.2-18.5 | 20.6-37.3 | 47.5 | 6 | 62.84 | 2,250 |

| row | plugin (medians) | native (medians) | verdict |
|---|---|---|---|
| fps | 51.0-56.7 | 50.4-55.2 | NOT SEPARATED at n = 3 |
| p50 ms | 17.4-19.4 | 17.9-19.2 | PARITY |
| p99 ms | 20.6-25.4 | 20.7-31.1 | NOT SEPARATED at n = 3 |
| worst ms | 33.0-43.3 | 33.5-74.2 | NOT SEPARATED at n = 3 |
| hitches/window | 0-2 | 0-8 | NOT SEPARATED at n = 3 |

**VERDICT: NO MEASUREMENT OBTAINED. This is not a parity reading, by our own
pre-registered rule, and it is a finding about the STAND rather than about
the native path.** Two reasons, either sufficient:
1. **The invariant MOVED** - landings per swap 62.67-62.94 in five legs
   against 60.36 in PC_off2. The rule was written before any leg precisely so
   it could not be softened afterwards: *a table whose invariant moves is not
   a parity reading at all.* It is the gate condition failing, not a caveat
   on a passing table.
2. **The test could not have separated anything.** The plugin arm ALONE spans
   48.4-57.4 fps between legs, so the stand's own variability exceeds any
   effect worth caring about. A test that cannot come out differently is not
   a test - and that applies to a null exactly as much as to a pass. "No
   difference found" would be a claim about the world; only "no measurement
   obtained" is supported.
(p50's spreads do nest, but a row that nests inside an uninformative table is
not a pass either.)
- **WHAT THE INVARIANT IS, named because two readings under one noun is how
  today's other mistakes started (coordinator's correction of his own
  claim):** after the cemetery it was called insensitive to content, because
  landings held to 0.03 % while native draws moved 2 %. That was an inference
  from ONE scene where content happened to be stable; insensitivity was never
  established. Here landings move 4 % with content. So it is **both** a
  comparability gate and a partial content measure - which means **a moving
  reading cannot by itself distinguish "the harness broke" from "a cart went
  past", and it must not be the SOLE gate.** It is kept as a gate (a move
  invalidates the table) and quoted beside the content measure, never alone.
- **What it does establish:** both arms sit at 50-57 fps in the market, the
  native path does not make the town worse, and the 09-22 plugin-only figure
  (~46 fps, n = 1, retired build) is not contradicted by either arm today.
- **To separate anything here** would need more legs (n >= 6) or a quieter
  stand within the town; that is the price of measuring in the scene that
  matters. Recorded as the honest outcome rather than a pass.

**What the town is FOR, decided before spending n >= 6 (coordinator, 12:00):**
- If the question is *"does the native path break the town?"* it is
  **already answered** - both arms at 50-57 fps, nothing catastrophic, and
  the 09-22 n = 1 ~46 fps is not contradicted. No further legs.
- If the question is *"parity to within X in the town"*, the n follows from
  the observed spread and with a ~9 fps range in one arm it will be large.
  **The better answer is then a QUIETER STAND, not more legs** - and that
  needs another save, which is the user's call. Recording the town as
  underpowered is a legitimate outcome, not a gap.

## THE LAKE PARITY TABLE — Bower Lake, n = 3 per condition (legs PL_off1-3 / PL_on1-3, 2026-09-23 11:52-12:20)

The user's own save (card 1, "Hero 1 / Bower Lake"), region asserted as
`bowerlake` in every leg; 58e0d3b rebuild; all diagnostics off in both arms.

**INVARIANT FIRST (the gate): landings per swap 79.02-79.11 (plugin) and
78.81-78.84 (native) - a total spread of 0.38 %. The gate PASSES**, and this
is a far more reproducible stand than the market (4 %): the native arm's draw
count is **317 per frame in all three legs, identical**, against 2,204-2,250
in the market. A quiet lakeside is what a measurable stand looks like.
- Honest wrinkle: the two arms' invariant groups do not overlap (79.0x vs
  78.8x, a systematic 0.3 %). It is tiny and consistent, so it reads as an
  arm effect rather than content - which means the quantity is not purely
  guest-side after all. Recorded, not explained; at 0.3 % it changes no
  verdict below.

| leg | fps | p50 ms | p99 ms | worst | hitches | landings/swap | native draws |
|---|---|---|---|---|---|---|---|
| PL_off1 | 60.0 | 16.6-16.9 | 18.6-19.0 | 25.0 | 0 | 79.11 | - |
| PL_off2 | 60.0 | 16.6-16.8 | 18.6-19.0 | 25.7 | 0 | 79.02 | - |
| PL_off3 | 60.0 | 16.6-16.9 | 18.6-19.0 | 24.8 | 0 | 79.07 | - |
| PL_on1 | 60.0 | 16.6-16.9 | 18.6-19.0 | 30.8 | 0 | 78.84 | 317 |
| PL_on2 | 60.0 | 16.6-16.8 | 18.6-19.5 | 20.0 | 0 | 78.82 | 317 |
| PL_on3 | 60.0 | 16.7-16.9 | 18.6-19.5 | 25.7 | 0 | 78.81 | 317 |

| row | plugin (medians) | native (medians) | verdict |
|---|---|---|---|
| fps | 60.0 | 60.0 | **EMPTY** - both at the clamp, which reads itself back |
| hitches/window | 0 | 0 | **EMPTY** - an absence with no floor under it |
| p50 ms | 16.6-16.7 | 16.6-16.8 | NOT SEPARATED at n = 3 |
| p99 ms | 18.6-18.9 | 18.9-19.1 | NOT SEPARATED at n = 3 (**the closest row**: the native spread sits entirely at or above the plugin's top edge, ~0.2 ms) |
| worst ms | 24.8-25.7 | 20.0-30.8 | NOT SEPARATED at n = 3 |

**Reading: in the lake the native path is at parity on everything this stand
can measure.** Unlike the market this table HAD power - the within-arm
spreads are 0.2-0.3 ms and the invariant holds to 0.38 % - so "not separated"
here is a real result rather than an absence of one. The only row with any
lean is the tail, where the native legs sit at or just above the plugin's
best, by about 0.2 ms; at n = 3 that does not separate, and it is the same
direction as the cemetery's tail row. **Clause A's perf half, on the user's
bar (parity), passes in Bower Lake; the town remains unmeasured (its stand is
too noisy) and the cemetery is direction-only.**

**THE GATE RULE, SHARPENED (coordinator, 12:25) - it needed a distinction it
did not have, and without it the lake table would void itself:**
- **WITHIN-ARM movement across legs = the comparability failure, and THAT is
  the gate.** The town: 60.36 against 62.67-62.94 inside the same set - one
  leg measured a different street. VOID.
- **A consistent BETWEEN-ARM offset = an arm effect, and it is NOT the gate.**
  The lake: 79.0x plugin against 78.8x native, systematic, same direction
  every leg, 0.3 %. PASSES.
Written the other way round ("the invariant must not move"), a systematic arm
effect would void every table this project will ever take. Both verdicts
above are unchanged by the sharpening - the rule now says WHY rather than
accidentally getting it right.
**Third consequence for the invariant, recorded before anyone leans on it:**
it can no longer certify "the two arms did the same work", only "the same to
within its own arm effect" - about 0.3 % here. That bound is the certificate;
a smaller effect than the bound cannot be certified by this quantity.

**THE SCOPE OF THE LAKE PASS, in the words it must travel in:** the lake
stand is **317 native draws per frame against the market's 2,204-2,250 -
SEVEN TIMES LIGHTER.** The native path's cost scales with draws, so **parity
is demonstrated where parity is cheapest, and is unmeasurable where it is
expensive.** "Passes in the lake" must never travel as "passes". What is
missing is a stand that is BOTH heavy AND quiet - dense geometry without
wandering NPCs, an interior perhaps - which is the test that would settle the
perf half properly and needs another save from the user. A choice about how
strong they want the evidence, not a problem.

**p99 AS A WATCH ITEM, with the third scene pre-registered:** the tail is
**0-for-2 on separation and 2-for-2 on direction** - the cemetery separated
native-worse (census-contaminated, so direction-only) and the lake does not
separate but sits entirely at or above the plugin's top edge by ~0.2 ms. Two
independent scenes leaning the same way is **more than two nulls and less
than a finding**, and it is the only row that has ever leaned, either way, in
any table. **Pre-registered: if a heavy-quiet stand ALSO leans native-worse
on p99, the tail is real and small; if it leans the other way, the pattern
was noise.** Written before the data that would confirm it.
