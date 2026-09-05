# Fable II: 3D scene renders flat blue after the first region load (both backends)

**SDK:** v0.10.0 (`c94f5eb`), Windows amd64, `rexgpu-xenos.dll`
**Title:** Fable II GOTY, `4D5307F1`, disc build `v0.0.0.26`, media `716F0A0D`
**Host:** Windows 11, NVIDIA RTX 5090

## Summary

A few minutes into gameplay the 3D scene stops drawing and becomes a flat,
saturated blue (RGB ≈ 21,21,225). **The UI layer keeps drawing correctly on top
of it** — the loading-screen tip text is crisp and legible over the blue — and
the guest keeps running normally. It never recovers.

Xenia Canary plays the same disc, with the same community patches, straight
through this point. Since `rexgpu-xenos` is a Canary fork, and the fault is
present on BOTH backends, this looks like a divergence in the SHARED GPU code.

## It is not a hang

This is the part most likely to be misread, so it is worth stating plainly:

- The guest's presence heartbeat (`XGIUserSetContextEx`) keeps ticking once a
  second, indefinitely.
- ~510 APCs/second keep completing, at a perfectly steady rate.
- The GPU keeps compiling new pipelines while the screen is blue.
- Zero `[FATAL]`, zero unregistered functions, zero access violations, no
  ring-buffer failures, and no failed asset loads (the only failing opens are
  language packs not present on this disc and a title update that does not
  exist).
- With the D3D12 debug layer enabled (`d3d12_debug=true`) the validation layer
  reports **nothing**.

What stops is GPU *output*, not execution.

## Reproduction

100% reproducible here.

1. Extract the GOTY disc; run the title to the main menu.
2. **Continue** into an existing save (New Game also reaches it, but character
   select needs a stick deflection that a scripted A-press will not supply).
3. Walk for a few minutes until the first region transition.
4. The scene turns flat blue and stays blue. Measured: blue from 246 s to 898 s
   in a 15-minute run — it never clears.

Onset is sharp. The world renders perfectly (snow, fire, particles, NPCs) at
239 s; thirteen seconds later it is solid blue. The only activity logged in that
window is **four new graphics pipelines being compiled**, then nothing but APCs
and the heartbeat.

## It is NOT backend-specific

The plugin was rebuilt from source with `-DREXGLUE_USE_VULKAN=ON` and the same
reproduction run on the Vulkan backend. **The blue is identical there, and
character meshes additionally stop drawing altogether** - Vulkan is strictly
worse, not different.

Both backends failing the same way puts the defect in the **shared** GPU code
rather than in either backend's render-target path. For scale, the shared cache
here is 1,388 lines (`src/graphics/pipeline/render_target/cache.cpp`) against
Canary's 1,757 (`src/xenia/gpu/render_target_cache.cc`).

## What was ruled out

Every GPU/kernel cvar where Xenia Canary differs from this SDK's defaults was
aligned to Canary's values and tested against the reproduction — **still blue**:

| cvar | Canary | SDK default |
|---|---|---|
| `execute_unclipped_draw_vs_on_cpu` | true | false |
| `clear_memory_page_state` | false | true |
| `readback_memexport` | false | true |
| `primitive_processor_cache_min_indices` | 4096 | 0 |
| `anisotropic_override` | -1 | 3 |

Also tested individually or in combination, all still blue:
`render_target_path_d3d12` = rov / rtv / default, `readback_resolve` =
none / some / full, `d3d12_readback_resolve` = true,
`gpu_allow_invalid_fetch_constants` = true / false, `resolution_scale` 1 and 2,
and with every community patch disabled.

The community patches are **not** the cause (the bug predates enabling any of
them) but the "1280x720 Resolution" patch changes its *extent*: without it the
blue covers only the top ~45% of the frame with black below; with it, the whole
frame. That size relationship suggests the affected surface is being sized or
placed against the render width.

## Lineage note

The shipped plugin registers 19 cvars that exist only on Canary
(`readback_resolve`, `clear_memory_page_state`, `present_letterbox`,
`gpu_3d_to_2d_texture`, …) and 5 that exist only on Xenia master and have since
been renamed on Canary (`d3d12_readback_resolve`, `d3d12_readback_memexport`,
`d3d12_tiled_shared_memory`, `native_2x_msaa`,
`query_occlusion_fake_sample_count`) — so it forked from Canary around that
rename.

