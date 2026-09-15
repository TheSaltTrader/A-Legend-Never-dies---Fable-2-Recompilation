# s90: (1) per-draw dump diagnostic (gpu_draw_dump_frames / gpu_draw_dump_file, app pad
# command dump:N) to identify the pause-menu dissolve draws; (2) the texture-flash fix
# without the drain: wait for a PRIOR submission's pending readback only when a texture
# upload actually reads that range (readback_await_before_texture_upload), and the
# resolve-time drain (readback_resolve_drain_large_kb) defaults to 0.
import io, sys

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox"
CP = ROOT + r"\rexglue-src\src\graphics\d3d12\command_processor.cpp"
HDR = ROOT + r"\rexglue-src\include\rex\graphics\d3d12\command_processor.h"
APP = ROOT + r"\fable2recomp\src\fable2_autoskip.cpp"


def rd(p):
    with io.open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with io.open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def rep(s, old, new, count=1):
    assert s.count(old) == count, (old[:80], s.count(old))
    return s.replace(old, new)


# ---------------------------------------------------------------- plugin .cpp
s = rd(CP)

# (a) cvars
s = rep(s, 'REXCVAR_DEFINE_INT32(readback_resolve_drain_large_kb, 128, "GPU/D3D12",',
        'REXCVAR_DEFINE_INT32(readback_resolve_drain_large_kb, 0, "GPU/D3D12",')
s = rep(s, '''REXCVAR_DEFINE_DOUBLE(fable2_uw_2d_k, 0.0, "GPU/D3D12",
                      "Fable II ultrawide: scale the 2D HUD's pixel-to-clip constant (c8) x by this factor "
                      "(16:9 / display aspect) so the HUD keeps its proportions; 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
''', '''REXCVAR_DEFINE_DOUBLE(fable2_uw_2d_k, 0.0, "GPU/D3D12",
                      "Fable II ultrawide: scale the 2D HUD's pixel-to-clip constant (c8) x by this factor "
                      "(16:9 / display aspect) so the HUD keeps its proportions; 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(gpu_draw_dump_frames, 0, "GPU/D3D12",
                     "Diagnostic: write one line per draw for this many guest frames to "
                     "gpu_draw_dump_file, then reset to 0 (the app's pad command dump:N sets it)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(gpu_draw_dump_file, "", "GPU/D3D12",
                      "Diagnostic: the file gpu_draw_dump_frames writes its per-draw lines to")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(readback_await_before_texture_upload, true, "GPU/D3D12",
                    "A texture upload that reads guest memory still waiting for a resolve readback "
                    "from an EARLIER submission waits for that submission (only) and lands the copy "
                    "first, so it never uploads stale bytes (the white/magenta streaming flash). "
                    "Replaces the resolve-time drain (readback_resolve_drain_large_kb), which "
                    "waited for the whole GPU queue at every large resolve: 28-38 guest fps in town.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
''')

# (b) counters
s = rep(s, 'static std::atomic<uint32_t> g_impostor_readbacks_landed{0};\n',
        'static std::atomic<uint32_t> g_impostor_readbacks_landed{0};\n'
        '// [readback] Texture uploads that waited for a prior submission\'s readback, and\n'
        '// uploads that found a readback still in the OPEN submission (not awaitable here).\n'
        'static std::atomic<uint32_t> g_readbacks_awaited_before_upload{0};\n'
        'static std::atomic<uint64_t> g_readbacks_awaited_us{0};\n'
        'static std::atomic<uint32_t> g_readbacks_open_at_upload{0};\n')

