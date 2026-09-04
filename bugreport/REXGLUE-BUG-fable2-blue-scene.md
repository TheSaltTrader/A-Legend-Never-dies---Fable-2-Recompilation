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
