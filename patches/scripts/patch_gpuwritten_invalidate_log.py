"""s68/s68b diagnostics - when a CPU write invalidates pages the GPU had written
(a resolved render target, an impostor), the plugin logs it: the first 30
with address, length and whether the write was exact, then a count every
1000. A texture on those pages is re-uploaded from CPU memory afterwards -
the readback copy, which may be older than the render or never landed - so
this is the event behind a white impostor or lake flash, if that theory
holds. Correlate with a screen recording. Keep; costs nothing measurable.
APPLY ONCE (requires patch_readback_selfcopy.py)."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\shared_memory.cpp"
s = open(P, encoding="utf-8").read()
assert "gpu-written pages invalidated" not in s, "already applied"
old = '''  for (uint32_t i = block_first; i <= block_last; ++i) {
    uint64_t invalidate_bits = UINT64_MAX;
    if (i == block_first) {
      invalidate_bits &= ~((uint64_t(1) << (page_first & 63)) - 1);
    }
    if (i == block_last && (page_last & 63) != 63) {
      invalidate_bits &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
    }
    system_page_flags_valid_[i] &= ~invalidate_bits;
    system_page_flags_valid_and_gpu_written_[i] &= ~invalidate_bits;
  }
'''
new = '''  uint32_t gpu_written_hit = 0;
  for (uint32_t i = block_first; i <= block_last; ++i) {
    uint64_t invalidate_bits = UINT64_MAX;
    if (i == block_first) {
      invalidate_bits &= ~((uint64_t(1) << (page_first & 63)) - 1);
    }
    if (i == block_last && (page_last & 63) != 63) {
      invalidate_bits &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
    }
    gpu_written_hit += uint32_t(rex::bit_count(system_page_flags_valid_and_gpu_written_[i] & invalidate_bits));
    system_page_flags_valid_[i] &= ~invalidate_bits;
    system_page_flags_valid_and_gpu_written_[i] &= ~invalidate_bits;
  }
  if (gpu_written_hit) {
    // [diag] A CPU write over GPU-rendered pages: whatever texture lives
    // there is re-uploaded from CPU memory next - the readback copy, older
    // than the render or never landed. The event behind a white impostor.
    static std::atomic<uint32_t> n{0};
    static std::atomic<uint64_t> bucket_second{0};
    static std::atomic<uint32_t> bucket_count{0};
    const uint32_t k = ++n;
    // Up to 40 lines a second (each flash needs its own line to be matched
    // with a recording), then a count.
    const uint64_t now_s = uint64_t(std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    if (bucket_second.exchange(now_s) != now_s) bucket_count.store(0);
    if (++bucket_count <= 40 || k % 1000 == 0)
      REXLOG_INFO("[diag] gpu-written pages invalidated by a CPU write: {} page(s) at {:08X} len {} "
                  "exact {} ({} so far)", gpu_written_hit, physical_address_start, length,
                  exact_range ? 1 : 0, k);
  }
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
if "#include <atomic>" not in s:
    s = s.replace("#include <algorithm>", "#include <algorithm>\n#include <atomic>", 1)
open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: gpu-written invalidation diagnostics")
