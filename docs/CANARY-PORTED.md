# Porting Xenia Canary's GPU work into the ReXGlue SDK

The SDK is a fork of Canary that stopped taking changes around **2026-08-01**.
Everything Canary has landed in `src/xenia/gpu` since then is a candidate for
porting; `tools/canary_backlog.py` lists them and reads its state from this
file.

**A commit with no line here reads as TODO, deliberately.** An unexamined
commit is not the same as one that does not apply, and the difference has to be
visible. When you decide a commit is not applicable, record it as SKIP *with a
reason* — silence is not a decision.

How the fork point was dated: probing distinctive changes against our tree.
`[GPU] Implement wide 1D texture support` (2026-07-31) is absent and
`[GPU] Host RT polygon offset` (2026-08-01) is absent, while everything before
2026-07-30 that was checked is present.

States: **PORTED** (in our tree and building), **SKIP** (does not apply, reason
given), **PARTIAL** (one backend done), **TODO** (not yet judged).

## A hard limit on parity: we have no shader sources

Canary keeps its EDRAM, resolve and clear shaders as `.xesli` sources and
compiles them in its build. **Our tree has none** — `find src -name '*.xesli'`
returns nothing, there is no shader build step, and what ships is prebuilt
bytecode headers plus two hand-written GLSL/HLSL files. So any Canary commit
that changes those shaders cannot be ported here without the shader toolchain.

**Update - the compiler is not the obstacle; the shaders are.** Canary's
sources are public, our header names map onto them one-to-one, and FXC (Windows
10 SDK) is the same compiler our headers were built with. `tools/build_shaders.py`
drives it, and ten shaders reproduce the shipped bytecode byte for byte apart
from the DXBC checksum.

But 83 of 93 differ in length, often hugely - `resolve_full_16bpp_cs` is 41,472
bytes here against Canary's 99,848. **The SDK's resolve and EDRAM shaders are
its own code**, not an older copy of Canary's. So a working shader compiler does
not unblock `437a7280c`: porting it means re-implementing its addressing scheme
on top of the SDK's own shader variants by hand, not dropping Canary's in.

### Why the shaders diverge: flat vs group-packed scaled addressing

Diffing the one shader ReXGlue *does* ship source for
(`resolve_downscale.cs.hlsl`) against Canary's `resolve_downscale.cs.xesl`
explains all 83 divergent shaders at once. Canary's own comment states it:

> The source is group-packed (`XeniaTextureGetResolutionScaledAddressing` in
> `texture_address.xesli`), **not a flat scale_x*scale_y expansion**, so this
> shader reverses that layout per output block.

The two projects lay scaled resolve memory out differently:

| | scaled address layout |
|---|---|
| **ReXGlue** | **flat** - `SharedMemory::kBufferSize * (scale_x * scale_y)`, address multiplied by the scale area (`d3d12/texture_cache.cpp:1151`, `pipeline/texture/cache.cpp:264`). Nothing in our tree mentions `ResolutionScaledAddressing`. |
| **Canary**  | **group-packed** - `XeniaTextureGetResolutionScaledAddressing`, used by `resolve.xesli`, `texture_load.xesli` and `texture_address.xesli`. |

That is why Canary's resolve shaders are so much larger: they carry the address
math to reverse the packed layout. It also means **every Canary resolve/EDRAM
shader change assumes an addressing scheme we do not implement**. Porting
`437a7280c` is therefore not "recompile some shaders" - it needs the addressing
architecture underneath it, or its scheme reworked onto flat addressing by hand.

What ReXGlue actually changed in `resolve_downscale`, for the record:

- rewrote it from `.xesl` to hand-written HLSL/GLSL (no xesl toolchain here);
- flat source addressing instead of group-packed;
- a 32x32 thread group, one thread per output pixel, with groupshared memory to
  coalesce 8/16bpp writes (Canary uses 128 threads striding over output dwords
  and no shared memory, because xesl byte buffers are dword-granular);
- kept `xe_downscale_half_pixel_offset`, which **Canary also has** - so on
  features Canary's version is a superset;
- dropped `xe_downscale_source_offset_bytes`, which Canary has and we lack.

