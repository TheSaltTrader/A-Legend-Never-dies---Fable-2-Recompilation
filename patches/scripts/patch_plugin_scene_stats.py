"""Per-frame draw statistics for the app: gpu_frame_draws / gpu_frame_depth_draws.

Published from IssueSwap (before the presenter is handed the frame) so the
HUD overlay can tell a frame that drew the 3D world (hundreds of depth-tested
draws) from a frame of 2D menus only (none): the pause menu and the shop go
to 16:9 with bars, a chest popup or a dialogue with the world behind it stays
edge to edge. The counters cover swap to swap.
"""
import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) == 0 and t.count(new) == 1:
            print("  already applied:", old[:50].strip()); continue
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

edit("include/rex/graphics/d3d12/command_processor.h", [
    ("  uint32_t resolve_sync_misses_this_frame_ = 0;\n",
     "  uint32_t resolve_sync_misses_this_frame_ = 0;\n"
     "  // [scene] Draws since the last swap, all and depth-tested, published as\n"
     "  // gpu_frame_draws / gpu_frame_depth_draws for the app's HUD overlay.\n"
     "  uint32_t frame_draws_ = 0;\n"
     "  uint32_t frame_depth_draws_ = 0;\n"),
])

edit("src/graphics/d3d12/command_processor.cpp", [
    ("REXCVAR_DEFINE_INT32(guest_fps_x10, 0, \"GPU\",\n",
     "// [scene] Draw counts of the last presented guest frame, for the app's HUD\n"
     "// overlay: a frame that drew the 3D world has hundreds of depth-tested\n"
     "// draws, a frame of 2D menus only has none.\n"
     "REXCVAR_DEFINE_INT32(gpu_frame_draws, 0, \"GPU\", \"Draws in the last presented guest frame (stat)\");\n"
     "REXCVAR_DEFINE_INT32(gpu_frame_depth_draws, 0, \"GPU\",\n"
     "                     \"Depth-tested draws in the last presented guest frame (stat)\");\n"
     "REXCVAR_DEFINE_INT32(guest_fps_x10, 0, \"GPU\",\n"),
    ("    deferred_command_list_.TagNextDraw(\n"
     "        {vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,\n"
     "         pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, index_count,\n"
     "         uint32_t(primitive_type)});\n"
     "    deferred_command_list_.D3DDrawInstanced(",
     "    deferred_command_list_.TagNextDraw(\n"
     "        {vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,\n"
     "         pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, index_count,\n"
     "         uint32_t(primitive_type)});\n"
     "    ++frame_draws_;  // [scene]\n"
     "    if (normalized_depth_control.z_enable) ++frame_depth_draws_;\n"
     "    deferred_command_list_.D3DDrawInstanced("),
    ("    deferred_command_list_.TagNextDraw(\n"
     "        {vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,\n"
     "         pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, index_count,\n"
     "         uint32_t(primitive_type)});\n"
     "    deferred_command_list_.D3DDrawIndexedInstanced(",
     "    deferred_command_list_.TagNextDraw(\n"
     "        {vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,\n"
     "         pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, index_count,\n"
     "         uint32_t(primitive_type)});\n"
     "    ++frame_draws_;  // [scene]\n"
     "    if (normalized_depth_control.z_enable) ++frame_depth_draws_;\n"
     "    deferred_command_list_.D3DDrawIndexedInstanced("),
    ("  ReportGuestSwapRate();\n",
     "  ReportGuestSwapRate();\n"
     "  // [scene] The frame's draw counts go out before the presenter gets it.\n"
     "  REXCVAR_SET(gpu_frame_draws, int32_t(frame_draws_));\n"
     "  REXCVAR_SET(gpu_frame_depth_draws, int32_t(frame_depth_draws_));\n"
     "  frame_draws_ = 0;\n"
     "  frame_depth_draws_ = 0;\n"),
])
print("done")
