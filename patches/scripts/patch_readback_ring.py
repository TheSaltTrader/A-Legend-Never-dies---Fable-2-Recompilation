# s99: the readback ring - never overtake a copy that has not landed, and almost
# never wait for it either.
#
# s98 proved the mechanism (0 flashes at the reproducing spot): a target resolved
# again before its readback copy landed had that copy dropped (guest memory left at
# the impostor pool's fill, uploaded over the render by the next invalidation) or
# read later from a buffer the GPU was rewriting. s98 landed it first but had to
# WAIT every time (675 x 3.4 ms per 5 s = 45% of the thread): the impostor pool
# resolves the same target several times inside one frame, so the copy two resolves
# back is still in the open submission. Now each target's readback ring grows on
# demand from 2 up to kReadbackSlots (8): when the slot to be reused still holds an
# unlanded copy whose submission is incomplete, the resolve moves to a fresh slot
# instead; a completed one is landed with a memcpy; only a full ring waits. The
# check moved BEFORE the new copy is recorded (s98 ran after it). Census:
# "superseded N landed (M waited, K ring grown)".
import io

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
CP = ROOT + r"\src\graphics\d3d12\command_processor.cpp"
HDR = ROOT + r"\include\rex\graphics\d3d12\command_processor.h"


def rd(p):
    return io.open(p, "r", encoding="utf-8", newline="").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def rep(s, old, new, count=1):
    assert s.count(old) == count, (old[:70], s.count(old))
    return s.replace(old, new)


h = rd(HDR)
h = rep(h, '''  struct ReadbackBuffer {
    ID3D12Resource* buffers[2] = {nullptr, nullptr};
    uint32_t sizes[2] = {0, 0};
    void* mapped_data[2] = {nullptr, nullptr};
    uint64_t submission_written[2] = {0, 0};
    uint32_t written_size[2] = {0, 0};
    uint32_t current_index = 0;
    uint64_t last_used_frame = 0;
  };''', '''  // [readback] Up to kReadbackSlots buffers per resolve target; the resolve
  // path grows a target's ring (ring_size) on demand so a copy that has not
  // landed is never overtaken. The memexport path uses slots 0 and 1 only.
  static constexpr uint32_t kReadbackSlots = 8;
  struct ReadbackBuffer {
    ID3D12Resource* buffers[kReadbackSlots] = {};
    uint32_t sizes[kReadbackSlots] = {};
    void* mapped_data[kReadbackSlots] = {};
    uint64_t submission_written[kReadbackSlots] = {};
    uint32_t written_size[kReadbackSlots] = {};
    uint32_t current_index = 0;
    uint32_t ring_size = 2;
    uint64_t last_used_frame = 0;
  };''')
wr(HDR, h)

s = rd(CP)
s = rep(s, "static std::atomic<uint32_t> g_superseded_waited{0};\n",
        "static std::atomic<uint32_t> g_superseded_waited{0};\n"
        "static std::atomic<uint32_t> g_ring_grown{0};\n")

# the release / eviction loops over the slots
s = rep(s, '''  for (auto& resolve_readback_pair : readback_buffers_) {
    auto& readback = resolve_readback_pair.second;
    for (uint32_t i = 0; i < 2; ++i) {''', '''  for (auto& resolve_readback_pair : readback_buffers_) {
    auto& readback = resolve_readback_pair.second;
    for (uint32_t i = 0; i < kReadbackSlots; ++i) {''')
s = rep(s, '''    for (uint32_t i = 0; i < 2; ++i) {
      if (readback.buffers[i]) {
        if (readback.mapped_data[i]) {
          readback.buffers[i]->Unmap(0, nullptr);
        }
        // [readback] A copy into it may be in flight: release later.''', '''    for (uint32_t i = 0; i < kReadbackSlots; ++i) {
      if (readback.buffers[i]) {
        if (readback.mapped_data[i]) {
          readback.buffers[i]->Unmap(0, nullptr);
        }
        // [readback] A copy into it may be in flight: release later.''')