Relative to current Canary, the SDK source is missing 37 GPU cvars, several of
them in exactly the area this bug lives in:

    force_depth_clamp
    no_discard_stencil_in_transfer_pipelines
    depth_bias_shader_offset
    draw_resolution_scale_threshold
    debug_msaa_2x_as_4x
    gpu_allow_invalid_upload_range

`src/graphics/d3d12/render_target_cache.cpp` is 5,799 lines against Canary's
6,655 in `d3d12_render_target_cache.cc`, and the shared cache is 1,388 against
1,757.

## Also worth fixing: Vulkan is not built on Windows

(Independently of this bug - the Vulkan path did not fix it, see above.)

`REXGLUE_USE_VULKAN` defaults `OFF` on Windows, so the shipped
`rexgpu-xenos.dll` contains no Vulkan support at all (zero occurrences of the
string). Rebuilding from source with `-DREXGLUE_USE_VULKAN=ON` works with no
extra dependencies — Vulkan-Headers and glslang are already vendored — and
produces a plugin with both backends. Shipping that by default would give
Windows users a second render path to fall back on.

Note also that `LoadGpuPlugin` is only ever called with the default `"any"`
backend from `ui/rex_app.cpp:317`, and `"any"` picks D3D12 first, so an
application cannot select Vulkan through `RuntimeConfig::gpu_plugin` — it has to
call `LoadGpuPlugin(name, "vulkan")` itself. A `gpu_backend` cvar honoured by
ReXApp would be friendlier.

## Also worth fixing: FSR and CAS are compiled out, and need not be

(A second packaging issue, in the same spirit as the Vulkan one above, and
independent of the blue scene.)

`present_effect` advertises exactly one value, `bilinear`, in the shipped
build. That reads as "this runtime has no upscaling filter", and it is what
sent this project's settings menu down a long detour. It is not true.

The FSR 1.0 (EASU/RCAS) and CAS pixel shaders are **already built and committed
to the tree**, for both backends:

    src/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_easu_ps.h
    src/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_fsr_rcas_ps.h
    src/ui/shaders/bytecode/d3d12_5_1/guest_output_ffx_cas_*.h
    src/ui/shaders/vulkan_spirv/guest_output_ffx_*.h

They are gated on `REX_HAS_FIDELITYFX_SDK`, which `src/ui/CMakeLists.txt` sets
only when `REXGLUE_FIDELITYFX_SOURCE_DIR/sdk/include` exists — i.e. only after
`REXGLUE_ENABLE_FIDELITYFX=ON` has done a full (`GIT_SHALLOW OFF`) FetchContent
clone of the FidelityFX SDK and built `ffx-api`.

**But the spatial path needs nothing from that SDK.** No file under `src/ui/`
includes a FidelityFX header outside `<ffx_api/...>`, and every `<ffx_api/...>`
include and call site is guarded by the *separate* `REX_HAS_FIDELITYFX_RUNTIME`
define, each with a pure-arithmetic fallback (see
`QueryTemporalFsrRenderResolutionFromQualityMode`). Only the temporal `fsr2` /
`fsr3` modes actually need the library.

So one define gates two unrelated things, and the expensive half decides for
both. Splitting them makes the spatial upscalers available with no fetch, no
`ffx-api` target, no extra DLL and no shader compiler:

```cmake
elseif(REXGLUE_FIDELITYFX_SPATIAL_ONLY)
    target_compile_definitions(rexui PRIVATE REX_HAS_FIDELITYFX_SDK=1)
```

Verified here: with that one branch added, `present_effect` goes from
`bilinear` to `bilinear cas fsr fsr2 fsr3`, `present_cas_additional_sharpness`
/ `present_fsr_sharpness_reduction` / `present_fsr_quality_mode` register, and
the build is otherwise unchanged (`FidelityFX: OFF` still reported, nothing
fetched). `fsr2`/`fsr3` remain selectable and degrade to spatial FSR, which is
what the runtime already warns they may do.

Worth considering shipping the spatial effects on by default: for a 720p-era
guest on a modern display the output is always being upscaled, so `bilinear`
is the one choice a user would never deliberately make.

Note this also changes `GuestOutputPaintConfig`'s layout and
`Presenter::Effect`'s values, so it is an ABI break for anything compiled
against the headers without the define — an argument for deciding it once, in
the shipped package, rather than leaving it to each consumer.

## Attachments worth requesting

- Screenshots of the same scene immediately before and after onset.
- 429 shaders dumped at the transition (`dump_shaders`).
- The four pipeline hashes compiled at the moment of onset.

---

# UPDATE 2026-09-04: root cause located, and the GPU premise above is WRONG