So "implement ReXGlue's changes in the new Canary shader" is the wrong way
round for this file: Canary's is ahead on everything except the addressing. The
contained version of the job is to take Canary's shader and substitute flat
addressing for `XeDownscaleScaledBlockByte`. That is worth doing only alongside
the rest of the resolve family - a lone downscale shader in a different layout
from its neighbours is worse than either choice.

Three of the 54 are affected, and one of them matters a great deal:

- **`437a7280c` — "Use EDRAM layout with a single sample addressing scheme"**,
  2,628 lines over 11 shader files and both render-target caches. Its own
  message says the change cannot be split: *"The old 2x2 subdivision decodes
  intentionally aliased views into shuffled images. Individually changing
  components would just shuffle mismatched layouts, so we change everything at
  once."* This is the largest post-fork change and the one that touches the
  subsystem our flat-blue bug lives in, and it is exactly the one we cannot
  take. Worth raising upstream: shipping the shader sources, or the compiled
  outputs of this commit, would unblock it.
- `2b3f0cb45` (A8 resolves) and `fc48d37cd` (dead shader code) touch one or two
  shader files each and may be portable in part.

## Port in COMMIT-date topological order, not author date

`947075f88` is authored 2026-07-31 and `fbdb1f281` 2026-08-02, so author date
puts the wide-1D commit first - but it was committed *second*, and its diff
already assumes `coordinate_dimension`, which `fbdb1f281` introduces. Sorting by
author date hands you a dependent commit whose context will not match.
`tools/canary_backlog.py` now lists `--topo-order --reverse` with commit dates.

Checked while fixing this: `2ddc5ef73` (commit date 2026-07-30, just under the
cutoff) is already in our tree, so the ~2026-07-31 fork point holds.

## The remaining commits increasingly do not map, and the reason is the same

Probing the rest for the symbols they touch, a pattern shows up that is worth
stating before anyone reads a low "ported" count as slow progress. The SDK did
not just fall behind Canary - it **refactored the same areas**, so a growing
share of what is left has no counterpart to patch:

| Canary symbol the commit needs | in our tree? |
|---|---|
| `GetIntegerScaleBits` (0c843efb3, and 052cb95f2 / 6a4545208 behind it) | absent - the SDK does unsigned-biased scaling its own way |
| `FSI_AlphaToMask` (cb240560d) | absent - the Vulkan FSI alpha-to-mask feature was never here |
| `block_rt_0_alpha_tests_rt_written_end` (654a8cacf) | absent - our FSI block structure differs |
| `ac6_ground_fix` (3a44f20c7 replaces it) | absent - we never had the hack being replaced |

This is the same finding as the shaders, in the C++: **ReXGlue is a fork that
diverged, not a snapshot that is behind.** Where it diverged, "port the commit"
becomes "implement the feature Canary's commit assumes, then port the commit" -
a different and much larger job, and one that should be decided deliberately
rather than slipped in under a porting pass.

## The shader translators are five months more diverged than the GPU code

The GPU code forked around 2026-08-01. The **shader modification layout did
not** - our DXBC `Modification::kVersion` reads `0x20260226`, February, against
Canary's `0x20260716` *before* the last commit that touches it. `Modification`
is the packed key that decides which variant of a shader gets generated, so
porting anything that adds a mode to it means reconciling five months of
intervening changes to that structure first. Get it wrong and every shader is
generated wrong, not just the new path.

That is what blocks `cde5d85ec`, and it is worth knowing before anyone tries a
"quick" shader-translator port.

## Where the bug actually is (2026-09-04)

Not in the GPU code, and not anything Canary can fix for us. Measured with the
census in `tools/` and the `draw_census` cvar:

1. **~2,000,000 draws issued, essentially none dropped.** Geometry reaches the
   GPU and renders nothing. Not a skipped draw.
2. **NaN appears in the vertex shader float constants and then explodes** -
   0 -> 35,152 -> 1,982,280 values over 35 seconds, while nothing else moves.
   Skinned characters get their bone matrices through these constants; a NaN
   matrix collapses every skinned vertex while static geometry is untouched.
