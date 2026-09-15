# s95: the flash barrier as a submission split placed only where it can matter.
#
# s93 ended the GPU submission after EVERY resolve: it kept the streaming flash
# away (the 0.2.10 drain implied the same boundary) but at the lake that is 4,200
# submissions a second = 32-41% of the GPU command thread (s94 counters), and the
# game thread spins waiting for that thread. The hazard every hypothesis shares is
# "a texture is loaded from memory the GPU resolved in the still-open submission".
# So: remember the ranges resolved since the last submission end; when a texture
# whose guest range overlaps one is about to be loaded (LoadTextureDataFromResident
# MemoryImpl, inside the draw's RequestTextures), end the submission there and begin
# a new one - a full GPU sync between the resolve and the load - then the draw
# re-binds its render targets (the only per-draw state set before RequestTextures).
# Hundreds of splits a second instead of thousands. readback_resolve_split_before_load
# (default on); readback_resolve_submit_small_kb defaults to 0 again.
import io

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
CP = ROOT + r"\src\graphics\d3d12\command_processor.cpp"
HDR = ROOT + r"\include\rex\graphics\d3d12\command_processor.h"
TC = ROOT + r"\src\graphics\d3d12\texture_cache.cpp"


def rd(p):
    return io.open(p, "r", encoding="utf-8", newline="").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def rep(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)


h = rd(HDR)
h = rep(h, "  // [dd] Per-draw dump diagnostic (gpu_draw_dump_frames): one line per draw.\n",
        "  // [split] Guest ranges resolved since the last submission end; a texture load\n"
        "  // overlapping one splits the submission first (readback_resolve_split_before_load).\n"
        "  std::vector<std::pair<uint32_t, uint32_t>> fresh_resolve_ranges_;\n"
        "  bool rt_rebind_after_split_ = false;\n"
        "  void NoteFreshResolve(uint32_t address, uint32_t length);\n"
        "  // [dd] Per-draw dump diagnostic (gpu_draw_dump_frames): one line per draw.\n")
h = rep(h, "  void LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length);\n",
        "  void LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length);\n"
        "  // [split] True if the range overlaps a resolve of the still-open submission.\n"
        "  bool RangeResolvedInOpenSubmission(uint32_t address, uint32_t length) const;\n"
        "  // [split] End the open submission and begin a new one (a full GPU sync) before a\n"
        "  // texture load reads freshly resolved memory; the draw re-binds its render targets.\n"
        "  bool SplitSubmissionForFreshResolve();\n")
wr(HDR, h)

s = rd(CP)
s = rep(s, '''REXCVAR_DEFINE_INT32(readback_resolve_submit_small_kb, 1048576, "GPU/D3D12",''',
        '''REXCVAR_DEFINE_BOOL(readback_resolve_split_before_load, true, "GPU/D3D12",
                    "End the GPU submission (a full sync, no wait) right before a texture is loaded "
                    "from memory the GPU resolved in the still-open submission - the streaming "
                    "flash's hazard - instead of after every resolve (readback_resolve_submit_small_kb): "
                    "hundreds of splits a second instead of thousands (0.2.11)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(readback_resolve_submit_small_kb, 0, "GPU/D3D12",''')
s = rep(s, "static std::atomic<uint32_t> g_landing_count{0};\n",
        "static std::atomic<uint32_t> g_landing_count{0};\n"
        "static std::atomic<uint32_t> g_splits_before_load{0};\n")

# record fresh resolves: the stock path and the readback path
s = rep(s, '''  if (readback_mode == ReadbackResolveMode::kDisabled) {
    uint32_t written_address, written_length;
    return render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,
                                         written_address, written_length);
  }
  return IssueCopy_ReadbackResolvePath();''', '''  if (readback_mode == ReadbackResolveMode::kDisabled) {
    uint32_t written_address, written_length;
    if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,
                                       written_address, written_length)) {
      return false;
    }
    NoteFreshResolve(written_address, written_length);
    return true;
  }
  return IssueCopy_ReadbackResolvePath();''')