# (c) the on-demand wait
s = rep(s, '''void D3D12CommandProcessor::LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length) {
  if (!length || !REXCVAR_GET(readback_land_before_texture_upload)) return;
  if (HasPendingResolveReadback(address, length)) LandCompletedResolveReadback(address, length);
}
''', '''void D3D12CommandProcessor::LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length) {
  if (!length || !REXCVAR_GET(readback_land_before_texture_upload)) return;
  if (!HasPendingResolveReadback(address, length)) return;
  LandCompletedResolveReadback(address, length);
  if (!REXCVAR_GET(readback_await_before_texture_upload)) return;
  // [readback] A copy still in flight from an EARLIER submission: wait for that
  // submission only (not the whole queue, as the resolve-time drain did), land
  // it, and the upload below reads fresh bytes. A copy issued in the still-open
  // submission cannot be awaited from inside a draw (its render targets are
  // bound and ending the submission here would drop them); it is counted.
  uint64_t await = 0;
  uint32_t open = 0;
  const uint64_t end = uint64_t(address) + length;
  for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
    if (p.address >= end || uint64_t(p.address) + p.length <= address) continue;
    if (p.submission >= submission_current_) {
      ++open;
      continue;
    }
    await = std::max(await, p.submission);
  }
  if (await) {
    FenceReasonScope fence_reason(fence_reason_, "readback before upload");
    const auto t0 = std::chrono::steady_clock::now();
    CheckSubmissionFence(await);
    g_readbacks_awaited_us.fetch_add(
        uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count()),
        std::memory_order_relaxed);
    g_readbacks_awaited_before_upload.fetch_add(1, std::memory_order_relaxed);
    LandCompletedResolveReadback(address, length);
  }
  if (open) g_readbacks_open_at_upload.fetch_add(open, std::memory_order_relaxed);
}
''')

# (d) census line
s = rep(s, '''    const uint32_t landed = g_impostor_readbacks_landed.exchange(0);
    if (landed) {
      char b[80];
      std::snprintf(b, sizeof(b), "%slanded %u impostor readbacks before their texture upload",
                    line.empty() ? "" : ", ", landed);
      line += b;
    }
''', '''    const uint32_t landed = g_impostor_readbacks_landed.exchange(0);
    if (landed) {
      char b[80];
      std::snprintf(b, sizeof(b), "%slanded %u impostor readbacks before their texture upload",
                    line.empty() ? "" : ", ", landed);
      line += b;
    }
    {
      const uint32_t awaited = g_readbacks_awaited_before_upload.exchange(0);
      const uint64_t us = g_readbacks_awaited_us.exchange(0);
      const uint32_t open = g_readbacks_open_at_upload.exchange(0);
      if (awaited || open) {
        char b[120];
        std::snprintf(b, sizeof(b), "%sawaited %u readbacks before upload (%.1f ms), %u still open",
                      line.empty() ? "" : ", ", awaited, us / 1000.0, open);
        line += b;
      }
    }
''')

# (e) frame boundary in IssueSwap
s = rep(s, '''  frame_draws_ = 0;
  frame_depth_draws_ = 0;

  system::X_VIDEO_MODE video_mode;
''', '''  frame_draws_ = 0;
  frame_depth_draws_ = 0;
  // [dd] Per-draw dump: close out a frame, start a dump the app asked for.
  {
    if (dd_file_) {
      std::fprintf(dd_file_, "# swap after frame %u (%u draws)\\n", dd_frame_, dd_draw_);
      ++dd_frame_;
      if (dd_frames_left_) --dd_frames_left_;
      if (!dd_frames_left_) {
        std::fclose(dd_file_);
        dd_file_ = nullptr;
        REXLOG_INFO("[dd] draw dump done after {} frames", dd_frame_);
      }
    }
    const int32_t want = REXCVAR_GET(gpu_draw_dump_frames);
    if (want > 0 && !dd_file_) {
      const std::string path = rex::cvar::Query<std::string>("gpu_draw_dump_file");
      dd_file_ = path.empty() ? nullptr : std::fopen(path.c_str(), "w");
      dd_frames_left_ = dd_file_ ? uint32_t(want) : 0u;
      dd_frame_ = 0;
      REXCVAR_SET(gpu_draw_dump_frames, 0);
      REXLOG_INFO("[dd] draw dump: {} frames to '{}'{}", want, path, dd_file_ ? "" : " (open failed)");
    }
    dd_draw_ = 0;
  }

  system::X_VIDEO_MODE video_mode;
''')

# (f) the per-draw call in IssueDraw
s = rep(s, '''  uw_diag_vertex_count_ = primitive_processing_result.host_draw_vertex_count;
  uw_ppr_ = &primitive_processing_result;
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
''', '''  uw_diag_vertex_count_ = primitive_processing_result.host_draw_vertex_count;
  uw_ppr_ = &primitive_processing_result;
  if (dd_file_) {
    DrawDumpLine(vertex_shader, pixel_shader, primitive_processing_result, viewport_info, scissor,
                 normalized_depth_control);
  }
  if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
''')

