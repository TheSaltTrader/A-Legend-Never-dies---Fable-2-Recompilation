# HANDOVER — Xenos->native census + the porting phase (2026-09-22, claudecode-b8)

START HERE next session. This is the pickup doc for the game-agnostic conversion.
Everything is LOCAL and UNPUSHED on branch `native-gpu` (both repos); nothing ships
until RELEASE_GATE passes.

## Where you are
The USER's method (verbatim intent): a systematic, game-agnostic process that
enumerates EVERY Xenos call and ports each to native, so any 360 title works by
construction and what is NOT ported is COUNTED. That machine - the CENSUS - is
built and proven. The next phase is PORTING handlers down the lists it names,
validated replay-differentially.

## What exists (commits, local/unpushed on native-gpu)
- `6b42d1b` texture_conversion lift (first handler; ngpu_use_sdk_untile, default off)
- `86f31ca` census foundation: denominator generated from the SDK enums
  (`tools/native_gpu/gen_census.py` -> `src/native_gpu_census_subjects.inc`, 1,439
  subjects), `src/native_gpu_census.{h,cpp}`, two axes (ported / verified), four
  failure-mode defenses.
- `b20db4b`,`017eb78` instrument PM4, registers, primitives, vertex (SEEN axis via
  RingParse + MapVertexFormat). `70084dc`,`7b79c7b` docs + the boundary/denominator
  caveat in the census output.
- `36aba28` `SelfTestUntile` - the offline replay-differential harness + first
  VERIFIED result (see below) + this handover.
- FULL LOCAL/UNPUSHED CHAIN on native-gpu, nothing pushed (origin/native-gpu does not
  exist; RELEASE_GATE holds): 6b42d1b -> 86f31ca -> b20db4b -> 017eb78 -> 70084dc ->
  7b79c7b -> 36aba28.
Read `CENSUS_FOUNDATION_2026-09-22.md` next to this file for the full census.

## The census, verified (run with FABLE2_TUNE="ngpu_census=true")
Real named data: registers RB_MODECONTROL/RB_SURFACE_INFO/SQ_PROGRAM_CNTL/
VGT_INDX_OFFSET/PA_SU_SC_MODE_CNTL...; PM4 SET_BIN_MASK/WAIT_REG_MEM/EVENT_WRITE;
primitives kTriangleStrip/List/Point/Rect. ngpu_census.txt is written next to the exe.

## FIRST TO PORT (the census names it; ordered by traffic)
The unported queue, highest-hit first, is the work order. BUT note the register
finding below - some are already ported and only need MarkPorted.

## KEY FINDING - the 5 "first registers" are ALREADY applied natively
RB_MODECONTROL (Reg(0,0x2208) -> edram_mode), PA_SU_SC_MODE_CNTL (Reg(0,0x2205) ->
cull/winding), RB_SURFACE_INFO (36 uses), SQ_PROGRAM_CNTL, VGT_INDX_OFFSET are all
read+applied by the native path today. Their census `ported=0` is an UNDERCOUNT: I
added See() for registers but never MarkPorted() at the apply sites. So step 1 of
register porting is CHEAP: call MarkPorted at each apply site (which is also where
you name what it was verified against). Do not re-port what is already ported;
audit first, mark, then port the genuinely-missing ones.

## VALIDATION - replay-differential, and its real obstacle (named, per the method)
- WORKS for tiling, and it is a DIFFERENTIAL not a self-test (the distinction matters):
  `SelfTestUntile()` compares the hand tiled-address math against the SDK's
  `texture_util::GetTiledOffset2D` - which is the PLUGIN's own tiling code (shared,
  Xenia-based), NOT expectations we wrote. So a wrong hand implementation scores FAIL;
  it is oracle-grade, not "rigorous and worthless". **RESULT (2026-09-22): PASS,
  552,960 (x,y,pitch,bpp) offsets compared, 0 mismatches** - the SDK path's addressing
  is IDENTICAL to the hand loop's. Fires from RingSetReg (a provably-running site;
  BindBridge, the first hook, was gated behind ngpu_bridge and never ran). Trigger it
  with FABLE2_TUNE="ngpu_census=true;ngpu_native_draws=true;ngpu_shadow=true"; result in
  the [ngpu] log.
- SCOPE, kept honest: this verifies the tiled ADDRESS MATH only, NOT the full untile
  (format decode + copy callback + offset/pitch). Do NOT mark any texture-format subject
  VERIFIED from this - it is a component result. The full differential (native `Untile`
  OUTPUT vs the plugin's texture decode on recorded inputs) is STILL OWED, and it needs
  the plugin's per-texture output extracted (the harder oracle - see the obstacle below).
- OBSTACLE for REGISTER apply (name it, do not paper over): a register's DECODE is
  shared (both native and plugin use registers.h `reg::` structs - nothing to diff),
  and its APPLY (fields -> D3D12 state) is native-specific with the plugin's apply
  NOT callable in isolation (compiled into rexgpu-xenos.dll). So per-register
  replay-differential of the APPLY has no isolated plugin oracle; the only differential
  is FRAME-LEVEL (ngpu_truth vs the native shadow), which needs the native draw path
  running. That is a real finding about the oracle for the register surface, distinct
  from the texture surface where the SDK pure function IS the isolated oracle.

## DELIBERATELY PARKED (with reasons - do these with FRESH context)
- Shader-op SEEN instrumentation: needs microcode parsing (or the SDK AnalyzeUcode)
  - careful; a mis-parsed op count poisons the census. Coupled to the shader-translator
  integration.
- Resolve RT-format (color/depth) SEEN instrumentation: needs decoding the
  RB_COPY_DEST_INFO / RB_COLOR_INFO bitfields for the format; a mis-decoded value
  going INTO the census is the one failure that destroys its credibility. Do not rush.
- Handler PORTING at scale: correctness-critical; a wrong decode renders a plausible-
  but-wrong picture that no counter and no conservation check can catch (SUCCESS AND
  STABILITY ARE NOT CORRECTNESS). Port with the SelfTestUntile-style harness or
  replay-differential validation READY FIRST, one handler at a time, ported->verified
  only when the differential passes and you say what it was compared against.

## The two disciplines that already caught real errors here - keep as default
- CONSERVATION on every count that moves: if an off-census bucket falls, the
  recognised bucket must RISE by the same amount in the same run. Both falling = LOSS
  (it caught the 767-register over-narrowing today: recognised fell 838->71 while
  off-census fell 5403->89 - BOTH fell, so 767 real registers were SILENCED not fixed;
  a real fix would show recognised RISING by ~ the off-census fall. Reverted.)
- COHERENT off-census is a FINDING, not noise: `0x2010-0x202D` (real writes to indices
  register_table.inc doesn't name) is a measured counterexample to "the denominator is
  all subjects" - UNRESOLVED (undocumented regs / addressing / our decode). Named in the
  census output, not filed under desync garbage.
