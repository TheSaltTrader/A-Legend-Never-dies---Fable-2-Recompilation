"""s82 - the black distant-ridge flash. On a mass-reload frame the texture
cache re-uploads the distant-terrain impostors (small render targets the game
resolved then samples) from guest memory - but with readback_resolve=some the
resolve's copy into that memory is DEFERRED and may not have landed, so the
impostor reads the old/empty bytes: black for one frame (2026-09-14, user's
"the next hill goes black" after a focus loss). Cure: before a texture loads
from resident memory, if a resolve readback into its base range is pending and
that resolve's submission has ALREADY completed, land the copy now (a memcpy
from the readback buffer, no GPU wait, no submission boundary - safe inside
command-list building). A readback from the still-open submission is left as
is (rare for a stable impostor). Opt-in: readback_land_before_texture_upload
(bool, default false) until the flash is confirmed cured on the user's
session. "[readback] landed N impostor readbacks before their texture upload"
on the fence line. APPLY ONCE (requires patch_reload_frame_diag.py)."""
H = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\include\rex\graphics\d3d12\command_processor.h"
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
T = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"
h = open(H, encoding="utf-8").read()
q = open(Q, encoding="utf-8").read()
t = open(T, encoding="utf-8").read()
assert "LandCompletedResolveReadback" not in h, "already applied"
assert "FrameTextureLoads()" in h, "needs patch_reload_frame_diag.py first"

def rep(s, old, new):
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    return s.replace(old, new)

h = rep(h, '''  // [readback] Whether a deferred resolve copy into any part of the range has
  // not landed in guest memory yet.
  bool HasPendingResolveReadback(uint32_t address, uint32_t length) const;
''', '''  // [readback] Whether a deferred resolve copy into any part of the range has
  // not landed in guest memory yet.
  bool HasPendingResolveReadback(uint32_t address, uint32_t length) const;
  // [readback] Land - into guest memory now - every pending resolve copy that
  // overlaps the range AND whose submission has already completed (a memcpy,
  // no GPU wait, no submission boundary). Returns how many it landed. Safe to
  // call while building a command list. Used before a texture uploads from
  // resident memory, so an impostor gets the fresh render, not stale bytes.
  uint32_t LandCompletedResolveReadback(uint32_t address, uint32_t length);
  // [readback] The opt-in guard around it, called from the texture load path
  // (the cvar lives in the .cpp, so the gate must too).
  void LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length);
''')

q = rep(q, '''REXCVAR_DEFINE_BOOL(readback_resolve_on_demand, false, "GPU/D3D12",
''', '''REXCVAR_DEFINE_BOOL(readback_land_before_texture_upload, false, "GPU/D3D12",
                    "Before a texture loads from resident memory, land any completed deferred resolve "
                    "copy into its range (fixes the black distant-impostor flash on reload frames)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
static std::atomic<uint32_t> g_impostor_readbacks_landed{0};
REXCVAR_DEFINE_BOOL(readback_resolve_on_demand, false, "GPU/D3D12",
''')

q = rep(q, '''bool D3D12CommandProcessor::HasPendingResolveReadback(uint32_t address, uint32_t length) const {
  const uint64_t end = uint64_t(address) + length;
  for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
    if (p.address < end && uint64_t(p.address) + p.length > address) return true;
  }
  return false;
}
''', '''bool D3D12CommandProcessor::HasPendingResolveReadback(uint32_t address, uint32_t length) const {
  const uint64_t end = uint64_t(address) + length;
  for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
    if (p.address < end && uint64_t(p.address) + p.length > address) return true;
  }
  return false;
}

uint32_t D3D12CommandProcessor::LandCompletedResolveReadback(uint32_t address, uint32_t length) {
  if (pending_resolve_readbacks_.empty()) return 0;
  const uint64_t end = uint64_t(address) + length;
  const uint64_t completed = GetCompletedSubmission();
  uint32_t landed = 0;
  size_t kept = 0;
  for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {
    const PendingResolveReadback p = pending_resolve_readbacks_[i];
    const bool overlaps = !(p.address >= end || uint64_t(p.address) + p.length <= address);
    if (!overlaps || p.submission > completed) {
      pending_resolve_readbacks_[kept++] = p;  // still open, or unrelated
      continue;
    }
    shared_memory_->UnprotectGpuRange(p.address, p.length);
    if (REXCVAR_GET(readback_resolve_on_demand))
      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);
    auto it = readback_buffers_.find(p.key);
    if (it != readback_buffers_.end()) {
      ReadbackBuffer& rb = it->second;
      if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {
        CopyToGuestMemory(p.address, rb.mapped_data[p.index], p.length);
        ++landed;
      }
    }
  }
  pending_resolve_readbacks_.resize(kept);
  if (landed) g_impostor_readbacks_landed.fetch_add(landed, std::memory_order_relaxed);
  return landed;
}

void D3D12CommandProcessor::LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length) {
  if (!length || !REXCVAR_GET(readback_land_before_texture_upload)) return;
  if (HasPendingResolveReadback(address, length)) LandCompletedResolveReadback(address, length);
}
''')

q = rep(q, '''    const uint32_t splits = g_resolve_splits.exchange(0);
    if (splits) {
      char b[64];
      std::snprintf(b, sizeof(b), "%sresolve splits %u", line.empty() ? "" : ", ", splits);
      line += b;
    }
''', '''    const uint32_t splits = g_resolve_splits.exchange(0);
    if (splits) {
      char b[64];
      std::snprintf(b, sizeof(b), "%sresolve splits %u", line.empty() ? "" : ", ", splits);
      line += b;
    }
    const uint32_t landed = g_impostor_readbacks_landed.exchange(0);
    if (landed) {
      char b[80];
      std::snprintf(b, sizeof(b), "%slanded %u impostor readbacks before their texture upload",
                    line.empty() ? "" : ", ", landed);
      line += b;
    }
''')

t = rep(t, '''  command_processor_.NoteTextureLoad(  // [hitch]
      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +
      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));
''', '''  command_processor_.NoteTextureLoad(  // [hitch]
      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +
      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));
  // [readback] An impostor (a resolved render target) sampled as a texture:
  // if its resolve copy into guest memory is still deferred, land the
  // already-completed one now so the upload reads the fresh render, not the
  // old/empty bytes (the black distant-ridge flash). Off by default.
  if (load_base) {
    command_processor_.LandImpostorReadbackBeforeUpload(
        uint32_t(texture.key().base_page) << 12, texture.GetGuestBaseSize());
  }
''')
open(H, "w", encoding="utf-8", newline="").write(h)
open(Q, "w", encoding="utf-8", newline="").write(q)
open(T, "w", encoding="utf-8", newline="").write(t)
print("patched: land completed impostor readbacks before texture upload (opt-in)")
