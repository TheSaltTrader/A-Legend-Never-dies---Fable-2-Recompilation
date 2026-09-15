"""s80b - which resolves go WITHOUT a submission boundary? The white/magenta
impostor flashes were cured by ending the submission after every deferred
resolve of at most readback_resolve_submit_small_kb (64) KB; the black
one-frame flashes of the distant hills (2026-09-14 19:03, ~30 a minute at
Bower Lake, most on frames that reload 100+ textures) are suspected to be
the same hazard on a resolve above that size. The fence line now carries a
size census of the deferred resolves ("deferred resolves <=64K a <=256K b
<=1M c <=4M d >4M e") and the first 40 deferred resolves larger than the
boundary threshold are logged with their size and address ("[readback]
deferred resolve without boundary"). APPLY ONCE."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
q = open(Q, encoding="utf-8").read()
assert "g_resolve_size_buckets" not in q, "already applied"

def rep(old, new):
    global q
    assert q.count(old) == 1, ("expected 1, found %d: %s" % (q.count(old), old[:90]))
    q = q.replace(old, new)

rep('''static std::atomic<uint32_t> g_resolve_splits{0};
''', '''static std::atomic<uint32_t> g_resolve_splits{0};
// [readback] Size census of the deferred resolves: <=64K, <=256K, <=1M,
// <=4M, >4M guest bytes.
static std::atomic<uint32_t> g_resolve_size_buckets[5];
''')

rep('''      const int32_t submit_kb = REXCVAR_GET(readback_resolve_submit_small_kb);
      const int32_t rebind_kb = REXCVAR_GET(readback_resolve_rebind_small_kb);
      if (submit_kb > 0 && written_length <= uint32_t(submit_kb) * 1024u) {
''', '''      const int32_t submit_kb = REXCVAR_GET(readback_resolve_submit_small_kb);
      const int32_t rebind_kb = REXCVAR_GET(readback_resolve_rebind_small_kb);
      {
        const uint32_t kb = written_length / 1024u;
        const size_t bucket = kb <= 64 ? 0 : kb <= 256 ? 1 : kb <= 1024 ? 2 : kb <= 4096 ? 3 : 4;
        g_resolve_size_buckets[bucket].fetch_add(1, std::memory_order_relaxed);
        if (submit_kb > 0 && written_length > uint32_t(submit_kb) * 1024u) {
          static uint32_t logged = 0;
          if (logged < 40) {
            ++logged;
            REXLOG_INFO("[readback] deferred resolve without boundary: {} KB at {:08X} (submission {})",
                        kb, written_address, GetCurrentSubmission());
          }
        }
      }
      if (submit_kb > 0 && written_length <= uint32_t(submit_kb) * 1024u) {
''')

rep('''    const uint32_t splits = g_resolve_splits.exchange(0);
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
    {
      uint32_t sz[5];
      uint32_t total = 0;
      for (size_t i = 0; i < 5; ++i) {
        sz[i] = g_resolve_size_buckets[i].exchange(0);
        total += sz[i];
      }
      if (total) {
        char b[160];
        std::snprintf(b, sizeof(b), "%sdeferred resolves <=64K %u <=256K %u <=1M %u <=4M %u >4M %u",
                      line.empty() ? "" : ", ", sz[0], sz[1], sz[2], sz[3], sz[4]);
        line += b;
      }
    }
''')
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: deferred resolve size census on the fence line")