# the overtaking check, BEFORE the new copy is recorded
s = rep(s, '''  uint32_t write_index = rb.current_index;
  uint32_t size = AlignReadbackBufferSize(written_length);
''', '''  uint32_t write_index = rb.current_index;
  // [readback] The slot this resolve is about to fill may still hold a copy
  // that has not landed (the same target resolved again before it reached
  // guest memory - the impostor pool does this several times a frame). Never
  // overtake it: a copy whose submission has completed is landed now (a
  // memcpy); otherwise the resolve moves on to a fresh slot of the ring, grown
  // on demand; only a full ring waits for that one submission. Dropping the
  // copy left guest memory at the pool's fill, which the next invalidation
  // uploaded over the render; landing it later read a buffer the GPU was
  // rewriting: the white/violet impostor flash (0.2.11).
  if (GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve)) == ReadbackResolveMode::kSome &&
      REXCVAR_GET(readback_resolve_keep_superseded)) {
    for (uint32_t attempt = 0; attempt <= kReadbackSlots; ++attempt) {
      uint64_t await = 0;
      uint32_t held = 0;
      for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
        if (p.key == resolve_key && p.index == write_index) {
          ++held;
          await = std::max(await, p.submission);
        }
      }
      if (!held) break;
      if (await > GetCompletedSubmission()) {
        if (rb.ring_size < kReadbackSlots) {
          ++rb.ring_size;
          write_index = rb.ring_size - 1;
          rb.current_index = write_index;
          g_ring_grown.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (await >= submission_current_ && submission_open_) EndSubmission(false);
        CheckSubmissionFence(std::min(await, submission_current_ - 1));
        g_superseded_waited.fetch_add(1, std::memory_order_relaxed);
      }
      size_t kept = 0;
      for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {
        const PendingResolveReadback p = pending_resolve_readbacks_[i];
        if (p.key != resolve_key || p.index != write_index) {
          pending_resolve_readbacks_[kept++] = p;
          continue;
        }
        shared_memory_->UnprotectGpuRange(p.address, p.length);
        if (REXCVAR_GET(readback_resolve_on_demand))
          memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);
        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {
          CopyToGuestMemory(p.address, rb.mapped_data[p.index], p.length);
        }
      }
      pending_resolve_readbacks_.resize(kept);
      g_superseded_kept.fetch_add(held, std::memory_order_relaxed);
      break;
    }
  }
  uint32_t size = AlignReadbackBufferSize(written_length);
''')

# the old supersede block (after the copy) goes: it is handled above now
a = s.index("    {\n      uint32_t superseded = 0;\n")
b = s.index("    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);", a)
s = s[:a] + s[b:]

# ring arithmetic instead of the two-slot toggle (resolve path only)
s = rep(s, '''  uint32_t read_index = write_index;
  if (use_delayed_sync) {
    read_index = 1 - write_index;''', '''  uint32_t read_index = write_index;
  if (use_delayed_sync) {
    read_index = (write_index + rb.ring_size - 1) % rb.ring_size;''')
s = rep(s, "    rb.current_index = 1 - rb.current_index;\n    return true;\n  }\n  if (is_cache_miss) {",
        "    rb.current_index = (rb.current_index + 1) % rb.ring_size;\n    return true;\n  }\n  if (is_cache_miss) {")
s = rep(s, '''      rb.current_index = 1 - rb.current_index;
      return true;
    }
  }

  bool should_copy = true;''', '''      rb.current_index = (rb.current_index + 1) % rb.ring_size;
      return true;
    }
  }

  bool should_copy = true;''')
s = rep(s, '''    CopyToGuestMemory(written_address, rb.mapped_data[read_index], written_length);
  }

  rb.current_index = 1 - rb.current_index;
  return true;
}''', '''    CopyToGuestMemory(written_address, rb.mapped_data[read_index], written_length);
  }

  rb.current_index = (rb.current_index + 1) % rb.ring_size;
  return true;
}''')

# census
s = rep(s, '''        const uint32_t sw = g_superseded_waited.exchange(0);
        if (ln || sn) {
          char b[280];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies, superseded %u landed (%u waited) %u dropped",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors, sk, sw, sd);
          line += b;
        }''', '''        const uint32_t sw = g_superseded_waited.exchange(0);
        const uint32_t rg = g_ring_grown.exchange(0);
        if (ln || sn) {
          char b[300];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies, superseded %u landed (%u waited, %u ring grown) %u dropped",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors, sk, sw, rg, sd);
          line += b;
        }''')
assert "1 - rb.current_index" not in s, "a two-slot toggle survived"
wr(CP, s)
print("patched OK")
