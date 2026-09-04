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