Everything above still describes the symptom accurately, but its conclusion —
"a divergence in the SHARED GPU code" — is now disproven. The GPU is drawing
exactly what it is told to draw. It is being told to draw NaN.

## What was measured

Instrumenting the vertex float-constant upload
(`D3D12CommandProcessor::UpdateBindings`) and counting the values actually
handed to the GPU:

    VS CONSTANTS: 2275685 uploads, 261223088 values | NaN 18967897 (7.26%), Inf 0
    VS CONSTANTS NaN: 36 of 256 constants affected, first was c9
        | c0-c7, c9, c28-c39, c72-c74, c76, c93, c104-c107, c113-c117, c255
    VS CONSTANTS NaN patterns: 0x7FC00000 x8995088, 0xFFC00000 x8732176,
                               0x7FE00000 x1200846

Three things matter here:

  * The affected constants are **contiguous four-register blocks** (`c0-c7`,
    `c28-c39`, `c104-c107`, `c113-c117`) — the shape of transform matrices,
    not of scattered scalar parameters.
  * The NaN count is **zero until gameplay is actually reached**. A run that
    sits on the title screen reports `NaN 0 (0.00%)` for its whole length, and
    reports `memexport 0`. The NaN arrives with the world.
  * The bit patterns are dominated by **canonical quiet NaNs**, which is what
    an invalid operation produces — not a fill pattern.

## Proof that the NaN is the CAUSE, not a side effect

A census can only show that NaN arrives, never that it matters. So the
constants were repaired at the upload site (`diag_vs_const_nan_fix=2`,
substituting the matching row of an identity matrix for any NaN) and the same
330-second schedule was replayed.

  * `diag_vs_const_nan_fix=0` — flat saturated blue, tutorial banner on top.
    (`out/shots/nan2_14.png`)
  * `diag_vs_const_nan_fix=2` — **Old Bowerstone renders**: buildings, snow,
    cobblestones, the brazier fire, falling-snow particles.
    (`out/shots/fix2_14.png`)

Same build, same schedule, same shader cache; the only difference is whether
NaN reaches the GPU. That settles causality.

The identity-row substitution is a *probe, not a fix* — it assumes matrices are
4-register aligned, so later frames distort badly as the guess goes wrong
(`out/shots/fix2_16.png`). It is not proposed as a workaround.

## Where the NaN is NOT coming from

Ruled out by reading the code rather than by guessing:

  * **Extended-range float16.** `build_vupkd3d128` FLOAT16_4 maps half
    exponent 31 to float32 exponent 143 — a finite value up to 131008, which
    is the Xbox 360 behaviour. It does not produce Inf/NaN. Correct already.
  * **Saturating float→int conversion.** `rex::ppc::simde_mm_vctsxs` handles
    NaN→0 and saturates to `INT_MAX` rather than returning x86's
    `0x80000000` indefinite. Correct already.
  * **The GPU port backlog.** 26 Canary GPU commits were ported with no effect
    on this symptom, which is consistent with the fault being CPU-side.

## What the FP trap says

Running the recompiled code with MXCSR's IM bit clear
(`REX_TRAP_FP_INVALID=1`) and recording the faulting opcode at each distinct
site gives 53 sites. Decoded, the busiest are:

    1955302 x  F3 0F 5B DB   cvttps2dq   sub_8220D058+0x24B
     182064 x  0F C2 8F ..01 cmpps LT    sub_82E1A9C0+0x621
     162062 x  0F 5F 96 ..   maxps       sub_8220D058+0x4D6
      64976 x  0F C2 C5 01   cmpps LT    sub_82DE9120+0xAE43
      41690 x  0F C2 D5 02   cmpps LE    sub_821DC3A8+0x4D5

This is mostly **noise, and it is important to say so**:

  * `cvttps2dq` is simde emulating a per-lane vector shift (`vslw`) through
    floats. For a shift of 31 the intermediate is 2^31, which is out of signed
    range, so it raises a spurious #I — and still computes the correct answer.
  * `cmpps` with the LT/LE predicates, and `minps`/`maxps`, raise #I on a
    **quiet** NaN. They are NaN *consumers*, faulting because a NaN is already
    in the data.

Almost no `divps`, `sqrtps` or `subps` fires. So the NaN is not being created
in bulk by arithmetic at the point it is consumed; it is propagated from
somewhere upstream. That is the open question.

## Open question

Which guest write first puts a canonical QNaN into the constant registers.
The trap cannot answer it as built, because it cannot distinguish an
instruction that *created* a NaN from one that merely *touched* one.
