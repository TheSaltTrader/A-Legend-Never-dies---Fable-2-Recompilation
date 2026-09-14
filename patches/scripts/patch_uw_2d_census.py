"""s76 diagnostics - fable2_2d_census (int, 0): log the first N distinct vertex
shaders seen in draws, with what the ultrawide HUD fix needs to know: depth
test on/off, pixel-shader texture count, whether the vertex constants hold a
screen-space ortho block (generic scan: w column [0,0,0,1], small +x and -y
scales, origin (-1,1)) and at which register, whether they hold a perspective
block, and the first used constants. Fable II's 3D is widened by the app's
camera hook; only its 2D HUD draws need compressing, and this says how its
UI shaders carry their transform. Logged as "[2d-census] ...". APPLY ONCE."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
q = open(Q, encoding="utf-8").read()
assert "fable2_2d_census" not in q, "already applied"

def rep(old, new):
    global q
    assert q.count(old) == 1, ("expected 1, found %d: %s" % (q.count(old), old[:90]))
    q = q.replace(old, new)

rep('''REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, "GPU/D3D12",''',
    '''REXCVAR_DEFINE_INT32(fable2_2d_census, 0, "GPU/D3D12",
                     "Diagnostic: log the first N distinct vertex shaders with their 2D/3D classification")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(readback_resolve_sync_budget, 8, "GPU/D3D12",''')

# after the FOV block's opening: the constants are packed at float_constants_begin per the
# vertex float bitmap. Anchor on the ng2 seed comment, insert the census right before it.
rep('''    {
      // One-time env seed (NG2_FOV_K) so the widen can be driven headless for
      // testing; the live cvar ng2_fov_k, set from the in-game FOV slider, is
''', '''    // [2d-census] Fable II: how do the UI shaders carry their transform?
    if (REXCVAR_GET(fable2_2d_census) > 0) {
      static std::unordered_map<const void*, int> s_seen;
      static int s_logged = 0;
      const void* skey = static_cast<const void*>(vertex_shader);
      if (s_logged < REXCVAR_GET(fable2_2d_census) && !s_seen.count(skey)) {
        s_seen.emplace(skey, 1);
        ++s_logged;
        const float* fb = reinterpret_cast<const float*>(float_constants_begin);
        const auto& bmc = float_constant_map_vertex.float_bitmap;
        const float* rp[512] = {nullptr};
        uint32_t used = 0, reg_lo = 512, reg_hi = 0;
        for (uint32_t i = 0; i < 4; ++i) {
          uint64_t bits = bmc[i];
          uint32_t bit;
          while (rex::bit_scan_forward(bits, &bit)) {
            bits &= ~(1ull << bit);
            const uint32_t reg = (i << 6) + bit;
            if (reg < 512) {
              rp[reg] = fb + used * 4;
              if (reg < reg_lo) reg_lo = reg;
              if (reg > reg_hi) reg_hi = reg;
            }
            ++used;
          }
        }
        int ortho_at = -1, persp_at = -1;
        float ox = 0, oy = 0, oox = 0, ooy = 0;
        for (uint32_t r = reg_lo; r + 3 <= reg_hi && r < 512; ++r) {
          const float *r0 = rp[r], *r1 = rp[r + 1], *r2 = rp[r + 2], *r3 = rp[r + 3];
          if (!r0 || !r1 || !r3) continue;
          const float w2 = r2 ? r2[3] : 0.0f;
          const bool ortho_w = std::fabs(r0[3]) < 1e-3f && std::fabs(r1[3]) < 1e-3f &&
                               std::fabs(w2) < 1e-3f && std::fabs(r3[3] - 1.0f) < 1e-3f;
          if (ortho_at < 0 && ortho_w && r0[0] > 1e-5f && r0[0] < 0.5f && std::fabs(r1[0]) < 1e-4f &&
              std::fabs(r1[1]) > 1e-5f && std::fabs(r1[1]) < 0.5f) {
            ortho_at = int(r); ox = r0[0]; oy = r1[1]; oox = r3[0]; ooy = r3[1];
          }
          bool close = true;
          const float* prows[3] = {r0, r1, r2};
          for (int j = 0; j < 3; ++j) {
            if (!prows[j]) continue;
            if (std::fabs(prows[j][2] - prows[j][3]) > 0.01f * (1.0f + std::fabs(prows[j][2]))) close = false;
          }
          const bool nontrivial = std::fabs(r0[3]) > 1e-3f || std::fabs(r1[3]) > 1e-3f || std::fabs(w2) > 1e-3f;
          if (persp_at < 0 && close && nontrivial && !ortho_w) persp_at = int(r);
        }
        const reg::RB_DEPTHCONTROL depth = draw_util::GetNormalizedDepthControl(*register_file_);
        const uint32_t ps_textures = pixel_shader ? uint32_t(pixel_shader->GetTextureBindingsAfterTranslation().size()) : 0u;
        char first[160] = {0};
        int fn = 0;
        for (uint32_t r = reg_lo; r < 512 && r <= reg_hi && fn < 150; ++r) {
          if (!rp[r]) continue;
          fn += std::snprintf(first + fn, sizeof(first) - size_t(fn), " c%u=%.4g,%.4g,%.4g,%.4g", r,
                              rp[r][0], rp[r][1], rp[r][2], rp[r][3]);
          if (r >= reg_lo + 5) break;
        }
        REXLOG_INFO("[2d-census] vs {:016X} ps {:016X} depth {} ps_textures {} consts {} ({}..{}) "
                    "ortho_at {} (x {:.5f} y {:.5f} origin {:.3f},{:.3f}) persp_at {} first:{}",
                    vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,
                    pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, depth.z_enable ? 1 : 0,
                    ps_textures, used, reg_lo, reg_hi, ortho_at, ox, oy, oox, ooy, persp_at, first);
      }
    }
    {
      // One-time env seed (NG2_FOV_K) so the widen can be driven headless for
      // testing; the live cvar ng2_fov_k, set from the in-game FOV slider, is
''')
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: fable2_2d_census")
