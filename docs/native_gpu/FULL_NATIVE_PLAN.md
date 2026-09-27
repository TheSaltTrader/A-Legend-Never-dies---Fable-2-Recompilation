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

## The measure of "100%"

A census, not a claim. Two numbers, driven to zero:
1. **PM4 packets the game writes that no replaced function accounts for.** Attributed from the ring write pointer at
   the entry and exit of every hooked XDK function (NG2's method: engines that build packets with computed opcodes
   cannot be attributed by call counts).
2. **Draws the native front end did not issue itself** (compared per frame against the bridge, which stays alive as
   the reference until the end).
Then: rexgpu-xenos.dll not loaded, no ring buffer allocated, and the picture compared frame by frame with the bridge
build (the reference) at Bower Lake, Bowerstone Market, Fairfax and a cutscene.

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