# (g) DrawDumpLine after C8QuadSpan
s = rep(s, '''    if (first) { x_min = x_max = x; first = false; }
    else { x_min = std::min(x_min, x); x_max = std::max(x_max, x); }
  }
  return x_max - x_min;
}
''', '''    if (first) { x_min = x_max = x; first = false; }
    else { x_min = std::min(x_min, x); x_max = std::max(x_max, x); }
  }
  return x_max - x_min;
}

// [dd] One line per draw while a dump is open: shaders, primitive, vertex
// count, depth/blend/mask state, render target, viewport, scissor, the c0..c3
// projection block and c8 when the vertex shader uses them, the small-draw
// vertex x range, and the pixel shader's textures (size, format, address,
// GPU-written, readback pending). Written with buffered stdio; the game runs
// on while the file grows.
void D3D12CommandProcessor::DrawDumpLine(const D3D12Shader* vertex_shader,
                                         const D3D12Shader* pixel_shader,
                                         const PrimitiveProcessor::ProcessingResult& ppr,
                                         const draw_util::ViewportInfo& viewport,
                                         const draw_util::Scissor& scissor,
                                         reg::RB_DEPTHCONTROL depth_control) {
  if (!dd_file_ || !vertex_shader) return;
  const RegisterFile& regs = *register_file_;
  char line[1400];
  int n = std::snprintf(
      line, sizeof(line),
      "f%u d%u vs %016llX ps %016llX prim %u verts %u idx %u z %u/%u blend %08X cmask %X "
      "rt %08X vp %u,%u %ux%u sc %u,%u %ux%u",
      dd_frame_, dd_draw_++, (unsigned long long)vertex_shader->ucode_data_hash(),
      (unsigned long long)(pixel_shader ? pixel_shader->ucode_data_hash() : 0ull),
      uint32_t(ppr.guest_primitive_type), ppr.host_draw_vertex_count,
      uint32_t(ppr.index_buffer_type), uint32_t(depth_control.z_enable),
      uint32_t(depth_control.z_write_enable), regs[XE_GPU_REG_RB_BLENDCONTROL0],
      regs[XE_GPU_REG_RB_COLOR_MASK] & 0xFu, regs[XE_GPU_REG_RB_COLOR_INFO], viewport.xy_offset[0],
      viewport.xy_offset[1], viewport.xy_extent[0], viewport.xy_extent[1], scissor.offset[0],
      scissor.offset[1], scissor.extent[0], scissor.extent[1]);
  auto put = [&](const char* fmt, auto... args) {
    if (n < 0 || size_t(n) >= sizeof(line) - 2) return;
    const int m = std::snprintf(line + n, sizeof(line) - 2 - size_t(n), fmt, args...);
    if (m > 0) n += m;
  };
  const auto& bmu = vertex_shader->constant_register_map().float_bitmap;
  const float* c = reinterpret_cast<const float*>(&regs[XE_GPU_REG_SHADER_CONSTANT_000_X]);
  if ((bmu[0] & 0xFull) == 0xFull) {
    put(" c0 %.4g,%.4g,%.4g,%.4g c1 %.4g,%.4g,%.4g,%.4g c2 %.4g,%.4g,%.4g,%.4g c3 %.4g,%.4g,%.4g,%.4g",
        c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13],
        c[14], c[15]);
  }
  if ((bmu[0] >> 8) & 1ull) put(" c8 %.5g,%.5g,%.4g,%.4g", c[32], c[33], c[34], c[35]);
  put(" vconst %016llX", (unsigned long long)bmu[0]);
  float x0 = 0.0f, x1 = 0.0f;
  const float span = C8QuadSpan(vertex_shader, x0, x1);
  if (span >= 0.0f) put(" x %.1f..%.1f", x0, x1);
  if (pixel_shader) {
    const auto& tb = pixel_shader->GetTextureBindingsAfterTranslation();
    for (size_t i = 0; i < tb.size() && i < 4; ++i) {
      xenos::xe_gpu_texture_fetch_t fetch;
      std::memcpy(&fetch,
                  &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + tb[i].fetch_constant * 6],
                  sizeof(fetch));
      const uint32_t base = uint32_t(fetch.base_address) << 12;
      put(" t%u:%ux%u f%u @%08X%s%s", tb[i].fetch_constant, uint32_t(fetch.size_2d.width) + 1,
          uint32_t(fetch.size_2d.height) + 1, uint32_t(fetch.format), base,
          (shared_memory_ && shared_memory_->AnyPageGpuWritten(base, 4096)) ? " gpuw" : "",
          HasPendingResolveReadback(base, 4096) ? " rbpend" : "");
    }
  }
  if (n < 0) n = 0;
  if (size_t(n) > sizeof(line) - 2) n = int(sizeof(line) - 2);
  line[n++] = '\\n';
  line[n] = 0;
  std::fputs(line, dd_file_);
}
''')
wr(CP, s)

