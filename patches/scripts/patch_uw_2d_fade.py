"""s80 - the scene-transition fades stayed inside the 16:9 band at ultrawide:
Fable II draws a fade as a full-screen 2D quad with the same c8 pixel-to-clip
constant as the HUD and a pixel shader that samples NO texture (a solid
fill), so the s77 compression squeezed it into the band and the sides of the
world stayed lit through the fade-out and popped in first on arrival
(recorded 2026-09-14 18:50, rec_fade2/rec_fade3). Same answer as Ninja
Gaiden II v1.0.20: a c8 draw whose pixel shader binds no texture keeps its
full width; textured HUD draws are still compressed. A rate-limited line
"[uw-2d] solid-fill 2D draws left full width" reports the count so a
transition in the log confirms the exemption fired. APPLY ONCE (requires
patch_uw_2d_hud.py)."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
q = open(Q, encoding="utf-8").read()
assert "fable2_uw_2d_k" in q, "needs patch_uw_2d_hud.py first"
assert "solid-fill 2D draws left full width" not in q, "already applied"

def rep(old, new):
    global q
    assert q.count(old) == 1, ("expected 1, found %d: %s" % (q.count(old), old[:90]))
    q = q.replace(old, new)

rep('''          if (pixel_scale) {
            // c8's packed position = the number of used registers below 8.
            const uint32_t pos = rex::bit_count(bmu[0] & 0xFFull);
''', '''          // A solid fill (no texture sampled) is a scene-transition fade or a
          // full-screen tint: it must cover the whole ultrawide picture, so it
          // keeps its width (the Ninja Gaiden II v1.0.20 rule).
          const bool solid_fill =
              pixel_shader != nullptr && pixel_shader->GetTextureBindingsAfterTranslation().empty();
          if (pixel_scale && solid_fill) {
            static uint32_t solid_count = 0;
            static std::chrono::steady_clock::time_point solid_logged{};
            ++solid_count;
            const auto now_sf = std::chrono::steady_clock::now();
            if (now_sf - solid_logged > std::chrono::seconds(1)) {
              solid_logged = now_sf;
              REXLOG_INFO("[uw-2d] solid-fill 2D draws left full width: {} since the last line (ps {:016X})",
                          solid_count, pixel_shader->ucode_data_hash());
              solid_count = 0;
            }
          }
          if (pixel_scale && !solid_fill) {
            // c8's packed position = the number of used registers below 8.
            const uint32_t pos = rex::bit_count(bmu[0] & 0xFFull);
''')
if "#include <chrono>" not in q:
    q = q.replace("#include <cmath>", "#include <chrono>\n#include <cmath>", 1)
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: solid-fill 2D draws keep their full width at ultrawide")
