# texture_conversion lift — edit plan (2026-09-22, claudecode-b8)

Goal (game-agnostic, per NATIVE_LAYER_PLAN reading i): replace the native path's
HAND-WRITTEN untile + endian + format decode in native_gpu_present.cpp GetTexture
with the SDK's PURE, game-agnostic logic - "what we learn from Xenos" as logic,
not runtime. PURE constraint: if any piece reaches for RegisterFile / SharedMemory,
STOP and write the coupling down. (None of the below does - all byte-in/byte-out.)

## The two sides (both read)
SDK (RexBlue/.../pipeline/texture/): PURE, no device/memory deps.
- `conversion.h`: `Untile(out, in, UntileInfo*)`; `UntileInfo{offset_x, offset_y,
  width, height, input_pitch, output_pitch, input_format_info, output_format_info,
  copy_callback}`; `CopySwapBlock(endian,out,in,len)`;
  `ConvertTexelCTX1ToR8G8`; `ConvertTexelDXT3AToDXT3`.
- `info.h`: `FormatInfo::Get(uint32_t gpu_format)` -> the format's block size etc.
  This is the mapping the hand path open-codes as `bw`/`bpb` in its switch.

Hand path (native_gpu_present.cpp): GetTexture parses fc[] (tiled, pitch, format,
endian, base, dim, w, h), a switch format->RenderFormat+bw+bpb, then the untile
LOOPS (~5216-5300): per block TiledOffset2DInner (tiled addr) -> endian swap
switch (per bpb) -> padded dst row; plus a DXT3A special (8B block -> 16 R8) and
bias_lane (SNORM sign flip). Author's warning: "every currently-working format
depends on this loop" - so this is validated surgery, not a blind swap.

## The edit
1. `#include "rex/graphics/pipeline/texture/conversion.h"` and `.../info.h`.
2. GENERIC path (the block-to-block loop): replace TiledOffset2D + the endian
   switch with one call:
   `texture_conversion::Untile(dst, src, &UntileInfo{ offset_x=l.bx0*bw,
   offset_y=l.by0, width=l.lw, height=l.lh, input_pitch=l.pitch_blocks*bpb
   (tiled) / l.row_bytes_src (linear), output_pitch=row_bytes,
   input_format_info=FormatInfo::Get(format), output_format_info=Get(format),
   copy_callback=[endian](o,i,n){CopySwapBlock((xenos::Endian)endian,o,i,n);} })`.
   Keeps `format`->RenderFormat for the D3D12 texture; the untile/endian become
   the SDK's. Verify Untile handles the LINEAR case (tiled==0) or keep the linear
   memcpy branch.
3. DXT3A (format 58): the hand path is an R8 EXPANSION hack (ngpu_dxt3a). The SDK
   converts DXT3A->DXT3 (BC2). Prefer the SDK path: RenderFormat BC2_UNORM +
   `ConvertTexelDXT3AToDXT3` as the block callback. THIS MAY FIX DXT3A properly
   rather than the R8 workaround - but it changes the target, so it is its own
   validated step, behind the existing ngpu_dxt3a switch so OFF stays today's
   behaviour (control = same binary).
4. CTX1 (if present as a fetch format): `ConvertTexelCTX1ToR8G8` callback.
5. bias_lane (SNORM): keep as the post-pass it is unless FormatInfo carries
   signedness; do NOT fold it into the swap blindly.
6. UNCHANGED: the resolve-target substitution, GPU-written-texture decline,
   descriptor/cache/reuse logic, the DXT3A round-trip and RT-dump instruments.

## Highest-signal target
Format 49 (BC5/DXN normal maps) = the black-terrain class. If its bug is in the
hand untile/endian, `Untile` with `FormatInfo::Get(49)` should fix it. This is
the reason the plan calls texture_conversion the first brick.

## Validation (NEEDS a game boot - interactive / user-authorised, not unattended)
Per RELEASE_GATE + claudecode-76: no native change is trusted until measured
against the reference frame (ngpu_truth / the game window). Do the edit behind a
switch where possible so the control is the same binary; compare the terrain and
the format-49/DXT3A textures on the defect mask before vs after. Build is offline
(no boot); the RENDER comparison is the gate and it waits for the machine.

## RUN-VALIDATION RECIPE (turnkey - 2026-09-22, ready when the machine is free)
Status: build DONE (exe relinked 17:51:46, native GPU backend ON). Static review
DONE: found+fixed a linear-branch offset bug; tiled byte-identity is a HYPOTHESIS
only a dump-compare can settle (SDK Untile/GetTiledOffset2D are in rexruntime.lib,
not source). Steps:
1. Give the fresh worktree build a config: copy the MAIN repo's working
   fable2_settings.cfg (game_path + saves) into
   out/build/win-amd64-Release/ so it boots the loaded save, not the setup screen.
   Verify no other game process first (one-game-at-a-time); close only MY PID.
2. Run A (control): set `ngpu_dump_textures 64`, `ngpu_use_sdk_untile 0`. Boot,
   reach the SAME gameplay scene, quit. Move out/build/.../ngpu_textures -> ngpu_A.
3. Run B (lift): same scene/save, `ngpu_dump_textures 64`, `ngpu_use_sdk_untile 1`.
   Move ngpu_textures -> ngpu_B.
4. Compare the untiled OUTPUT files (the *.rows* names carry base/w/h/format/
   endian/tiled, so they match by name across runs):
     for f in ngpu_A/*.rows*; do cmp -s "$f" "ngpu_B/$(basename "$f")" || echo "DIFF $f"; done
   Byte-identical (no DIFF) => tiled+linear BASE-level byte-identity confirmed for
   the dumped formats. Any DIFF => the SDK tiled math differs from the hand loop -
   a real finding, keep the file pair.
   Also confirm the raw `src` dumps (no .rows suffix) are identical A vs B (same
   textures, sanity that the scene matched).
5. COVERAGE GAP to state plainly: the dump fires only at L==0 (base level), where
   bx0/by0==0. So it does NOT exercise the linear-PACKED-MIP-TAIL path (L>0,
   bx0/by0!=0) - the exact path whose offset bug was fixed by reading. Runtime
   proof of that path needs a mip-tail-aware dump (dump L>0 too) or a world-
   material scene whose visible correctness depends on packed-tail linear mips.
   Do not report "byte-identical everywhere" from a base-level-only dump.
6. Then, separately, the DXT3A step (SDK ConvertTexelDXT3AToDXT3, a REAL change) -
   its own before/after behind ngpu_dxt3a.
