# s100: land or await a pending readback copy IN THE UPLOAD, before its bytes are read.
#
# The only consumer of guest memory that could carry the impostor pool's stale fill
# into the GPU is our own upload (D3D12SharedMemory::UploadRanges memcpy from guest
# memory). The 0.2.8/0.2.10 landing-and-wait hooks sat in the texture load, which
# runs AFTER RequestTextures has uploaded (and made valid) the pages, so they never
# saw a stale page. Now the upload itself, per range and before the memcpy, lands
# every completed readback copy overlapping it and waits (for that submission only)
# for an in-flight one from an earlier submission; a copy in the still-open
# submission cannot be awaited inside a draw and is only counted. Fence line:
# "upload landed N / awaited M / open K readbacks".
import io

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
CP = ROOT + r"\src\graphics\d3d12\command_processor.cpp"
HDR = ROOT + r"\include\rex\graphics\d3d12\command_processor.h"
SM = ROOT + r"\src\graphics\d3d12\shared_memory.cpp"


def rd(p):
    return io.open(p, "r", encoding="utf-8", newline="").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def rep(s, old, new):
    assert s.count(old) == 1, old[:70]
    return s.replace(old, new)


h = rd(HDR)
h = rep(h, "  void LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length);\n",
        "  void LandImpostorReadbackBeforeUpload(uint32_t address, uint32_t length);\n"
        "  // [readback] Called by the shared-memory upload before it reads guest memory:\n"
        "  // land every completed readback copy overlapping the range, wait for an\n"
        "  // in-flight one from an earlier submission (that fence only), count an open one.\n"
        "  void LandOrAwaitReadbackForUpload(uint32_t address, uint32_t length);\n")
wr(HDR, h)

s = rd(CP)
s = rep(s, "static std::atomic<uint32_t> g_ring_grown{0};\n",
        "static std::atomic<uint32_t> g_ring_grown{0};\n"
        "static std::atomic<uint32_t> g_upload_landed{0};\n"
        "static std::atomic<uint32_t> g_upload_awaited{0};\n"
        "static std::atomic<uint32_t> g_upload_open{0};\n")
s = rep(s, "bool D3D12CommandProcessor::ShouldDeferTextureUpload(uint32_t address, uint32_t length) {",
        '''void D3D12CommandProcessor::LandOrAwaitReadbackForUpload(uint32_t address, uint32_t length) {
  if (!length || pending_resolve_readbacks_.empty()) return;
  if (!HasPendingResolveReadback(address, length)) return;
  const uint32_t landed = LandCompletedResolveReadback(address, length);
  if (landed) g_upload_landed.fetch_add(landed, std::memory_order_relaxed);
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
    const char* previous_reason = fence_reason_;
    fence_reason_ = "readback before upload";
    CheckSubmissionFence(await);
    fence_reason_ = previous_reason;
    g_upload_awaited.fetch_add(1, std::memory_order_relaxed);
    const uint32_t landed_after = LandCompletedResolveReadback(address, length);
    if (landed_after) g_upload_landed.fetch_add(landed_after, std::memory_order_relaxed);
  }
  if (open) g_upload_open.fetch_add(open, std::memory_order_relaxed);
}

bool D3D12CommandProcessor::ShouldDeferTextureUpload(uint32_t address, uint32_t length) {''')
s = rep(s, '''        const uint32_t rg = g_ring_grown.exchange(0);
        if (ln || sn) {''', '''        const uint32_t rg = g_ring_grown.exchange(0);
        {
          const uint32_t ul = g_upload_landed.exchange(0);
          const uint32_t ua = g_upload_awaited.exchange(0);
          const uint32_t uo = g_upload_open.exchange(0);
          if (ul || ua || uo) {
            char c[120];
            std::snprintf(c, sizeof(c), "%supload landed %u / awaited %u / open %u readbacks",
                          line.empty() ? "" : ", ", ul, ua, uo);
            line += c;
          }
        }
        if (ln || sn) {''')
# The s96 mirror copy is itself a flash source (protocol v3, 70-s pans, two runs
# each: mirror on 82 and 75 flashes, off 0 and 1): the 1x downscale written into
# the unscaled buffer is not what an unscaled texture load of that range expects.
# Off by default; kept as an experiment key.
s = rep(s, 'REXCVAR_DEFINE_BOOL(readback_resolve_mirror_unscaled, true, "GPU/D3D12",',
        'REXCVAR_DEFINE_BOOL(readback_resolve_mirror_unscaled, false, "GPU/D3D12",')
wr(CP, s)

m = rd(SM)
m = rep(m, '''      MakeRangeValid(upload_range_start << page_size_log2(), uint32_t(upload_buffer_size), false);
      command_processor_.NoteSharedMemoryUpload(uint64_t(upload_buffer_size));  // [hitch]''',
        '''      // [readback] Before the bytes below are read from guest memory: any resolve
      // copy still owed to this range lands now (or is awaited, an earlier
      // submission only) so the upload carries the render, not the stale fill.
      command_processor_.LandOrAwaitReadbackForUpload(upload_range_start << page_size_log2(),
                                                      uint32_t(upload_buffer_size));
      MakeRangeValid(upload_range_start << page_size_log2(), uint32_t(upload_buffer_size), false);
      command_processor_.NoteSharedMemoryUpload(uint64_t(upload_buffer_size));  // [hitch]''')
wr(SM, m)
print("patched OK")
