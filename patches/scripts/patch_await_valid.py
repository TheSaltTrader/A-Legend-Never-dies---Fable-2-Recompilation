# s92: the on-demand readback wait only when the upload would actually read guest
# memory. A texture load reads the GPU-side shared memory buffer; CPU memory is read
# only for pages that are NOT valid there (RequestRange uploads them). A resolve
# target's pages are valid and GPU-written right after the resolve, so most loads
# that overlap a pending readback never touch CPU memory - s90 waited for them all
# (160 waits/s, ~1.3 s per 5 s in the market, 40 fps). Now: land completed copies as
# before, then wait only when some page of the range is invalid.
import io

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
SM_H = ROOT + r"\include\rex\graphics\shared_memory.h"
SM_C = ROOT + r"\src\graphics\shared_memory.cpp"
CP = ROOT + r"\src\graphics\d3d12\command_processor.cpp"


def rd(p):
    return io.open(p, "r", encoding="utf-8", newline="").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def rep(s, old, new):
    assert s.count(old) == 1, old[:70]
    return s.replace(old, new)


h = rd(SM_H)
h = rep(h, "  void ProtectGpuRange(uint32_t start, uint32_t length);\n",
        "  void ProtectGpuRange(uint32_t start, uint32_t length);\n"
        "  // [readback] Every page of the range is valid in the GPU buffer: a texture\n"
        "  // load of it reads no CPU memory, so a pending readback copy cannot leak in.\n"
        "  bool AllPagesValid(uint32_t start, uint32_t length);\n")
wr(SM_H, h)

c = rd(SM_C)
c = rep(c, "bool SharedMemory::AnyPageGpuWritten(uint32_t start, uint32_t length) {\n",
        '''bool SharedMemory::AllPagesValid(uint32_t start, uint32_t length) {
  if (length == 0 || start >= kBufferSize) {
    return false;
  }
  length = std::min(length, kBufferSize - start);
  const uint32_t page_first = start >> page_size_log2_;
  const uint32_t page_last = (start + length - 1) >> page_size_log2_;
  auto global_lock = global_critical_region_.Acquire();
  for (uint32_t i = page_first >> 6; i <= (page_last >> 6); ++i) {
    uint64_t bits = UINT64_MAX;
    if (i == (page_first >> 6)) {
      bits &= ~((uint64_t(1) << (page_first & 63)) - 1);
    }
    if (i == (page_last >> 6) && (page_last & 63) != 63) {
      bits &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
    }
    if ((system_page_flags_valid_[i] & bits) != bits) {
      return false;
    }
  }
  return true;
}

bool SharedMemory::AnyPageGpuWritten(uint32_t start, uint32_t length) {
''')
wr(SM_C, c)

s = rd(CP)
s = rep(s, '''  if (!REXCVAR_GET(readback_await_before_texture_upload)) return;
  // [readback] A copy still in flight from an EARLIER submission: wait for that
''', '''  if (!REXCVAR_GET(readback_await_before_texture_upload)) return;
  // [readback] The load reads CPU memory only for pages that are not valid in
  // the GPU buffer; a resolve target's pages are valid (GPU-written) after the
  // resolve, so nothing stale can reach the texture - no wait (s92: the wait
  // for every overlapping load cost 1.3 s per 5 s in the market).
  if (shared_memory_ && shared_memory_->AllPagesValid(address, length)) {
    g_readbacks_valid_no_wait.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // [readback] A copy still in flight from an EARLIER submission: wait for that
''')
s = rep(s, "static std::atomic<uint32_t> g_readbacks_open_at_upload{0};\n",
        "static std::atomic<uint32_t> g_readbacks_open_at_upload{0};\n"
        "static std::atomic<uint32_t> g_readbacks_valid_no_wait{0};\n")
s = rep(s, '''      const uint32_t open = g_readbacks_open_at_upload.exchange(0);
      if (awaited || open) {
        char b[120];
        std::snprintf(b, sizeof(b), "%sawaited %u readbacks before upload (%.1f ms), %u still open",
                      line.empty() ? "" : ", ", awaited, us / 1000.0, open);
        line += b;
      }
''', '''      const uint32_t open = g_readbacks_open_at_upload.exchange(0);
      const uint32_t valid = g_readbacks_valid_no_wait.exchange(0);
      if (awaited || open || valid) {
        char b[160];
        std::snprintf(b, sizeof(b),
                      "%sawaited %u readbacks before upload (%.1f ms), %u still open, %u pages valid (no wait)",
                      line.empty() ? "" : ", ", awaited, us / 1000.0, open, valid);
        line += b;
      }
''')
wr(CP, s)
print("patched OK")
