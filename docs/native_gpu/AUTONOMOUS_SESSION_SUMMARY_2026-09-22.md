# Autonomous session summary — Fable II native (2026-09-22, claudecode-b8)

User left fully autonomous. Everything below is LOCAL, nothing published, the
native-gpu branch stays unpushed (RELEASE_GATE holds). Saves were backed up
bit-for-bit before any save work.

## 1. Shipping line (done, verified)
- **v1.0.0 PUBLISHED** (setup-reopen crash fix + ultrawide-in-setup fix, both
  verified; published asset byte-complete; 0.x->1.x auto-update comparison
  proven correct). Save crash diagnosed + user-confirmed (a genuinely damaged
  3rd-party save; removing it loads the rest). Disk crisis resolved (121 GB).

## 2. THE PERF BASELINE - the finding that anchors the native case
Plugin (current shipping renderer), guest fps from the log swap line, proven
settings from the log echo. Full detail in PERF_BASELINE_2026-09-22.md.
- **Cemetery (light): 60 fps CAPPED**, and 1x==2x (byte-identical frame profile)
  => GPU has headroom; the 60 is the patch's software cap. A capped run can NOT
  tell you the ceiling (I retracted a "CPU-bound" read that leaned on it).
- **Bowerstone Market (busy town), hero STANDING, 0 hitches: ~46 fps, p50 ~21.5
  ms, GPU ~30%.** This is the valid ceiling read: BELOW the cap (finding its own
  ceiling) with the GPU idle => **the town is CPU/EMULATION-bound, not GPU-bound.**
  21.5 ms/frame vs the 16.67 ms 60-fps budget. **The 46->60 gap is exactly the
  emulation cost (PM4 parse, page-watch, game-thread) that going native removes.**
  This is the measured validation of the whole "replace emulation" direction.

## 3. Native build - WORKS
Fresh native-gpu worktree builds cleanly (needed the game XEX copied into
assets/; Plume prebuilt at NativeGPU/build/plume/plume.lib). 308/308, native GPU
backend ON. So the native renderer is buildable + iterable here.

## 4. FIRST CODE BRICK cut: the texture_conversion lift
Per NATIVE_LAYER_PLAN (game-agnostic layer reusing the plugin's PURE logic).
In native_gpu_present.cpp GetTexture, behind **`ngpu_use_sdk_untile` (default
OFF = control is the same binary)**: for the plain untile+endian case, use the
SDK's `rex::graphics::texture_conversion::Untile` + `CopySwapBlock` keyed by
`rex::graphics::FormatInfo::Get` instead of the hand-written tiled loop.
Bias/exp/scale/DXT3A stay on the hand path.
- **It COMPILES and LINKS.** That is the real, established result: the SDK's
  texture_conversion (Untile/CopySwapBlock/FormatInfo::Get) is linkable from the
  app, confirming the plan's premise that the pure translation can be reused
  directly (not just inside the plugin).
- **Byte-identity is a HYPOTHESIS, not yet established** (corrected — I first
  over-claimed it). The SDK's Untile + GetTiledOffset2D are compiled into
  rexruntime.lib; their source is NOT in the tree, and the hand path's local
  TiledOffset2DOuter/Inner are a different factoring. So whether the tiled paths
  produce identical bytes CANNOT be proven by reading - only the runtime
  dump-compare (the OWED validation) can. Do not call it "safe/identical" yet.
- **A real bug was found + fixed by reading before trusting it:** the lift's
  hand-rolled LINEAR branch dropped the l.bx0/l.by0 base offset, so it read the
  wrong rows for linear packed-mip-tail levels (util.h: linear DXT world
  materials use packed tails heavily). Fixed to add the offset exactly as the
  hand loop does. Switch defaults OFF so the shipping binary was never affected.
- OWED (next, and the ONLY proof of correctness): run-validation - boot the
  native build, dump textures switch OFF vs ON (ngpu_dump_textures), confirm
  byte-identical on a scene that exercises tiled AND linear-packed-tail levels;
  then the DXT3A path (SDK ConvertTexelDXT3AToDXT3, a REAL change vs the R8
  hack). Edit plan in TEXTURE_CONVERSION_LIFT_PLAN.md.

## 5. Recommended next priority (for the user + coordinator)
The lift is a CORRECTNESS brick and it's byte-identical, so it does NOT move the
46 fps. **The 46->60 town win requires the D3D9-HOOK path (M4_DESIGN) - intercept
the ~20 driver calls BEFORE any PM4, off the bridge.** The current native path
still RIDES on emulation (hybrid), so it can't beat the plugin yet. The measured
baseline (S2) says the prize is real and CPU-side; the hook bring-up is the work
that claims it. That is the big next piece and it is a multi-session build.

## Hazards recorded this session
- make_release --force rmtree's Releases\<ver> (had the user's 6.5 GB install) -
  use --releases-dir a clean path.
- The AI-Vision MCP pad tool ignores its button arg (always sends A) - drive via
  the pad-script FILE instead.
- 5 shaders make XenosRecomp emit 32-34 GB HLSL (unbounded CF walk) - bound it +
  a hard output-size cap before any full re-translation (list at
  D:\ng2_frameinterp\work\xenosrecomp_runaway_output.txt).