s = rep(s, '''  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_, written_address,
                                     written_length)) {
    return false;
  }
  if (REXCVAR_GET(readback_resolve_uav_barrier)) {''', '''  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_, written_address,
                                     written_length)) {
    return false;
  }
  NoteFreshResolve(written_address, written_length);
  if (REXCVAR_GET(readback_resolve_uav_barrier)) {''')

# clear at every submission end
s = rep(s, '''bool D3D12CommandProcessor::EndSubmission(bool is_swap) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
''', '''bool D3D12CommandProcessor::EndSubmission(bool is_swap) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  fresh_resolve_ranges_.clear();  // [split] a boundary makes every resolve "old"
''')

# the helpers, after LandImpostorReadbackBeforeUpload
s = rep(s, '''bool D3D12CommandProcessor::ShouldDeferTextureUpload(uint32_t address, uint32_t length) {''',
        '''void D3D12CommandProcessor::NoteFreshResolve(uint32_t address, uint32_t length) {
  if (!length || !REXCVAR_GET(readback_resolve_split_before_load)) return;
  if (fresh_resolve_ranges_.size() < 4096) {
    fresh_resolve_ranges_.emplace_back(address, address + length);
  }
}

bool D3D12CommandProcessor::RangeResolvedInOpenSubmission(uint32_t address,
                                                          uint32_t length) const {
  if (!length || !submission_open_ || fresh_resolve_ranges_.empty()) return false;
  const uint64_t end = uint64_t(address) + length;
  for (const auto& r : fresh_resolve_ranges_) {
    if (r.first < end && uint64_t(r.second) > address) return true;
  }
  return false;
}

bool D3D12CommandProcessor::SplitSubmissionForFreshResolve() {
  if (!submission_open_) return false;
  if (!EndSubmission(false)) return false;
  if (!BeginSubmission(true)) return false;
  rt_rebind_after_split_ = true;
  g_splits_before_load.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool D3D12CommandProcessor::ShouldDeferTextureUpload(uint32_t address, uint32_t length) {''')

# the draw: re-bind the render targets after a split inside RequestTextures
s = rep(s, '''  texture_cache_->RequestTextures(used_texture_mask);

  // Bind the pipeline after configuring it and doing everything that may bind''',
        '''  rt_rebind_after_split_ = false;
  texture_cache_->RequestTextures(used_texture_mask);
  if (rt_rebind_after_split_) {
    // [split] A texture load ended the submission: the render targets bound
    // above belong to the closed one - bind them again in the new one.
    rt_rebind_after_split_ = false;
    if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                      normalized_color_mask, *vertex_shader)) {
      DrawFailReason("render target re-bind after split");
      return false;
    }
  }

  // Bind the pipeline after configuring it and doing everything that may bind''')

# census
s = rep(s, '''        if (ln || sn) {
          char b[160];
          std::snprintf(b, sizeof(b), "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0);
          line += b;
        }''', '''        const uint32_t splits = g_splits_before_load.exchange(0);
        if (ln || sn) {
          char b[200];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load)",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits);
          line += b;
        }''')
wr(CP, s)

t = rd(TC)
t = rep(t, '''bool D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture, bool load_base,
                                                              bool load_mips) {
  command_processor_.NoteTextureLoad(  // [hitch]
      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +
      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));
''', '''bool D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl(Texture& texture, bool load_base,
                                                              bool load_mips) {
  command_processor_.NoteTextureLoad(  // [hitch]
      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +
      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));
  // [split] Loading from memory the GPU resolved in the still-open submission:
  // end that submission first so the resolve is complete and visible before the
  // load reads it (the streaming flash), instead of a boundary after every
  // resolve. Nothing of this load has been recorded yet, so the new submission
  // gets all of it; the draw re-binds its render targets afterwards.
  if ((load_base && command_processor_.RangeResolvedInOpenSubmission(
                        uint32_t(texture.key().base_page) << 12, texture.GetGuestBaseSize())) ||
      (load_mips && command_processor_.RangeResolvedInOpenSubmission(
                        uint32_t(texture.key().mip_page) << 12, texture.GetGuestMipsSize()))) {
    command_processor_.SplitSubmissionForFreshResolve();
  }
''')
wr(TC, t)
print("patched OK")
