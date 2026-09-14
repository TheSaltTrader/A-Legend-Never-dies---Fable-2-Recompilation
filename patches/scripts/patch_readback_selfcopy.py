"""s62 part 1 - the plugin's own readback copy must not invalidate the pages it
lands on.

With readback 'some'/'full' the resolved render targets are copied back into
guest memory once the GPU has finished them. That copy is a CPU write into
write-watched pages: the shared memory saw a fault, cleared the pages' "valid
and written by the GPU" state and fired the texture watches, and the texture
cache reloaded every render-target texture from the shared-memory buffer -
five textures, 16.6 MB, every frame in Oakfield (48 fps where the market ran
60). The GPU buffer already holds exactly those bytes, so the copy is now
declared to the shared memory first: the pages are unwatched without being
invalidated, the bytes land, and MakeRangeValid re-arms the watch with the
pages still valid and GPU-written. Only ranges whose pages are ALL valid and
GPU-written take that path; anything else keeps today's behaviour.

APPLY ONCE (anchors are replaced, not re-added).
"""
import os
R = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def patch(rel, pairs):
    p = os.path.join(R, rel)
    s = open(p, encoding="utf-8").read()
    for old, new in pairs:
        n = s.count(old)
        assert n == 1, ("%s: expected 1, found %d: %s" % (rel, n, old[:120]))
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8", newline="").write(s)
    print("patched", rel)

patch("include/rex/graphics/shared_memory.h", [
('''  bool IsGpuRangeProtected(uint32_t start, uint32_t length) const;
''',
'''  bool IsGpuRangeProtected(uint32_t start, uint32_t length) const;

  // A readback copy of the plugin's own, about to land in [start, start +
  // length): the bytes are the ones the GPU buffer already holds. When every
  // page of the range is valid and GPU-written, the range is unwatched
  // without being invalidated and true is returned; the caller writes, then
  // calls EndSelfCopy, which re-arms the watch with the pages still valid
  // and GPU-written. False (any other page state) means: plain write, the
  // usual invalidation applies.
  bool BeginSelfCopy(uint32_t start, uint32_t length);
  void EndSelfCopy(uint32_t start, uint32_t length);
  // Whether any page of the range is currently valid and written by the GPU
  // - a resolve destination, whose bytes came from the GPU and never from
  // art. The texture pack and the texture dump keep away from those.
  bool AnyPageGpuWritten(uint32_t start, uint32_t length);
'''),
('''  std::vector<uint64_t> system_page_flags_valid_and_gpu_written_;
''',
'''  std::vector<uint64_t> system_page_flags_valid_and_gpu_written_;
  // The range of a self copy in progress (BeginSelfCopy), or length 0.
  // Read by MemoryInvalidationCallback under global_critical_region_.
  uint32_t self_copy_start_ = 0;
  uint32_t self_copy_length_ = 0;
'''),
])

patch("src/graphics/shared_memory.cpp", [
('''  auto global_lock = global_critical_region_.Acquire();

  if (!exact_range) {
''',
'''  auto global_lock = global_critical_region_.Acquire();

  // The plugin's own readback copy landing (BeginSelfCopy): the GPU buffer
  // already holds these bytes. The heap lifts the page protection on return;
  // the pages stay valid and GPU-written and no watch fires, so the texture
  // cache keeps its copy of the resolved render target instead of reloading
  // it - which it did five times a frame. EndSelfCopy re-arms the watch.
  if (self_copy_length_ &&
      physical_address_start < uint64_t(self_copy_start_) + self_copy_length_ &&
      uint64_t(physical_address_start) + length > self_copy_start_) {
    return std::make_pair(page_first << page_size_log2_,
                          (page_last - page_first + 1) << page_size_log2_);
  }

  if (!exact_range) {
'''),
('''void SharedMemory::UnlinkWatchRange(WatchRange* range) {
''',
'''bool SharedMemory::BeginSelfCopy(uint32_t start, uint32_t length) {
  if (length == 0 || start >= kBufferSize) {
    return false;
  }
  length = std::min(length, kBufferSize - start);
  const uint32_t page_first = start >> page_size_log2_;
  const uint32_t page_last = (start + length - 1) >> page_size_log2_;
  {
    auto global_lock = global_critical_region_.Acquire();
    if (self_copy_length_) {
      return false;   // one at a time; the caller is the single readback thread
    }
    for (uint32_t i = page_first >> 6; i <= (page_last >> 6); ++i) {
      uint64_t bits = UINT64_MAX;
      if (i == (page_first >> 6)) {
        bits &= ~((uint64_t(1) << (page_first & 63)) - 1);
      }
      if (i == (page_last >> 6) && (page_last & 63) != 63) {
        bits &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
      }
      if ((system_page_flags_valid_and_gpu_written_[i] & bits) != bits) {
        return false;   // the CPU wrote here since the resolve: keep the normal path
      }
    }
    self_copy_start_ = start;
    self_copy_length_ = length;
  }
  // Unwatched in one call rather than one fault per page: the callback above
  // would say the same thing for each of the ~900 pages of a 720p target.
  if (memory_invalidation_callback_handle_) {
    memory().EnablePhysicalMemoryAccessCallbacks(
        page_first << page_size_log2_, (page_last - page_first + 1) << page_size_log2_, false,
        false);
  }
  return true;
}

void SharedMemory::EndSelfCopy(uint32_t start, uint32_t length) {
  {
    auto global_lock = global_critical_region_.Acquire();
    self_copy_length_ = 0;
    self_copy_start_ = 0;
  }
  // Valid, GPU-written, watched again - the state the resolve left them in.
  MakeRangeValid(start, length, true);
}

bool SharedMemory::AnyPageGpuWritten(uint32_t start, uint32_t length) {
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
    if (system_page_flags_valid_and_gpu_written_[i] & bits) {
      return true;
    }
  }
  return false;
}

void SharedMemory::UnlinkWatchRange(WatchRange* range) {
'''),
])

patch("src/graphics/d3d12/command_processor.cpp", [
('''  std::memcpy(destination, source, length);
  return true;
}
''',
'''  // Declared to the shared memory first when the pages are all valid and
  // GPU-written: the bytes below are the ones the GPU buffer holds, so the
  // write must not invalidate them or fire the texture watches (which made
  // the texture cache reload every resolved render target once a frame).
  const bool quiet = shared_memory_ && shared_memory_->BeginSelfCopy(address, length);
  std::memcpy(destination, source, length);
  if (quiet) {
    shared_memory_->EndSelfCopy(address, length);
    g_guest_copy_quiet.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}
'''),
('''      g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);
''',
'''      g_guest_copy_faults.fetch_add(1, std::memory_order_relaxed);
'''),
])

# the counter, beside the faults counter (whatever form it has)
p = os.path.join(R, "src/graphics/d3d12/command_processor.cpp")
s = open(p, encoding="utf-8").read()
import re
m = re.search(r"^(static\s+)?std::atomic<[^>]+>\s+g_guest_copy_faults[^\n]*\n", s, re.M)
assert m, "g_guest_copy_faults definition not found"
assert "g_guest_copy_quiet" not in s[:m.start()]
s = s[:m.end()] + m.group(0).replace("g_guest_copy_faults", "g_guest_copy_quiet") + s[m.end():]
open(p, "w", encoding="utf-8", newline="").write(s)
print("counter g_guest_copy_quiet added")
