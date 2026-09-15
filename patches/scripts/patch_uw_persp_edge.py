# s91: the pause-menu transition layer keeps its full width at ultrawide.
#
# Draw dumps (gpu_draw_dump_frames, 2026-09-15) of a pause-menu open and close in
# the forest: Fable II's UI is one shader pair (vs 7B22146D51E0E4F5 / ps
# 0542FFDF2B118509) drawing 6-vertex quads through a hard-coded 16:9 perspective
# (c0 = (6.303,0,0,0), c1 = (0,0,11.2,0)). The gameplay HUD is 17 such quads with
# the depth test off, |x| <= 6.4. The steady pause menu is 256 of them with the
# depth test ON (never touched here). The TRANSITION - the last 4 gameplay frames
# after the Start press, and the first 3 world frames after a close - draws 5 of
# them depth-OFF over the world: one x -7.6..7.6 (the whole 16:9 frame; a 1024x512
# GPU-written capture of the menu, alpha-blended = the dissolve) and four leather
# side pieces at +-1.6..+-7.7. The perspective-widget rule compressed those five
# into the centred 16:9 band while the world stayed edge to edge: the "character
# compressed to 16:9 when pressing Start" and the "semi-transparent layer not
# reaching the edges" on close. A quad that reaches the frame edge (|x| >= 7.0)
# is that layer, not a HUD widget: it keeps its width, like the steady menu.
import io

CP = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = io.open(CP, "r", encoding="utf-8", newline="").read()
old = '''            const float ratio = lanes == 1 ? sy / sx : 0.0f;
            if (ratio > 1.751f && ratio < 1.805f &&
                !draw_util::GetNormalizedDepthControl(*register_file_).z_enable) {
              // c0 is the first used register: packed position 0.
              float* out = reinterpret_cast<float*>(const_cast<uint8_t*>(float_constants_begin));
              out[0] = sx * static_cast<float>(kd);
            }
'''
new = '''            const float ratio = lanes == 1 ? sy / sx : 0.0f;
            if (ratio > 1.751f && ratio < 1.805f &&
                !draw_util::GetNormalizedDepthControl(*register_file_).z_enable) {
              // [uw-menu] The pause-menu transition draws its dissolve layer and
              // the leather frame pieces with this same projection, depth off,
              // over the world for a few frames: quads that reach the 16:9 frame
              // edge (|x| >= 7.0 of 7.6). Compressing them put a 16:9 band of
              // menu over an edge-to-edge world; the steady menu (depth on) is
              // full width, so these stay full width too. HUD widgets are small.
              float ex0 = 0.0f, ex1 = 0.0f;
              const float espan = C8QuadSpan(vertex_shader, ex0, ex1);
              const bool frame_edge =
                  espan >= 0.0f && std::max(std::fabs(ex0), std::fabs(ex1)) >= 7.0f;
              if (!frame_edge) {
                // c0 is the first used register: packed position 0.
                float* out = reinterpret_cast<float*>(const_cast<uint8_t*>(float_constants_begin));
                out[0] = sx * static_cast<float>(kd);
              } else {
                static uint32_t edge_logged = 0;
                if (edge_logged < 12) {
                  ++edge_logged;
                  REXLOG_INFO("[uw-menu] transition layer quad kept full width: x {:.1f}..{:.1f} verts {}",
                              ex0, ex1, uw_diag_vertex_count_);
                }
              }
            }
'''
assert s.count(old) == 1
s = s.replace(old, new)
io.open(CP, "w", encoding="utf-8", newline="").write(s)
print("patched OK")