3. **The NaN sits in contiguous, matrix-shaped blocks** - `c0-c7`, `c28-c39`,
   `c113-c117`, `c255` - growing over time.
4. **The bit patterns are canonical quiet NaNs**: only THREE distinct values,
   `0x7FC00000` (1.1M), `0xFFC00000` (870k), `0x7FE00000` (6.7k). Not
   `0xFFFFFFFF` fill, not random - **this is arithmetic producing NaN**, from an
   invalid operation (0/0, Inf-Inf, sqrt of a negative, 0*Inf).

These constants are written by the GUEST; our GPU code copies them verbatim from
the register file. So the defect is in the **recompiled PowerPC floating-point
code**, which is why 26 Canary GPU ports changed nothing, why both backends fail
identically, and why nobody upstream reports this bug.

Ruled out along the way, each by measurement rather than reading: draws being
dropped, memexport, pipeline readiness, backface culling, depth rejection, the
`DrawExtentEstimator` call site, the DXBC `kMaxA` clamp, the `AllocFixed`
"already reserved" errors (Canary's code is character-for-character identical),
and any brightness regression from the ports.

| sha | state | note |
|---|---|---|
| `3ff230d23` | PORTED | Extended-range float16 in RT pack/unpack. Found independently from the TODOs before the history was available. DXBC + SPIR-V encoders, ROV pack/unpack, all six memexport cases, then the PSI clamp widened to ±131008. Measured: did NOT fix the flat-blue scene. |
| `da47dfaac` | PORTED | Signed round bias breaking memexport. Our tree had the exact bug: the bias overwrote `eM` and the add then read an uninitialised `round_bias_temp` (`PushSystemTemp` does not zero by default). Measured: did NOT fix the flat-blue scene. |
| `9e9d3cdd3` | PORTED | Out-of-bounds vertex fetch words clamp to 0. Our tree still carried the `FIXME(Triang3l): Bound checking is not done here`. DXBC done; SPIR-V side still open, so strictly this is PARTIAL for Vulkan. |
| `95545f8e7` | SKIP | Over-invalidation of bulk fetch register writes. **We do not have this bug** — our `WriteRegisterRangeFromMem` already computes `end_index = start_index + num_registers - 1`, which is the corrected form. Porting it blindly would have introduced an off-by-one. |
| `ec5e0f40e` | PORTED | Don't drop swizzles from dummy texture headers. Matters here: the commit names Bink movies' dummy alpha plane, and this title plays Bink. |
| `ac4225068` | PORTED | 20e4 bit span comment ([f24_shift, +10) -> +24) in both translator headers. Comment only, taken for parity. |
| `d0dd98923` | PORTED | Honor force_bc_w_to_max. The bit was already in our fetch constant in xenos.h and simply never read; now carried into the D3D12 sampler key and applied to BorderColor[3]. Vulkan side still open. |
| `92ada8ebc` | PORTED | Scalar ALU swizzle with three-source vector ops: `b` is Z, not X, when the co-issued vector op uses source 3. We had X unconditionally, so the arithmetic was wrong quietly. Translator and interpreter both. |
| `f3e42609a` | SKIP | Mantissa placement in CPU Float7e3To32. **We already have the fix** - our xenos.cpp reads `mantissa << 16`. Nothing to do. |
| `e87321b06` | SKIP | 2x-as-4x cvar typo. We do not have the `debug_msaa_*` cvar at all, so there is no typo to fix. Would come with the cvar if it is ever added. |
| `052cb95f2` | BLOCKED | Integer scale channel width. Depends on `GetIntegerScaleBits`, which we do not have - it arrives with 0c843efb3. Port that first. |
| `6a4545208` | BLOCKED | Walk the guest swizzle for integer scales. Same dependency as 052cb95f2. |
| `197929d96` | PORTED | Fall back to point sampling for non-filterable formats. Asking D3D12 for a linear filter on a format that does not report D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE is undefined and can come back black. Cleared the matching TODO in our tree. |
| `437a7280c` | BLOCKED | EDRAM single sample addressing. **Not portable without shader sources** - see the section above. The biggest post-fork change and the one closest to our bug. |
| `fc48d37cd` | BLOCKED | Remove num_format/decode cvars and dead shader code. Touches one shader source; the cvar removal half may be portable alone. |
| `2b3f0cb45` | BLOCKED | Select alpha for A8 resolves. Touches two shader sources. |
| `5cf409d6e` | SKIP | C++20'ify Part 1 - a style sweep, no behaviour. |
| `a5a18f5c7` | SKIP | Random optimizations - no behaviour change. |
| `0d395ce9a` | SKIP | Move debug cvars to a new TOML block. Config plumbing for cvars we do not have. |
| `22708301b` | SKIP | Initial XPS support - kernel/memory feature, not a GPU correctness fix, and the kernel half is outside the plugin. |
| `4a863a0e1` | PORTED | Base map selection with a separate mip page: with kBaseMap filtering and a real base page the base map IS level 0, so keeping the fetch constant's min level sampled the wrong subresource. |
| `8486e97a0` | PORTED | Locked-mip unnormalized fetches sample in that mip's grid. The denominator was always the base level size, so reductions after the first read garbage. DXBC done; SPIR-V open. |
| `fbdb1f281` | PORTED | Two-component tfetch1D coordinates: a 1D fetch with a multi-component coordinate addresses a 2D grid, so all seven dimension switches now use coordinate_dimension. Foundation for 947075f88. DXBC done; SPIR-V open. |
| `947075f88` | PORTED | Wide 1D textures (>8192) mapped onto a 2D grid. CPU side (texture cache no longer rejects them, IsWide1D/Get1DWidth, guest layout keeps the row count) AND the DXBC coordinate remapping, which needed OpRoundPI adding to our assembler. SPIR-V side still open. |
| `e519d59e4` | PORTED | Stacked-texture inter-layer lerp base. A lerp is first + (second - first) * factor; ours added the difference to the SECOND layer, so every inter-layer blend was wrong. |
| `2d5b41080` | PORTED | Clamp the stacked-texture layer index so an Inf/NaN coordinate cannot select an undefined array layer. |
| `4aeb518c9` | PORTED | Scalar maxas/maxasf clamp a0 to [0,255], not [-256,255]. Only the scalar site - the vector maxa nearby legitimately allows negatives and was left alone. |
| `25597a546` | PORTED | force_depth_clamp cvar plus its use in the D3D12 pipeline cache. Defaults off, so inert unless asked for. |
| `61a8aa360` | PORTED | spirv_disable_rounding_mode_rte, so RenderDoc can debug our SPIR-V (it cannot handle the RoundingModeRTE capability). Useful for the character-drawing investigation later. |
| `aed81ca93` | PORTED | Guest access resolution note in shared_memory. Comment only in the GPU half; the substantive part of that commit is in the memory subsystem, outside the plugin. |
| `0c843efb3` | BLOCKED | Round normalized unsigned fixed fetches. Needs `GetIntegerScaleBits`, which we do not have - the SDK implements unsigned-biased scaling differently. Blocks 052cb95f2 and 6a4545208 behind it. |
| `cb240560d` | BLOCKED | 2x MSAA alpha-to-coverage sample layout. Needs `FSI_AlphaToMask`, a Vulkan FSI feature our tree never had. |
| `654a8cacf` | BLOCKED | FSI 2x-as-4x sample mask. Needs `block_rt_0_alpha_tests_rt_written_end`; our FSI block structure differs, so the phi it adds has nowhere to go without reworking that region. |
| `3a44f20c7` | SKIP | Replaces the AC6 ground hack with scalar approximation rounding. We do not have `ac6_ground_fix`, so there is nothing to replace. |
| `77597d62a` | PORTED | Back-face stencil ref/mask only when drawing ONLY back faces - culling the front alone is not enough, the back must not also be culled. |
| `1fdbe569e` | PORTED | TRANSFER_WRITE -> TRANSFER_WRITE barrier so consecutive uploads to the same image are ordered. |
| `53061c63f` | PORTED | Alpha blend slot uses alpha-equivalent factors. Alpha is scalar, so the hardware reads a _COLOR factor there as the matching _ALPHA one; we were using the colour map for both. |
| `0f2980de4` | PORTED | Per-axis gradient exponent bias during fetch. The code was already in our tree behind three `#if 0` blocks and a FIXME; Canary enabled it, and so have we. No `#if 0` remains in that file. |
| `2b071d9b0` | SKIP | Invalidate user clip plane constants. Not applicable: Canary moved clip planes into their own constant buffer, ours keeps them in the system constants, which `UpdateSystemConstantValues` rebuilds every draw - there is no stale buffer to invalidate. |
| `e6bdb0fdf` | SKIP | Skip draws with surface_pitch == 0. **We already do this**, in both backends (`surface_pitch_is_zero`). |
| `4cc584f47` | BLOCKED | Constant-alpha blending in the RTV path. Needs a new provider capability `IsAlphaBlendFactorSupported()` in ui/d3d12, which we do not have. Portable, but it is a three-file job (provider + command processor + pipeline cache) rather than a patch. |
| `f25003c0e` | PORTED | Clamp the copy for scaled-resolve mips (D3D12 half). The placed footprint is the guest mip reduced then scaled while the subresource is the base scaled then reduced, so the deepest mips could overrun an uncompressed destination. |
| `7cd47947b` | PORTED | Promoted tfetch1D layouts selected at runtime: a tfetch1D promoted by its swizzle may still carry a 1D fetch constant, whose width is one 24-bit field rather than two 13-bit ones. Needed 947075f88's DXBC half first. |
| `3254ac20f` | PORTED | Cube auto-LOD sampled with implicit LOD + bias. Explicit cube gradients pick the wrong mip on Vulkan, so a cube fetch with auto-LOD and no register gradients now biases the implicit LOD instead. |
| `658cdbb5d` | SKIP | Disable anisotropy for ineligible formats. **Already ours** - our check already sits after the aniso computation and already disables aniso; Canary was moving theirs to where ours already is. |
| `084f14ec4` | SKIP | VK_EXT_custom_border_color for YCbCr borders. **Already ours** - we read customBorderColors / customBorderColorWithoutFormat and there is no TODO left in that function. |
| `0d3878176` | SKIP | Float controls on geometry shaders. **Already ours** - we read the device properties locally and add the extension, capabilities and execution modes; Canary only cached them as members. |
| `31168682b` | BLOCKED | draw_resolution_scaled_texture_offsets cancelling itself out. The cvar exists here only in the DXBC translator - the SPIR-V side never implemented the feature, so there is no bug to fix. |
| `3eab2b8b3` | PORTED | Tessellated triangle strips and fans. Strips and fans are now accepted for tessellation and converted to triangle lists at runtime, since no prebuilt index buffer exists on that path. Cleared the matching TODO. |
| `090cecd1b` | SKIP | Vertex kill (oPts.z). **Already ours** - we test bits 0:30 of the kill value, store -1 to the cull distance in AND mode and NaN the position W in OR mode. Only the member naming differs. |
| `2a6e9f4e2` | SKIP | depth_float24_convert_in_pixel_shader on Vulkan. **Already ours** - wired through the Vulkan render target cache, command processor and pipeline cache (two sites). |
| `66779fb87` | BLOCKED | Adaptive tessellation. Half of it is **already ours** - we set `ExecutionModeVertexOrderCw`, which is the winding fix. The other half reads the patch index from the hull shader output and needs `kConstantBufferTessellation`, a constant buffer our SPIR-V translator does not have at all (ours has System/FloatVertex/FloatPixel/BoolLoop/Fetch and nothing else). |
| `710102115` | SKIP | Bind shared memory persistently for texture loads and resolves. A Vulkan descriptor-management refactor, 328 lines, with no correctness symptom named in the commit - and our binding model differs (`buffer_relative_offset` does not exist here). Reworking descriptor lifetime on a backend that does not run this game, for no stated defect, is not a good trade. |
| `cde5d85ec` | BLOCKED | Host RT polygon offset for Z-fighting decals. Adds three DepthStencilMode values and widens the bitfield, i.e. it changes the shader **Modification** layout - and ours is `0x20260226` against Canary's `0x20260716` before this commit. See the section above. It is also cvar-gated off by default and targets decals in 494707EE and 41560881, not this title. |
