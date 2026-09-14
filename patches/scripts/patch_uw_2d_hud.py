"""s77 - fable2_uw_2d_k (double, 0 = off): Fable II's 2D HUD (health bar,
prompts, text) is drawn by vertex shaders that carry ONE pixel-to-clip
constant, c8 = (2/1280, -2/720, -1, 1) - the 2026-09-14 census found no
ortho matrix in any of its UI shaders. At ultrawide the presenter stretches
the 16:9 render to the window, so the HUD stretched with it. When k is set
(the app sets it to (16/9) / display aspect while the world is live and
ultrawide is on), every draw whose used constant 8 matches that pattern gets
its x scale and x offset multiplied by k: the HUD lands in a centred 16:9
band and survives the stretch at its true proportions. Full-screen effects
use c255 and pre-transformed vertices, and are untouched. APPLY ONCE
(requires patch_uw_2d_census.py)."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
q = open(Q, encoding="utf-8").read()
assert "fable2_uw_2d_k" not in q, "already applied"

def rep(old, new):
    global q
    assert q.count(old) == 1, ("expected 1, found %d: %s" % (q.count(old), old[:90]))
    q = q.replace(old, new)

rep('''REXCVAR_DEFINE_INT32(fable2_2d_census, 0, "GPU/D3D12",''',
    '''REXCVAR_DEFINE_DOUBLE(fable2_uw_2d_k, 0.0, "GPU/D3D12",
                      "Fable II ultrawide: scale the 2D HUD's pixel-to-clip constant (c8) x by this factor "
                      "(16:9 / display aspect) so the HUD keeps its proportions; 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(fable2_2d_census, 0, "GPU/D3D12",''')

rep('''    // [2d-census] Fable II: how do the UI shaders carry their transform?
    if (REXCVAR_GET(fable2_2d_census) > 0) {
''', '''    // [uw-2d] Fable II HUD at ultrawide: compress draws that carry the
    // pixel-to-clip constant c8 = (2/W, -2/H, -1, 1) to a centred 16:9 band.
    // The values are tested in the REGISTER FILE (cached memory) and only the
    // scaled ones are written into the upload buffer: reading them back from
    // that write-combined mapping cost 3 fps in the market (s77 -> s78).
    {
      const double kd = REXCVAR_GET(fable2_uw_2d_k);
      if (kd > 0.05 && kd < 1.5 && std::fabs(kd - 1.0) > 1e-3) {
        const auto& bmu = float_constant_map_vertex.float_bitmap;
        if ((bmu[0] >> 8) & 1ull) {
          float c8[4];
          std::memcpy(c8, &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * 8],
                      sizeof(c8));
          const bool pixel_scale = c8[0] > 1e-3f && c8[0] < 2.5e-3f && c8[1] < -1.5e-3f &&
                                   c8[1] > -4e-3f && std::fabs(c8[2] + 1.0f) < 0.02f &&
                                   std::fabs(c8[3] - 1.0f) < 0.02f;
          if (pixel_scale) {
            // c8's packed position = the number of used registers below 8.
            const uint32_t pos = rex::bit_count(bmu[0] & 0xFFull);
            float* out = reinterpret_cast<float*>(const_cast<uint8_t*>(float_constants_begin)) + pos * 4;
            const float k = static_cast<float>(kd);
            out[0] = c8[0] * k;  // x scale
            out[2] = c8[2] * k;  // x offset (-1 -> -k keeps the band centred)
          }
        }
      }
    }
    // [2d-census] Fable II: how do the UI shaders carry their transform?
    if (REXCVAR_GET(fable2_2d_census) > 0) {
''')
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: fable2_uw_2d_k")