# ---------------------------------------------------------------- plugin header
h = rd(HDR)
h = rep(h, '''  // [uw-2d] Pixel x span of a c8 quad read from guest memory; < 0 = unknown.
  float C8QuadSpan(const D3D12Shader* vertex_shader, float& x_min, float& x_max) const;
''', '''  // [uw-2d] Pixel x span of a c8 quad read from guest memory; < 0 = unknown.
  float C8QuadSpan(const D3D12Shader* vertex_shader, float& x_min, float& x_max) const;
  // [dd] Per-draw dump diagnostic (gpu_draw_dump_frames): one line per draw.
  std::FILE* dd_file_ = nullptr;
  uint32_t dd_frames_left_ = 0, dd_frame_ = 0, dd_draw_ = 0;
  void DrawDumpLine(const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
                    const PrimitiveProcessor::ProcessingResult& ppr,
                    const draw_util::ViewportInfo& viewport, const draw_util::Scissor& scissor,
                    reg::RB_DEPTHCONTROL depth_control);
''')
if '#include <cstdio>' not in h:
    h = rep(h, '#include <algorithm>\n#include <array>\n', '#include <algorithm>\n#include <array>\n#include <cstdio>\n')
wr(HDR, h)

# ---------------------------------------------------------------- app: dump:N pad command
a = rd(APP)
a = rep(a, '''  bool wait_only = false;                  // "wait:N": nothing pressed
  bool release = false;                    // "release": clear and stop
  std::string text;                        // for the log
};
''', '''  bool wait_only = false;                  // "wait:N": nothing pressed
  bool release = false;                    // "release": clear and stop
  int dump_frames = 0;                     // "dump:N": the plugin's per-draw dump
  std::string text;                        // for the log
};

// "dump:N" - ask the GPU plugin for one line per draw over the next N guest
// frames (gpu_draw_dump_frames), into draw_dump_<serial>.txt beside the exe.
// A diagnostic for finding which draws make up a transition (the ultrawide
// pause-menu dissolve): put it on the line before the press.
void StartDrawDump(int frames) {
  static int serial = 0;
  ++serial;
  const auto path = rex::filesystem::GetExecutableFolder() /
                    ("draw_dump_" + std::to_string(serial) + ".txt");
  rex::cvar::SetFlagByName("gpu_draw_dump_file", path.string());
  rex::cvar::SetFlagByName("gpu_draw_dump_frames", std::to_string(frames));
  REXLOG_INFO("[padfile] draw dump {} frames -> {}", frames, path.string());
}
''')
a = rep(a, '''  if (head == "release") { out.release = true; out.seconds = 0; return true; }
''', '''  if (head == "release") { out.release = true; out.seconds = 0; return true; }
  if (head == "dump") {
    out.wait_only = true;
    out.seconds = 0;
    out.dump_frames = int(secs_at(1, 40));
    return true;
  }
''')
a = rep(a, '''        current_until_ = now + current_.seconds;
        gap_until_ = current_until_ + 0.1;
        if (!current_.wait_only && !current_.release)
''', '''        current_until_ = now + current_.seconds;
        gap_until_ = current_until_ + 0.1;
        if (current_.dump_frames > 0) StartDrawDump(current_.dump_frames);
        if (!current_.wait_only && !current_.release)
''')
if '#include <rex/cvar.h>' not in a:
    a = rep(a, '#include <rex/filesystem.h>\n', '#include <rex/cvar.h>\n#include <rex/filesystem.h>\n')
wr(APP, a)
print("patched OK")
