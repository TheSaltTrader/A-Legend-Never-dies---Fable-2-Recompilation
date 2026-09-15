# s97: a deferred readback copy superseded by a new resolve of the same target (same
# key and readback buffer index, i.e. the target resolved twice before the copy
# landed) used to be DROPPED: its range unprotected, its guest memory never updated -
# still holding whatever the CPU last wrote there (the impostor pool's fill). The
# next invalidation of that page then uploaded the fill over the render: the
# white/violet flash. The 0.2.10 drain never dropped anything (every copy landed at
# once). Now the superseded entry stays pending (readback_resolve_keep_superseded,
# default on): it lands when its own submission completes, from the same buffer,
# which by then holds the old or the new render of that target - either is right.
# The fence line counts them: "superseded N kept" / "N dropped".
import io

CP = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = io.open(CP, "r", encoding="utf-8", newline="").read()


def rep(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


rep('''REXCVAR_DEFINE_BOOL(readback_resolve_mirror_unscaled, true, "GPU/D3D12",''',
    '''REXCVAR_DEFINE_BOOL(readback_resolve_keep_superseded, true, "GPU/D3D12",
                    "Keep a deferred resolve copy pending when the same target is resolved again "
                    "before it landed (it lands when its submission completes, from the same buffer), "
                    "instead of dropping it and leaving the CPU copy stale - the stale bytes were "
                    "uploaded over the render on the next invalidation: the white/violet impostor flash")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(readback_resolve_mirror_unscaled, true, "GPU/D3D12",''')
rep("static std::atomic<uint32_t> g_mirror_copies{0};\n",
    "static std::atomic<uint32_t> g_mirror_copies{0};\n"
    "static std::atomic<uint32_t> g_superseded_kept{0};\n"
    "static std::atomic<uint32_t> g_superseded_dropped{0};\n")
rep('''    DropPendingResolveReadbacks(resolve_key, write_index);  // superseded
    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);''',
    '''    {
      uint32_t superseded = 0;
      for (const PendingResolveReadback& p : pending_resolve_readbacks_) {
        if (p.key == resolve_key && p.index == write_index) ++superseded;
      }
      if (superseded) {
        if (REXCVAR_GET(readback_resolve_keep_superseded)) {
          // [readback] Kept pending: it lands from this same buffer when its
          // submission completes - the old or the new render, never the fill.
          g_superseded_kept.fetch_add(superseded, std::memory_order_relaxed);
        } else {
          g_superseded_dropped.fetch_add(superseded, std::memory_order_relaxed);
          DropPendingResolveReadbacks(resolve_key, write_index);  // superseded
        }
      }
    }
    const int32_t budget = REXCVAR_GET(readback_resolve_sync_budget);''')
rep('''        const uint32_t mirrors = g_mirror_copies.exchange(0);
        if (ln || sn) {
          char b[220];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors);
          line += b;
        }''', '''        const uint32_t mirrors = g_mirror_copies.exchange(0);
        const uint32_t sk = g_superseded_kept.exchange(0);
        const uint32_t sd = g_superseded_dropped.exchange(0);
        if (ln || sn) {
          char b[260];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies, superseded %u kept %u dropped",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors, sk, sd);
          line += b;
        }''')
io.open(CP, "w", encoding="utf-8", newline="").write(s)
print("patched OK")
