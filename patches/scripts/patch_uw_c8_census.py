"""s80d - which c8 draw is the scene-transition fade? The textureless rule
(s80) never fired: Fable II's fade samples a texture. Every c8 draw (the HUD
family) is now logged the first time its (vertex shader, pixel shader) pair
is seen, with the pixel shader's texture count, the first two textures'
sizes and formats from their fetch constants, the draw's host vertex count
and the pixel shader's float-constant use - up to 80 pairs. Matched against
a recording of a load, the pairs first seen at the fade identify it.
Diagnostic only (a small static set lookup per c8 draw). APPLY ONCE
(requires patch_uw_2d_hud.py)."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
H = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\include\rex\graphics\d3d12\command_processor.h"
q = open(Q, encoding="utf-8").read()
h = open(H, encoding="utf-8").read()
assert "uw_diag_vertex_count_" not in h, "already applied"

def rep(s, old, new):
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    return s.replace(old, new)

h = rep(h, '''  uint32_t frame_draws_ = 0;
''', '''  uint32_t frame_draws_ = 0;
  // [uw-2d] The current draw's host vertex count, for the c8 census.
  uint32_t uw_diag_vertex_count_ = 0;
''')

q = rep(q, '''  // Update constant buffers, descriptors and root parameters.
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
''', '''  // Update constant buffers, descriptors and root parameters.
  uw_diag_vertex_count_ = primitive_processing_result.host_draw_vertex_count;
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
''')

q = rep(q, '''          // A solid fill (no texture sampled) is a scene-transition fade or a
''', '''          // [uw-2d] c8 census: each (vs, ps) pair once, with its textures.
          if (pixel_scale) {
            static std::unordered_set<uint64_t> seen_pairs;
            const uint64_t ps_hash = pixel_shader ? pixel_shader->ucode_data_hash() : 0ull;
            const uint64_t pair = vertex_shader->ucode_data_hash() ^ (ps_hash * 0x9E3779B97F4A7C15ull);
            if (seen_pairs.size() < 80 && !seen_pairs.count(pair)) {
              seen_pairs.insert(pair);
              char tex_desc[160] = {0};
              int td = 0;
              uint32_t ntex = 0;
              if (pixel_shader) {
                const auto& tb = pixel_shader->GetTextureBindingsAfterTranslation();
                ntex = uint32_t(tb.size());
                for (size_t i = 0; i < tb.size() && i < 2; ++i) {
                  xenos::xe_gpu_texture_fetch_t fetch;
                  std::memcpy(&fetch,
                              &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                                                      tb[i].fetch_constant * 6],
                              sizeof(fetch));
                  td += std::snprintf(tex_desc + td, sizeof(tex_desc) - size_t(td), " t%u:%ux%u fmt %u",
                                      tb[i].fetch_constant, uint32_t(fetch.size_2d.width) + 1,
                                      uint32_t(fetch.size_2d.height) + 1, uint32_t(fetch.format));
                }
              }
              const auto& psb = pixel_shader ? pixel_shader->constant_register_map().float_bitmap : bmu;
              REXLOG_INFO("[uw-2d] c8 draw first seen: vs {:016X} ps {:016X} textures {}{} verts {} ps-consts {:016X}",
                          vertex_shader->ucode_data_hash(), ps_hash, ntex, tex_desc, uw_diag_vertex_count_,
                          pixel_shader ? psb[0] : 0ull);
            }
          }
          // A solid fill (no texture sampled) is a scene-transition fade or a
''')
if "#include <unordered_set>" not in q:
    q = q.replace("#include <chrono>", "#include <chrono>\n#include <unordered_set>", 1)
open(H, "w", encoding="utf-8", newline="").write(h)
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: c8 draw first-sight census")
