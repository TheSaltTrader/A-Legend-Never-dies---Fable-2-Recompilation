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

Two of 252 still show a dead right edge and are not explained yet.

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
