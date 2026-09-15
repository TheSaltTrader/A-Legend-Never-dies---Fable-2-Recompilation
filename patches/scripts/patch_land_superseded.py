# s98: land a superseded readback copy BEFORE its buffer is reused.
#
# Every A/B at the reproducing spot (25 s each): the 0.2.10 drain 0 flashes, drain of
# every resolve <= 256 KB 0; boundaries after every resolve 21, UAV barriers 42,
# 64 KB boundaries 32, none 36, unscaled mirror 23, on-demand landing 28 (and no CPU
# touch of the resolved memory), texture pack off 35, superseded copies kept pending
# 36 / dropped 35. So GPU-side ordering, CPU reads and the pack are out; only landing
# the CPU copy at once cures it. What the drain also does: it never lets a copy be
# overtaken. Each readback target has TWO buffers; the same target resolved a third
# time reuses the first buffer. Until now the copy still pending there was either
# dropped (its guest memory stays at the pool's fill, which the next invalidation
# uploads over the render) or, kept, landed later from a buffer the GPU was already
# rewriting (a torn copy). Now that copy is landed right here, before the new copy
# is recorded: a memcpy when its submission has completed (the usual case), a wait
# for that submission only when it has not - never a full drain. The fence line
# counts "superseded N landed (M waited)".
import io

CP = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = io.open(CP, "r", encoding="utf-8", newline="").read()


def rep(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


rep("static std::atomic<uint32_t> g_superseded_kept{0};\n",
    "static std::atomic<uint32_t> g_superseded_kept{0};\n"
    "static std::atomic<uint32_t> g_superseded_waited{0};\n")
rep('''      if (superseded) {
        if (REXCVAR_GET(readback_resolve_keep_superseded)) {
          // [readback] Kept pending: it lands from this same buffer when its
          // submission completes - the old or the new render, never the fill.
          g_superseded_kept.fetch_add(superseded, std::memory_order_relaxed);
        } else {
          g_superseded_dropped.fetch_add(superseded, std::memory_order_relaxed);
          DropPendingResolveReadbacks(resolve_key, write_index);  // superseded
        }
      }''', '''      if (superseded) {
        if (REXCVAR_GET(readback_resolve_keep_superseded)) {
          // [readback] This resolve is about to refill the buffer that still
          // holds a copy that has not landed. Land it NOW, before the new copy
          // is recorded: a memcpy when its submission has completed (usual), a
          // wait for that submission only when it has not - never a full drain.
          // Dropped, its guest memory stayed at the impostor pool's fill, which
          // the next invalidation uploaded over the render; kept for later, it
          // was read from a buffer the GPU was already rewriting. Either way the
          // white/violet flash; the 0.2.10 drain never let a copy be overtaken.
          uint64_t await = 0;
          for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
            if (p.key == resolve_key && p.index == write_index) await = std::max(await, p.submission);
          }
          if (await > GetCompletedSubmission()) {
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
          g_superseded_kept.fetch_add(superseded, std::memory_order_relaxed);
        } else {
          g_superseded_dropped.fetch_add(superseded, std::memory_order_relaxed);
          DropPendingResolveReadbacks(resolve_key, write_index);  // superseded
        }
      }''')
rep('''        const uint32_t sk = g_superseded_kept.exchange(0);
        const uint32_t sd = g_superseded_dropped.exchange(0);
        if (ln || sn) {
          char b[260];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies, superseded %u kept %u dropped",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors, sk, sd);
          line += b;
        }''', '''        const uint32_t sk = g_superseded_kept.exchange(0);
        const uint32_t sd = g_superseded_dropped.exchange(0);
        const uint32_t sw = g_superseded_waited.exchange(0);
        if (ln || sn) {
          char b[280];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies, superseded %u landed (%u waited) %u dropped",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors, sk, sw, sd);
          line += b;
        }''')
io.open(CP, "w", encoding="utf-8", newline="").write(s)
print("patched OK")
