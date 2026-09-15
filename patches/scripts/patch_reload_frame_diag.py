"""s80c - name the textures of a RELOAD FRAME. Every 2-3 s a frame reloads
100+ textures (36 MB) at once, and at daylight the distant ridge draws black
for exactly that frame with readback_resolve=some (2026-09-14 19:03). The
texture cache now logs, for the first 10 such frames (>= 40 loads in the
frame), up to 12 of the reloaded textures: guest address, base size, size in
texels, format, whether the shared memory holds the range as GPU-written,
and whether a deferred resolve readback into it is still pending. Two small
public accessors on the command processor carry the frame's load count and
the pending-readback test. Diagnostic only; no behaviour change. APPLY
ONCE."""
H = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\include\rex\graphics\d3d12\command_processor.h"
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
T = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"
h = open(H, encoding="utf-8").read()
q = open(Q, encoding="utf-8").read()
t = open(T, encoding="utf-8").read()
assert "HasPendingResolveReadback" not in h, "already applied"

def rep(s, old, new):
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    return s.replace(old, new)

h = rep(h, '''  void NoteSharedMemoryUpload(uint64_t bytes);
''', '''  void NoteSharedMemoryUpload(uint64_t bytes);
  // [diag] Texture loads counted so far in the current guest frame.
  uint32_t FrameTextureLoads() const;
  // [readback] Whether a deferred resolve copy into any part of the range has
  // not landed in guest memory yet.
  bool HasPendingResolveReadback(uint32_t address, uint32_t length) const;
''')

q = rep(q, '''void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {
''', '''uint32_t D3D12CommandProcessor::FrameTextureLoads() const { return g_frame_textures_loaded; }

bool D3D12CommandProcessor::HasPendingResolveReadback(uint32_t address, uint32_t length) const {
  const uint64_t end = uint64_t(address) + length;
  for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
    if (p.address < end && uint64_t(p.address) + p.length > address) return true;
  }
  return false;
}

void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {
''')

t = rep(t, '''  command_processor_.NoteTextureLoad(  // [hitch]
      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +
      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));
''', '''  command_processor_.NoteTextureLoad(  // [hitch]
      uint64_t(load_base ? texture.GetGuestBaseSize() : 0u) +
      uint64_t(load_mips ? texture.GetGuestMipsSize() : 0u));
  // [diag] A reload frame (>= 40 texture loads): name up to 12 of its
  // textures, for the first 10 such frames. The black distant-ridge flash of
  // 2026-09-14 lands on exactly these frames.
  {
    static uint32_t diag_frames = 0;
    static uint32_t diag_last_count = 0;
    static uint32_t diag_in_frame = 0;
    const uint32_t loads = command_processor_.FrameTextureLoads();
    if (loads < diag_last_count) diag_in_frame = 0;  // a new frame began
    diag_last_count = loads;
    if (loads >= 40 && diag_frames < 10) {
      if (loads == 40) ++diag_frames;
      if (diag_in_frame < 12) {
        ++diag_in_frame;
        const TextureKey& k = texture.key();
        const uint32_t base = uint32_t(k.base_page) << 12;
        const uint32_t size = texture.GetGuestBaseSize();
        REXLOG_INFO("[diag] reload frame #{} texture {:08X}+{} {}x{} fmt {} mips {}{}{}",
                    diag_frames, base, size, uint32_t(k.width_minus_1) + 1,
                    uint32_t(k.height_minus_1) + 1, uint32_t(k.format), uint32_t(k.mip_max_level),
                    shared_memory().AnyPageGpuWritten(base, size) ? " GPU-written" : "",
                    command_processor_.HasPendingResolveReadback(base, size) ? " READBACK-PENDING" : "");
      }
    }
  }
''')
open(H, "w", encoding="utf-8", newline="").write(h)
open(Q, "w", encoding="utf-8", newline="").write(q)
open(T, "w", encoding="utf-8", newline="").write(t)
print("patched: reload-frame texture diagnostic")
