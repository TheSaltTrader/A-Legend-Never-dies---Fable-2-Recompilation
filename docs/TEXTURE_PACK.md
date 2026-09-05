# Texture pack on Fable II

Ported from the NG2 port on 2026-09-05. The GPU plugin cvars were already
present in Fable II's build - the two ports share one SDK source tree - so
nothing on the plugin side had to change. What was missing was the settings
that drive them.

## Status

**Dumping WORKS and is verified. Decoding is NOT correct for this title yet.**

Wired through `fable2_settings.cfg` and the generated tuning file, exactly as
in NG2, with four menu rows (texture folder, cache size, dump, use pack):

    texture_dump=1
    texture_path=C:/somewhere
    texture_cache_mb=2048

Verified by READBACK rather than by the settings existing - `cache/fable2_tuning.toml`
carries `texture_dump = true`, `texture_dump_path`, and the two
`texture_cache_memory_limit_*` values, and the log confirms the plugin took
them. A 200-second run dumped **554 textures** plus `index.txt`.

Remember from NG2, and it is still true here: **a GPU plugin cvar cannot be
passed on the command line.** It only arrives through `cvar::LoadConfig`
deferral, i.e. the generated tuning TOML. `--texture_dump` does nothing.

## FIXED: the decode was ignoring endianness

The dump records nine fields per texture and the decoder was reading all nine -
then **using eight**. `endianness` was parsed into a variable and never applied.

Fable II's DXT1 art carries `endianness = 1` (8in16, swap bytes within each
16-bit word). A DXT1 block opens with two 16-bit colour endpoints, so skipping
that swap byte-reverses every colour: the output keeps its shapes and comes out
in psychedelic colour. NG2's textures are `k_8` / `k_8_8_8_8` and needed no
swap, which is why the gap never showed there.

`swap_endian()` now runs on the raw bytes before untiling, covering kNone,
8in16, 8in32 and 16in32.

    DXT1 textures   before      after
    noise             41          0
    flat               2          2
    dead right edge    -          2   (of 252 - rare, not systemic)

A 512x512 skybox that decoded as coloured static now decodes as a pink sunset
over mountain silhouettes.

## The "dead right edge" is not a bug

Two DXT1 textures decode with a black right quarter. It reproduces from a
gameplay dump, so it is not partial residency, and it is NOT a decode fault:

* every block's tiled source offset is in range (0 of 16384 out of range), and
  the banded texture and a clean one of the same size compute identical offsets
* no block in the region is zero bytes
* alpha is 255 across the whole image, so it is not 1-bit DXT1 transparency
  being flattened to black by an RGB conversion

Non-zero blocks whose colour endpoints are black decode to black. The region
genuinely encodes black - atlas padding or an unused quarter. 2 of 290.

## Replacement is DEFINED but NOT IMPLEMENTED

`texture_pack_path` is a cvar the plugin defines and never reads - zero uses
outside its own definition. So a "use texture pack" switch would do nothing.
The menu row was removed rather than shipped greyed out, and the tuning file no
longer sends the cvar, because a value in there that nothing reads looks
load-bearing and is not. The setting is kept for when the plugin side lands.

Implementing it means intercepting the texture upload, loading pack/<id>.png,
and converting to what the pipeline expects - a real GPU-side feature, not a
wiring job.

## How the wrongness was originally hidden

`tools/upscale_textures.py` runs and reports `decoded=332 ... failed=55`,
writing 593 PNGs. **That is not evidence of anything.** NG2's tool was verified
against `k_8` and `k_8_8_8_8`, which is what that title uses. Fable II is
mostly **`k_DXT1`** (256 of the decoded files), a BLOCK-compressed format whose
tiling works in 4x4 blocks rather than pixels.

Two measurements, and the second one is the honest one:

* A "does it have structure" metric (variance surviving an 8x downscale) said
  84% of DXT1 decodes were fine. **That metric is wrong for this question** - it
  detects structure, not correctness.
* Actually LOOKING at the highest-scoring DXT1 output shows psychedelic
  colours and a black band down the right quarter - the signature of a wrong
  stride, decoded at the wrong width.

So: the raw bytes and the key are being captured correctly, and the offline
untile/decode needs a block-format path before any of this is usable. Do not
trust `decoded=N` as a success criterion; open the pictures.

## Next

* Fix `GetTiledOffset2D` reproduction for block-compressed formats (bytes per
  block is 8 for DXT1, 16 for DXT4/5, and the tile maths is per block).
* `fmt49` appears in the dump and has no name in the tool - report it rather
  than guessing.


## k_DXN and k_DXT5A (added for Fable II)

NG2 is DXT1/DXT4_5, so the tool never grew the BC4-family formats. Fable II's
gameplay dump is **319 DXT1, 169 DXN, 75 DXT4_5, 3 DXT5A** - DXN is the second
most common format in the title and every one was skipped.

Both reuse the BC4 alpha-block scheme already implemented for DXT4_5:

    k_DXT5A (BC4)   8 bytes   one channel
    k_DXN   (BC5)  16 bytes   two of those, red then green

DXN stores a normal map's X and Y only, so Z is rebuilt as
`sqrt(1 - x^2 - y^2)`. Leaving blue at zero would look plausible in a thumbnail
and be wrong for anything that reads it.

    decoded         403 -> 575
    skipped_format  191 -> 19

**Verified against what the format MEANS, not a generic metric.** A correct
tangent-space normal map sits near R128 G128 B255. Across all 169 DXN textures
the means are **R 126, G 126, B 251**, with B >= 200 in 169 of 169 - and opening
one shows the expected lavender field with embossed belts, buckles and cloth
folds. A structure-or-noise heuristic could not have told that apart from
garbage; the format's own signature can.

## What is NOT done, and it is the same as NG2's list

Replacement is still not implemented in the plugin - `texture_pack_path` is
defined and never read, in the SHARED SDK, so neither port has it. NG2's design
for it stands: a pack texture needs its own resource at the new size, an
stb_image decode, a direct upload bypassing the load shader, and an SRV whose
format and swizzle match the replacement rather than the guest format.

Also inherited from NG2's notes, and it applies here: **the plugin and the
runtime must be built from the SAME source tree.** A source-built
rexgpu-xenos.dll against a stock rexruntime.dll exits during startup with no
error. Both are staged together here, which is why that has not bitten.
